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

package io.rsocket.rpc.kotlin.util

import io.reactivex.Flowable
import io.reactivex.processors.UnicastProcessor
import io.rsocket.kotlin.Payload
import org.reactivestreams.Publisher

class StreamSplitter {

    companion object {

        @JvmStatic
        fun split(p: Publisher<Payload>): Flowable<Split> {
            var first = true
            val rest = UnicastProcessor.create<Payload>()
            val channelArg = UnicastProcessor.create<Split>()

            return Flowable.fromPublisher(p)
                    .doOnComplete { rest.onComplete() }
                    .doOnError { rest.onError(it) }
                    .flatMap { payload ->
                        if (first) {
                            first = false
                            channelArg.onNext(Split(payload, rest))
                            channelArg.onComplete()
                            channelArg
                        } else {
                            rest.onNext(payload)
                            Flowable.empty<Split>()
                        }
                    }
        }
    }

    data class Split(val head: Payload, val tail: Flowable<Payload>)
}