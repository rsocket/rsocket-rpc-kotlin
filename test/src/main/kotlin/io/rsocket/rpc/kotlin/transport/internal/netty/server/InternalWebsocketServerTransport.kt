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

package io.rsocket.rpc.kotlin.transport.internal.netty.server

import io.reactivex.Single
import io.rsocket.kotlin.transport.ServerTransport
import io.rsocket.kotlin.transport.TransportHeaderAware
import io.rsocket.rpc.kotlin.transport.internal.netty.InternalWebsocketDuplexConnection
import io.rsocket.rpc.kotlin.transport.internal.netty.frameLengthMask
import io.rsocket.rpc.kotlin.transport.internal.netty.toSingle
import reactor.netty.Connection
import reactor.netty.http.server.HttpServer

class InternalWebsocketServerTransport private constructor(internal var server: HttpServer)
    : ServerTransport<CloseableChannel>, TransportHeaderAware {
    private var transportHeaders: () -> Map<String, String> = { emptyMap() }

    override fun start(acceptor: ServerTransport.ConnectionAcceptor)
            : Single<CloseableChannel> {
        return server
                .handle { _, response ->
                    transportHeaders()
                            .forEach { (name, value) -> response.addHeader(name, value) }
                    response.sendWebsocket(null,
                        frameLengthMask
                    ) { inbound, outbound ->
                        val connection =
                            InternalWebsocketDuplexConnection(inbound as Connection)
                        acceptor(connection).andThen(outbound.neverComplete())
                    }
                }.bind().toSingle().map { CloseableChannel(it) }
    }

    override fun setTransportHeaders(transportHeaders: () -> Map<String, String>) {
        this.transportHeaders = transportHeaders
    }

    companion object {

        fun create(bindAddress: String, port: Int): InternalWebsocketServerTransport {
            val httpServer = HttpServer.create().host(bindAddress).port(port)
            return create(
                httpServer
            )
        }

        fun create(port: Int): InternalWebsocketServerTransport {
            val httpServer = HttpServer.create().port(port)
            return create(
                httpServer
            )
        }

        fun create(server: HttpServer): InternalWebsocketServerTransport {
            return InternalWebsocketServerTransport(server)
        }
    }
}
