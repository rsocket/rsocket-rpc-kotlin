/*
 * Copyright 2015-2018 the original author or authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package io.rsocket.rpc.kotlin.rsocket

import io.netty.buffer.ByteBuf
import io.netty.buffer.Unpooled
import io.rsocket.rpc.kotlin.exception.ServiceNotFound
import io.rsocket.rpc.kotlin.frames.Metadata
import io.rsocket.rpc.kotlin.util.StreamSplitter
import io.netty.util.ReferenceCountUtil
import io.reactivex.Completable
import io.reactivex.Flowable
import io.reactivex.Single
import io.rsocket.kotlin.Payload
import io.rsocket.kotlin.util.AbstractRSocket
import io.rsocket.rpc.kotlin.RSocketRpcService
import org.reactivestreams.Publisher
import java.util.concurrent.ConcurrentHashMap

class RequestHandlingRSocket(vararg services: RSocketRpcService) : AbstractRSocket() {
    private val registeredServices = ConcurrentHashMap<String, RSocketRpcService>()

    init {
        for (proteusService in services) {
            val service = proteusService.service
            registeredServices[service] = proteusService
        }
    }

    fun addService(proteusService: RSocketRpcService) {
        val service = proteusService.service
        registeredServices[service] = proteusService
    }

    override fun fireAndForget(payload: Payload): Completable {
        try {
            val metadata = payload.byteBufMetadata()
            val service = Metadata.getService(metadata)

            val proteusService = registeredServices[service]

            if (proteusService == null) {
                ReferenceCountUtil.safeRelease(payload)
                return Completable.error(ServiceNotFound(service))
            }

            return proteusService.fireAndForget(payload)
        } catch (t: Throwable) {
            ReferenceCountUtil.safeRelease(payload)
            return Completable.error(t)
        }

    }

    override fun requestResponse(payload: Payload): Single<Payload> {
        try {
            val metadata = payload.byteBufMetadata()
            val service = Metadata.getService(metadata)

            val proteusService = registeredServices[service]

            if (proteusService == null) {
                ReferenceCountUtil.safeRelease(payload)
                return Single.error(ServiceNotFound(service))
            } else {
                return proteusService.requestResponse(payload)
            }
        } catch (t: Throwable) {
            ReferenceCountUtil.safeRelease(payload)
            return Single.error(t)
        }
    }

    override fun requestStream(payload: Payload): Flowable<Payload> {
        try {
            val metadata = payload.byteBufMetadata()
            val service = Metadata.getService(metadata)

            val proteusService = registeredServices[service]

            if (proteusService == null) {
                ReferenceCountUtil.safeRelease(payload)
                return Flowable.error(ServiceNotFound(service))
            }

            return proteusService.requestStream(payload)
        } catch (t: Throwable) {
            ReferenceCountUtil.safeRelease(payload)
            return Flowable.error(t)
        }
    }

    override fun requestChannel(payloads: Publisher<Payload>): Flowable<Payload> {
        return StreamSplitter.split(payloads).flatMap { split ->
            val payload = split.head
            val flow = split.tail
            try {
                val metadata = payload.byteBufMetadata()
                val service = Metadata.getService(metadata)
                val proteusService = registeredServices[service]
                if (proteusService == null) {
                    ReferenceCountUtil.safeRelease(payload)
                    Flowable.error(ServiceNotFound(service))
                } else {
                    proteusService.requestChannel(payload, flow)
                }
            } catch (t: Throwable) {
                ReferenceCountUtil.safeRelease(payload)
                Flowable.error<Payload>(t)
            }
        }
    }

    private fun Payload.byteBufMetadata(): ByteBuf = Unpooled.wrappedBuffer(metadata)
}
