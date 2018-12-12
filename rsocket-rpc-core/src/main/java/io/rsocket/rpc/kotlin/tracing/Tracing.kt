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

import io.netty.buffer.ByteBuf
import io.netty.buffer.ByteBufAllocator
import io.netty.buffer.ByteBufUtil
import io.netty.buffer.Unpooled
import io.opentracing.SpanContext
import io.opentracing.Tracer
import io.opentracing.propagation.Format
import io.opentracing.propagation.TextMapExtractAdapter
import io.reactivex.FlowableTransformer
import io.rsocket.rpc.kotlin.frames.Metadata
import java.nio.charset.StandardCharsets
import java.util.*

object Tracing {
    private const val UNSIGNED_SHORT_SIZE = 16
    private const val UNSIGNED_SHORT_MAX_VALUE = (1 shl UNSIGNED_SHORT_SIZE) - 1

    fun deserializeTracingMetadata(tracer: Tracer?, metadata: ByteBuf): SpanContext? {
        if (tracer == null) {
            return null
        }

        val tracing = Metadata.getTracing(metadata)

        if (tracing.readableBytes() < 0) {
            return null
        }

        val metadataMap = byteBufToMap(tracing)
        return if (metadataMap.isEmpty()) {
            null
        } else deserializeTracingMetadata(tracer, metadataMap)

    }

    fun mapToByteBuf(allocator: ByteBufAllocator, map: Map<String, String>?): ByteBuf {
        if (map == null || map.isEmpty()) {
            return Unpooled.EMPTY_BUFFER
        }

        val byteBuf = allocator.buffer()

        for ((key, value) in map) {
            val keyLength = requireUnsignedShort(ByteBufUtil.utf8Bytes(key))
            byteBuf.writeShort(keyLength)
            ByteBufUtil.reserveAndWriteUtf8(byteBuf, key, keyLength)

            val valueLength = requireUnsignedShort(ByteBufUtil.utf8Bytes(value))
            byteBuf.writeShort(valueLength)
            ByteBufUtil.reserveAndWriteUtf8(byteBuf, value, keyLength)
        }

        return byteBuf
    }

    fun <T> trace(): (Map<String, String>) -> FlowableTransformer<T, T> =
        { FlowableTransformer { upstream -> upstream } }

    fun <T> traceAsChild(): (SpanContext?) -> FlowableTransformer<T, T> =
        { FlowableTransformer { upstream -> upstream } }

    fun <T> trace(
        tracer: Tracer, name: String, vararg tags: Tag
    ): (Map<String, String>) -> FlowableTransformer<T, T> =
        { map ->
            FlowableTransformer { upstream ->
                upstream.lift(
                    TraceOperator(
                        tracer,
                        map,
                        name,
                        *tags
                    )
                )
            }
        }

    fun <T> traceAsChild(tracer: Tracer, name: String, vararg tags: Tag)
            : (SpanContext?) -> FlowableTransformer<T, T> =
        { spanContext ->
            FlowableTransformer { upstream ->
                upstream.lift(
                    if (spanContext == null)
                        TraceOperator(tracer, null, name, *tags)
                    else
                        TraceOperator(tracer, null, spanContext, name, tags)
                )
            }
        }

    private fun deserializeTracingMetadata(
        tracer: Tracer, metadata: Map<String, String>
    ): SpanContext {
        val adapter = TextMapExtractAdapter(metadata)
        return tracer.extract(Format.Builtin.TEXT_MAP, adapter)
    }

    private fun requireUnsignedShort(i: Int): Int {
        if (i > UNSIGNED_SHORT_MAX_VALUE) {
            throw IllegalArgumentException(
                String.format("%d is larger than %d bits", i, UNSIGNED_SHORT_SIZE)
            )
        }
        return i
    }

    private fun byteBufToMap(byteBuf: ByteBuf): Map<String, String> {
        val map = HashMap<String, String>()

        while (byteBuf.readableBytes() > 0) {
            val keyLength = byteBuf.readShort().toInt()
            val key = byteBuf.readCharSequence(keyLength, StandardCharsets.UTF_8) as String

            val valueLength = byteBuf.readShort().toInt()
            val value = byteBuf.readCharSequence(valueLength, StandardCharsets.UTF_8) as String

            map[key] = value
        }
        return map
    }
}
