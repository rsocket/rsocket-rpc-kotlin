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

package io.rsocket.rpc.kotlin.tracing

import io.opentracing.Span
import io.opentracing.SpanContext
import io.opentracing.Tracer
import io.opentracing.propagation.Format
import io.opentracing.propagation.TextMapInjectAdapter
import io.reactivex.FlowableSubscriber
import org.reactivestreams.Subscriber
import org.reactivestreams.Subscription
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean

class TraceSubscriber<T> : AtomicBoolean, FlowableSubscriber<T>, Subscription {
    private val span: Span
    private val rootSpan: Span?
    private val tracer: Tracer
    private val downstream: Subscriber<in T>
    internal var upstream: Subscription? = null

    constructor(
        downstream: Subscriber<in T>,
        tracer: Tracer,
        tracingMetadata: Map<String, String>?,
        spanContext: SpanContext,
        name: String,
        vararg tags: Tag
    ) {
        this.downstream = downstream
        this.tracer = tracer
        this.rootSpan = null

        val spanBuilder = this.tracer.buildSpan(name).asChildOf(spanContext)
        if (tags.isNotEmpty()) {
            for (tag in tags) {
                spanBuilder.withTag(tag.key, tag.value)
            }
        }
        this.span = spanBuilder.start()

        if (tracingMetadata != null) {
            val adapter = TextMapInjectAdapter(tracingMetadata)
            tracer.inject(span.context(), Format.Builtin.TEXT_MAP, adapter)
        }
    }

    constructor(
        downstream: Subscriber<in T>,
        tracer: Tracer,
        tracingMetadata: Map<String, String>?,
        name: String,
        vararg tags: Tag
    ) {

        this.downstream = downstream
        this.tracer = tracer
        val root = this.tracer.activeSpan()
        this.rootSpan = root

        val spanBuilder = this.tracer.buildSpan(name)
        if (tags.isNotEmpty()) {
            for (tag in tags) {
                spanBuilder.withTag(tag.key, tag.value)
            }
        }

        if (root != null) {
            spanBuilder.asChildOf(root)
        }

        this.span = spanBuilder.start()

        if (tracingMetadata != null) {
            val adapter = TextMapInjectAdapter(tracingMetadata)
            tracer.inject(span.context(), Format.Builtin.TEXT_MAP, adapter)
        }
    }

    override fun onSubscribe(s: Subscription) {
        if (upstream != null) {
            s.cancel()
        } else {
            upstream = s
            downstream.onSubscribe(this)
            this.tracer.scopeManager().activate(span, false).use { scope ->
                scope.span().log(TimeUnit.MILLISECONDS.toMicros(System.currentTimeMillis()), "onSubscribe")
                downstream.onSubscribe(this)
            }
        }
    }

    override fun onNext(t: T) {
        downstream.onNext(t)
    }

    override fun onError(t: Throwable) {
        try {
            this.tracer.scopeManager().activate(span, false).use { scope ->
                scope.span().log(TimeUnit.MILLISECONDS.toMicros(System.currentTimeMillis()), "onError")
            }
        } finally {
            cleanup()
        }
    }

    override fun onComplete() {
        try {
            this.tracer.scopeManager().activate(span, false).use { scope ->
                scope.span().log(TimeUnit.MILLISECONDS.toMicros(System.currentTimeMillis()), "onComplete")
                downstream.onComplete()
            }
        } finally {
            cleanup()
        }
    }

    override fun request(n: Long) {
        this.upstream!!.request(n)
    }

    override fun cancel() {
        try {
            this.tracer.scopeManager().activate(span, false).use { scope ->
                scope.span().log(TimeUnit.MILLISECONDS.toMicros(System.currentTimeMillis()), "cancel")
                this.upstream!!.cancel()
            }
        } finally {
            cleanup()
        }
    }

    internal fun cleanup() {
        if (compareAndSet(false, true)) {
            span.finish()
            rootSpan?.finish()
        }
    }
}
