![Reactive Foundation](https://raw.githubusercontent.com/reactivefoundation/artwork/master/rf/SVG/reactivefoundation-stacked-color.svg) ![RSocket](https://raw.githubusercontent.com/rsocket/rsocket-artwork/master/rsocket-horizontal-logo/SVG/r-socket-horizontal-pink.svg)

# RSocket RPC - Kotlin & RxJava2
[![Build Status](https://travis-ci.com/rsocket/rsocket-rpc-kotlin.svg?branch=develop)](https://travis-ci.com/rsocket/rsocket-rpc-kotlin)

The standard [RSocket](http://rsocket.io) RPC implementation based on [RSocket-kotlin](https://github.com/rsocket/rsocket-kotlin), [RxJava2](https://github.com/ReactiveX/RxJava) and [Protocol Buffers](https://github.com/protocolbuffers/protobuf).  
[RSocket](https://github.com/rsocket/rsocket) is binary application protocol with pluggable transports which models all communication as [Reactive Streams](https://github.com/reactive-streams/reactive-streams-jvm/blob/master/README.md) of messages multiplexed over a single network connection, and never synchronously blocks while waiting for a response.

## Build and binaries

Releases

```groovy
    repositories {
        maven { url 'https://oss.jfrog.org/libs-release' }
    }
```

```groovy
    dependencies {
        compile 'io.rsocket.rpc.kotlin:rsocket-rpc-core:0.2.13'
    }
```

Snapshots

```groovy
    repositories {
        maven { url 'https://oss.jfrog.org/libs-snapshot' }
    }
```

```groovy
    dependencies {
        compile 'io.rsocket.rpc.kotlin:rsocket-rpc-core:0.2.13-SNAPSHOT'
    }
```

## Getting started  

### Prerequisites

* JDK7

* [Protobuf](https://github.com/google/protobuf) compiler. RSocket RPC requires Protobuf 3.6.x or higher.

  Mac users can install the Protobuf compiler using Homebrew:

        $ brew install protobuf  

  For other operating systems Protobuf compiler can be installed from pre-built packages hosted on the [Protobuf Releases](https://github.com/google/protobuf/releases)  

### Services definition

RPC relies on languages agnostic, Protocol Buffers based service definitions (IDL).  
IDL defines service (and its data types) as set of methods which correspond to RSocket protocol interactions:  

```
message Request {
    string message = 1;
}

message Response {
    string message = 1;
}

service ExampleService {

    rpc RequestResponse (Request) returns (Response) {}

    rpc RequestStream (Request) returns (stream Response) {}

    rpc StreamRequest (stream Request) returns (Response) {}

    rpc Channel (stream Request) returns (stream Response) {}

    rpc FireAndForget(Request) returns (google.protobuf.Empty) {
       option (options).fire_and_forget = true;
    }
}
```

### Compilation

RSocket RPC-Kotlin compiler generates `Service` interfaces, `Clients` and `Servers` for available IDLs.  
For gradle, RPC compiler can be configured as follows (check [example](https://github.com/rsocket/rsocket-rpc-kotlin/blob/develop/example/build.gradle) for full reference):  
```
protobuf {
    generatedFilesBaseDir = "${projectDir}/src/generated"

    protoc {
        artifact = "com.google.protobuf:protoc"
    }
    plugins {
        rsocketRpcKotlin {
            path = kotlinPluginPath
        }
    }
    generateProtoTasks {
        all().each { task ->
            task.dependsOn ':rsocket-rpc-protobuf:kotlin_pluginExecutable'
            // Recompile protos when the codegen has been changed
            task.inputs.file kotlinPluginPath
            // Recompile protos when build.gradle has been changed, because
            // it's possible the version of protoc has been changed.
            task.inputs.file "${rootProject.projectDir}/build.gradle"
            task.plugins {
                rsocketRpcKotlin {}
            }
        }
    }
}
```

Client and server can be constructed based on generated clients & servers, and RSocket-Kotlin implementation:  
```
val server = RSocketFactory.receive()
                .acceptor {
                    { _, rSocket ->
                        /*RSocket is symmetric so both client and server have can have Client(requester) to make requests*/
                        Single.just(
                            ExampleServiceServer(ServerAcceptor())
                        )
                    }
                    /*transport is pluggable*/
                }.transport(InternalWebsocketServerTransport.create("localhost", 0))
                .start()
                .timeout(5, TimeUnit.SECONDS)
                .blockingGet()
```

Here, `ServerAcceptor` implements generated `ExampleService`, and is wired using generated `ExampleServiceServer`  

Client is constructed in similar fashion:  
```
val rSocket = RSocketFactory
                .connect()
                /*transport is pluggable */
                .transport(OkhttpWebsocketClientTransport.create(url))
                .start()
                .timeout(5, TimeUnit.SECONDS)
                .blockingGet()

            /*Requester of client side of connection*/
            val exampleClient = ExampleServiceClient(rSocket)
```

Generated `ExampleServiceClient` accepts `RSocket`.    

RSocket is symmetric, both RPC client and server can have requesters and responders, RPC support for this is available in   
[examples](https://github.com/rsocket/rsocket-rpc-kotlin/tree/develop/example)    
  
## Development  

1. Run the following Gradle command to build the project:

        $ ./gradlew clean build
        
## Bugs and Feedback

For bugs, questions, and discussions please use the [Github Issues](https://github.com/rsocket/rsocket-rpc-kotlin/issues).

## License
Copyright 2019 [Netifi Inc.](https://www.netifi.com)

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
