package io.rsocket.rpc.kotlin.example

import io.netty.buffer.ByteBuf
import io.reactivex.Completable
import io.reactivex.Flowable
import io.reactivex.Single
import io.rsocket.kotlin.RSocketFactory
import io.rsocket.kotlin.transport.netty.server.WebsocketServerTransport
import io.rsocket.rpc.kotlin.example.protos.*
import io.rsocket.rpc.kotlin.rsocket.RequestHandlingRSocket
import io.rsocket.transport.okhttp.client.OkhttpWebsocketClientTransport
import okhttp3.HttpUrl
import org.reactivestreams.Publisher
import org.slf4j.LoggerFactory
import java.net.InetSocketAddress
import java.util.concurrent.TimeUnit

class RpcExample {

    companion object {

        @JvmStatic
        fun main(args: Array<String>) {

            val context = RSocketFactory.receive()
                .acceptor {
                    { _, rSocket ->
                        /*RSocket is symmetric so both client and server have can have Client(requester) to make requests*/
                        Single.just(
                            ExampleServiceServer(ServerAcceptor(ExampleServiceClient(rSocket)))
                        )
                    }
                    /*transport is pluggable*/
                }.transport(WebsocketServerTransport.create("localhost", 0))
                .start()
                .timeout(5, TimeUnit.SECONDS)
                .blockingGet()

            val url = createUrl(context.address())

            val rSocket = RSocketFactory
                .connect()
                /*RSocket is symmetric so both client and server can have Server(responder) to respond to requests*/
                .acceptor { { RequestHandlingRSocket(ExampleServiceServer(ClientAcceptor())) } }
                /*transport is pluggable */
                .transport(OkhttpWebsocketClientTransport.create(url))
                .start()
                .timeout(5, TimeUnit.SECONDS)
                .blockingGet()

            /*Requester of client side of connection*/
            val exampleClient = ExampleServiceClient(rSocket)

            val fnf =
                Flowable.interval(7, TimeUnit.SECONDS)
                    .onBackpressureDrop()
                    .flatMapCompletable { exampleClient.fireAndForget(request("client fire-and-forget")) }

            val requestResponse =
                Flowable.interval(3, TimeUnit.SECONDS)
                    .onBackpressureDrop()
                    .flatMapSingle { exampleClient.requestResponse(request("client: request-response")) }

            val serverStream =
                exampleClient.requestStream(request("client: server stream request"))

            val clientStream =
                exampleClient.streamRequest(
                    Flowable.interval(10, TimeUnit.SECONDS)
                        .onBackpressureDrop()
                        .map { request("client: client stream request") }
                ).toFlowable()

            val channel =
                exampleClient.channel(
                    Flowable.interval(10, TimeUnit.SECONDS)
                        .onBackpressureDrop()
                        .map { request("client: channel request") })

            Flowable.merge(Flowable.just(fnf.toFlowable(), requestResponse, serverStream, clientStream, channel))
                .ignoreElements()
                .blockingAwait()
        }

        private fun createUrl(address: InetSocketAddress): HttpUrl =
            HttpUrl.Builder()
                .scheme("http")
                .host(address.hostName)
                .port(address.port)
                .build()
    }

    /*Request acceptor of server side of connection. It can initiate requests to client side acceptor
    with provided ExampleServiceClient*/
    class ServerAcceptor(private val exampleService: ExampleServiceClient) : ExampleService {
        companion object {
            val logger = LoggerFactory.getLogger(ServerAcceptor::class.java)
        }

        override fun requestResponse(message: Request, metadata: ByteBuf): Single<Response> {
            return Single.defer {
                logger.info("Server acceptor received request response message")
                Single.just(response("server response"))
            }
        }

        override fun requestStream(message: Request, metadata: ByteBuf): Flowable<Response> =
            Flowable.interval(4, TimeUnit.SECONDS)
                .onBackpressureDrop()
                .map { response("server stream response") }

        override fun streamRequest(message: Publisher<Request>, metadata: ByteBuf): Single<Response> =
            Flowable.fromPublisher(message)
                .doOnNext { m -> logger.info("Server acceptor received client streaming message") }
                .lastOrError()
                .map { m -> Response.newBuilder().setMessage(m.message).build() }

        override fun channel(message: Publisher<Request>, metadata: ByteBuf): Flowable<Response> =
            Flowable.fromPublisher(message)
                .doOnNext { m -> logger.info("Server acceptor received channel message") }
                .map { m -> response(m.message) }

        override fun fireAndForget(message: Request, metadata: ByteBuf): Completable =
            Completable.defer {
                logger.info("Server acceptor received fire-and-forget message")
                exampleService.fireAndForget(request("Server acceptor fire-and-forget"))
            }
    }


    class ClientAcceptor : ExampleService {
        companion object {
            val logger = LoggerFactory.getLogger(ServerAcceptor::class.java)
        }

        override fun fireAndForget(
            message: Request,
            metadata: ByteBuf
        ): Completable =
            Completable.complete()
                .doOnSubscribe { logger.info("Client acceptor received fire-and-forget message") }
    }
}

private fun response(msg: String) = Response
    .newBuilder()
    .setMessage(msg)
    .build()

private fun request(msg: String) = Request
    .newBuilder()
    .setMessage(msg)
    .build()