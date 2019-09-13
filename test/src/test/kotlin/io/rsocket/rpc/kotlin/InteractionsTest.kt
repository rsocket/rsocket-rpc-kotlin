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

package io.rsocket.rpc.kotlin

import io.netty.buffer.ByteBuf
import io.netty.buffer.Unpooled
import io.netty.util.ResourceLeakDetector
import io.reactivex.Completable
import io.reactivex.Flowable
import io.reactivex.Single
import io.reactivex.processors.ReplayProcessor
import io.reactivex.subscribers.TestSubscriber
import io.rsocket.kotlin.Duration
import io.rsocket.kotlin.RSocketFactory
import io.rsocket.kotlin.exceptions.ApplicationException
import io.rsocket.rpc.kotlin.rsocket.RequestHandlingRSocket
import io.rsocket.rpc.kotlin.test.*
import io.rsocket.rpc.kotlin.transport.internal.netty.server.CloseableChannel
import io.rsocket.rpc.kotlin.transport.internal.netty.server.InternalWebsocketServerTransport
import io.rsocket.rpc.kotlin.util.LongTest
import io.rsocket.transport.okhttp.client.OkhttpWebsocketClientTransport
import okhttp3.HttpUrl
import org.junit.jupiter.api.AfterAll
import org.junit.jupiter.api.Assertions
import org.junit.jupiter.api.BeforeAll
import org.junit.jupiter.api.Test
import org.junit.jupiter.params.ParameterizedTest
import org.junit.jupiter.params.provider.MethodSource
import org.reactivestreams.Publisher
import java.nio.charset.Charset
import java.util.concurrent.TimeUnit


class InteractionsTest {

    @Test
    fun fireAndForget() {
        val expected = "test"
        testClient.fireAndForget(request(expected))
            .andThen(testClient.anotherFireAndForget(request(expected)))
            .timeout(5, TimeUnit.SECONDS)
            .blockingAwait()

        val fnfRequests = clientAcceptor
            .fnfRequests()
            .take(2)
            .toList()
            .timeout(5, TimeUnit.SECONDS)
            .blockingGet()
        fnfRequests.forEach { r -> Assertions.assertEquals(expectedMessage, r.message) }
    }

    @ParameterizedTest
    @MethodSource("requestResponseProvider")
    fun requestResponse(requestResponse: (Request) -> Single<Response>) {
        val expected = "test"
        val response = requestResponse(request(expected))
            .timeout(5, TimeUnit.SECONDS)
            .blockingGet()

        Assertions.assertEquals(expected, response.message)
    }

    @ParameterizedTest
    @MethodSource("requestStreamProvider")
    fun requestStream(requestStream: (Request) -> Flowable<Response>) {
        val response = requestStream(request(expectedMessage))
            .toList()
            .timeout(5, TimeUnit.SECONDS)
            .blockingGet()

        Assertions.assertEquals(expectedStreamCount, response.size)
        response.forEach { r -> Assertions.assertEquals(expectedMessage, r.message) }
    }

    @ParameterizedTest
    @MethodSource("streamRequestProvider")
    fun streamRequest(streamRequest: (Publisher<Request>) -> Single<Response>) {
        val response = streamRequest(
            streamRequest(
                expectedStreamCount,
                expectedMessage
            )
        )
            .timeout(5, TimeUnit.SECONDS)
            .blockingGet()

        Assertions.assertEquals(expectedMessage, response.message)
    }

    @ParameterizedTest
    @MethodSource("channelProvider")
    fun channel(requestResponse: (Publisher<Request>) -> Flowable<Response>) {
        val response = requestResponse(
            streamRequest(
                expectedStreamCount,
                expectedMessage
            )
        )
            .toList()
            .timeout(5, TimeUnit.SECONDS)
            .blockingGet()

        Assertions.assertEquals(expectedStreamCount, response.size)
        response.forEach { r -> Assertions.assertEquals(expectedMessage, r.message) }
    }

    @ParameterizedTest
    @MethodSource("defaultInteractionProvider")
    fun defaultInteractionIsError(interaction: (Flowable<Request>) -> Flowable<Response>) {
        val subs = TestSubscriber<Response>()
        interaction(Flowable.just(request("test")))
            .timeout(5, TimeUnit.SECONDS)
            .blockingSubscribe(subs)

        subs.assertValueCount(0)
        subs.assertError { e -> e is ApplicationException && "not implemented" == e.message }
    }

    @LongTest
    fun responseLong() {
        Flowable.interval(1, TimeUnit.MILLISECONDS)
            .take(30, TimeUnit.SECONDS)
            .onBackpressureDrop()
            .flatMapSingle {
                testClient
                    .requestResponse(
                        request(expectedMessage),
                        "foo".toByteBuf()
                    )
            }
            .rebatchRequests(128)
            .doOnNext { r -> Assertions.assertEquals(expectedMessage, r.message) }
            .timeout(5, TimeUnit.SECONDS)
            .lastOrError()
            .blockingGet()
    }

    @LongTest
    fun responseStreamLong() {
        Flowable.interval(1, TimeUnit.MILLISECONDS)
            .take(30, TimeUnit.SECONDS)
            .onBackpressureDrop()
            .flatMap {
                testClient
                    .requestStream(
                        request(expectedMessage),
                        "foo".toByteBuf()
                    )
            }
            .rebatchRequests(128)
            .doOnNext { r -> Assertions.assertEquals(expectedMessage, r.message) }
            .timeout(5, TimeUnit.SECONDS)
            .lastOrError()
            .blockingGet()
    }

    @LongTest
    fun requestStreamLong() {
        Flowable.interval(1, TimeUnit.MILLISECONDS)
            .take(30, TimeUnit.SECONDS)
            .onBackpressureDrop()
            .flatMapSingle {
                testClient
                    .streamRequest(
                        streamRequest(16, expectedMessage),
                        "foo".toByteBuf()
                    )
            }
            .rebatchRequests(128)
            .doOnNext { r -> Assertions.assertEquals(expectedMessage, r.message) }
            .timeout(5, TimeUnit.SECONDS)
            .lastOrError()
            .blockingGet()
    }

    @LongTest
    fun channelLong() {
        Flowable.interval(1, TimeUnit.MILLISECONDS)
            .take(30, TimeUnit.SECONDS)
            .onBackpressureDrop()
            .flatMap {
                testClient
                    .channel(
                        streamRequest(
                            expectedStreamCount,
                            expectedMessage
                        ), "foo".toByteBuf()
                    )
            }
            .rebatchRequests(128)
            .doOnNext { r -> Assertions.assertEquals(expectedMessage, r.message) }
            .timeout(5, TimeUnit.SECONDS)
            .lastOrError()
            .blockingGet()
    }

    private fun String.toByteBuf(): ByteBuf {
        return Unpooled.wrappedBuffer(this.toByteArray(Charset.defaultCharset()))
    }

    private fun request(msg: String): Request {
        return Request
            .newBuilder()
            .setMessage(msg)
            .build()
    }

    private fun streamRequest(count: Int, msg: String): Flowable<Request> {
        return Flowable.range(0, count).map { request(msg) }
    }

    companion object {
        const val expectedStreamCount = 5
        const val expectedMessage = "test"

        lateinit var testClient: TestServiceClient
        private lateinit var server: CloseableChannel
        internal lateinit var clientAcceptor: ClientAcceptor

        @JvmStatic
        fun requestResponseProvider(): List<(Request) -> Single<Response>> =
            listOf(testClient::requestResponse, testClient::anotherRequestResponse)

        @JvmStatic
        fun requestStreamProvider(): List<(Request) -> Flowable<Response>> =
            listOf(testClient::requestStream, testClient::anotherRequestStream)

        @JvmStatic
        fun streamRequestProvider(): List<(Publisher<Request>) -> Single<Response>> =
            listOf(testClient::streamRequest, testClient::anotherStreamRequest)

        @JvmStatic
        fun channelProvider(): List<(Publisher<Request>) -> Flowable<Response>> =
            listOf(testClient::channel, testClient::anotherChannel)

        @JvmStatic
        fun defaultInteractionProvider(): List<(Flowable<Request>) -> Flowable<Response>> =
            listOf(
                { flowable -> flowable.flatMapSingle { r -> testClient.defaultRequestResponse(r) } },
                { flowable -> flowable.flatMap { r -> testClient.defaultRequestStream(r) } },
                { flowable -> testClient.defaultStreamRequest(flowable).toFlowable() },
                { flowable -> testClient.defaultChannel(flowable) }
            )

        @BeforeAll
        @JvmStatic
        internal fun setUp() {
            ResourceLeakDetector.setLevel(ResourceLeakDetector.Level.ADVANCED)

            server = RSocketFactory.receive()
                .acceptor {
                    { _, rSocket ->
                        Single.just(
                            TestServiceServer(
                                ServerAcceptor(expectedStreamCount, TestServiceClient(rSocket))
                            )
                        )
                    }
                }.transport(InternalWebsocketServerTransport.create("localhost", 0)).start()
                .timeout(5, TimeUnit.SECONDS)
                .blockingGet()

            val address = server.address()

            val url = HttpUrl.Builder()
                .scheme("http")
                .host(address.hostName)
                .port(address.port)
                .build()

            clientAcceptor = ClientAcceptor()
            val rSocket = RSocketFactory
                .connect()
                .keepAlive { opts ->
                    with(opts) {
                        keepAliveInterval(Duration.ofSeconds(1))
                        keepAliveMaxLifeTime(Duration.ofMinutes(2))
                    }
                }
                .acceptor { { RequestHandlingRSocket(TestServiceServer(clientAcceptor)) } }
                .transport(OkhttpWebsocketClientTransport.create(url))
                .start().timeout(5, TimeUnit.SECONDS)
                .blockingGet()

            testClient = TestServiceClient(rSocket)
        }

        @AfterAll
        @JvmStatic
        internal fun tearDown() {
            server
                .close()
                .blockingAwait(5, TimeUnit.SECONDS)
        }
    }
}

internal class ClientAcceptor : TestService {

    private val fnfProcessor = ReplayProcessor.create<Request>()

    fun fnfRequests(): Flowable<Request> = fnfProcessor

    override fun fireAndForget(message: Request, metadata: ByteBuf): Completable {
        fnfProcessor.onNext(message)
        return Completable.complete()
    }

    override fun anotherFireAndForget(message: Request, metadata: ByteBuf): Completable =
        fireAndForget(message, metadata)
}

private class ServerAcceptor(
    private val streamResponseCount: Int,
    private val serviceClient: TestServiceClient
) : TestService {
    override fun anotherFireAndForget(message: Request, metadata: ByteBuf): Completable =
        fireAndForget(message, metadata)

    override fun fireAndForget(message: Request, metadata: ByteBuf): Completable =
        serviceClient.fireAndForget(message)

    override fun anotherRequestResponse(message: Request, metadata: ByteBuf): Single<Response> =
        requestResponse(message, metadata)

    override fun anotherRequestStream(message: Request, metadata: ByteBuf): Flowable<Response> =
        requestStream(message, metadata)

    override fun anotherStreamRequest(message: Publisher<Request>, metadata: ByteBuf): Single<Response> =
        streamRequest(message, metadata)

    override fun anotherChannel(message: Publisher<Request>, metadata: ByteBuf): Flowable<Response> =
        channel(message, metadata)

    override fun requestResponse(
        message: Request,
        metadata: ByteBuf
    ): Single<Response> =
        Single.just(response(message))

    override fun requestStream(
        message: Request,
        metadata: ByteBuf
    ): Flowable<Response> =
        response(message, streamResponseCount)

    override fun streamRequest(
        message: Publisher<Request>,
        metadata: ByteBuf
    ): Single<Response> =
        Flowable
            .fromPublisher(message)
            .lastOrError()
            .map { response(it) }

    override fun channel(
        message: Publisher<Request>,
        metadata: ByteBuf
    ): Flowable<Response> =
        Flowable
            .fromPublisher(message)
            .map { response(it) }

    private fun response(request: Request, count: Int) =
        Flowable.range(0, count)
            .map { response(request) }

    private fun response(request: Request) =
        Response
            .newBuilder()
            .setMessage(request.message)
            .build()
}