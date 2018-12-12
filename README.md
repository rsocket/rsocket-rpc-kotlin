# RSocket RPC - Kotlin
[![Build Status](https://travis-ci.org/rsocket/rsocket-rpc-kotlin.svg?branch=master)](https://travis-ci.org/rsocket/rsocket-rpc-kotlin)

The standard [RSocket](http://rsocket.io) RPC implementation based on [RSocket-kotlin](https://github.com/rsocket/rsocket-kotlin) and [RxJava2](https://github.com/ReactiveX/RxJava).  
RSocket is binary application protocol with pluggable transports which models all communication as [Reactive Streams](https://github.com/reactive-streams/reactive-streams-jvm/blob/master/README.md) of messages multiplexed over a single network connection, and never synchronously blocks while waiting for a response.
## Build and binaries

Snapshots are available on Bintray

```groovy
    repositories {
        maven { url 'https://oss.jfrog.org/libs-snapshot' }
    }
```

```groovy
    dependencies {
        compile 'io.rsocket.rpc.kotlin:rsocket-rpc-core:<TBD>'
    }
```

## Getting started  
TBD

## Development  

1. Building RSocket-RPC-kotlin requires installation of the [Protobuf](https://github.com/google/protobuf) compiler. RSocket RPC requires Protobuf 3.6.x or higher.

    For Mac users you can easily install the Protobuf compiler using Homebrew:

        $ brew install protobuf

    For other operating systems you can install the Protobuf compiler using the pre-built packages hosted on the [Protobuf Releases](https://github.com/google/protobuf/releases) page.

2. Run the following Gradle command to build the project:

        $ ./gradlew clean build
        
## Release Notes

Release notes are available at [https://github.com/rsocket/rsocket-rpc-kotlin/releases](https://github.com/rsocket/rsocket-rpc-kotlin/releases).

## Bugs and Feedback

For bugs, questions, and discussions please use the [Github Issues](https://github.com/rsocket/rsocket-rpc-kotlin/issues).

## License
Copyright 2018 [Netifi Inc.](https://www.netifi.com)

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
