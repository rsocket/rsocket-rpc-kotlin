/*
 * Copyright 2015-2018 the original author or authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
package io.rsocket.rpc.kotlin.transport.internal.netty

import io.netty.buffer.ByteBuf
import io.netty.buffer.Unpooled.wrappedBuffer
import io.netty.handler.codec.http.websocketx.BinaryWebSocketFrame
import io.reactivex.Completable
import io.reactivex.Flowable
import io.rsocket.kotlin.DuplexConnection
import io.rsocket.kotlin.Frame
import org.reactivestreams.Publisher
import reactor.netty.Connection

class InternalWebsocketDuplexConnection(private val connection:Connection) : DuplexConnection {
    private val allocator = connection.channel().alloc()
    
    override fun send(frame: Publisher<Frame>): Completable {
        return Flowable.fromPublisher(frame)
                .concatMap { sendOne(it).toFlowable<Frame>() }
                .ignoreElements()
    }

    override fun sendOne(frame: Frame): Completable {
        return connection.outbound().sendObject(
                BinaryWebSocketFrame(
                        frame.content().skipBytes(frameLengthSize)))
                .then()
                .toCompletable()
    }

    override fun receive(): Flowable<Frame> {
        return connection.inbound().receive()
                .map { buf ->
                    val composite = allocator.compositeBuffer()
                    val lengthBuffer = wrappedBuffer(ByteArray(frameLengthSize))
                    encodeLength(lengthBuffer, 0, buf.readableBytes())
                    composite.addComponents(true, lengthBuffer, buf.retain())
                    Frame.from(composite)
                }.toFlowable()
    }

    override fun availability(): Double = if (connection.isDisposed) 0.0 else 1.0

    override fun close(): Completable =
            Completable.fromRunnable {
                if (!connection.isDisposed) {
                    connection.channel().close()
                }
            }
    
    override fun onClose(): Completable = connection.onDispose().toCompletable()

    private fun encodeLength(byteBuf: ByteBuf, offset: Int, length: Int) {
        if (length and frameLengthMask.inv() != 0) {
            throw IllegalArgumentException("Length is larger than 24 bits")
        }
        byteBuf.setByte(offset, length shr 16)
        byteBuf.setByte(offset + 1, length shr 8)
        byteBuf.setByte(offset + 2, length)
    }
}
