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

package io.rsocket.rpc.kotlin.frames

import io.netty.buffer.ByteBuf
import io.netty.buffer.ByteBufAllocator
import io.netty.buffer.ByteBufUtil
import io.netty.buffer.Unpooled
import java.nio.charset.StandardCharsets

object Metadata {
    // Version
    const val VERSION: Short = 1
    private const val SHORT_BYTES = Short.SIZE_BYTES

    @JvmStatic
    fun encode(
            allocator: ByteBufAllocator,
            service: String,
            method: String,
            metadata: ByteBuf): ByteBuf {
        return encode(allocator, service, method, Unpooled.EMPTY_BUFFER, metadata)
    }

    @JvmStatic
    fun encode(
            allocator: ByteBufAllocator,
            service: String,
            method: String,
            tracing: ByteBuf,
            metadata: ByteBuf): ByteBuf {
        val byteBuf = allocator.buffer().writeShort(VERSION.toInt())

        val serviceLength = requireUnsignedShort(ByteBufUtil.utf8Bytes(service))
        byteBuf.writeShort(serviceLength)
        ByteBufUtil.reserveAndWriteUtf8(byteBuf, service, serviceLength)

        val methodLength = requireUnsignedShort(ByteBufUtil.utf8Bytes(method))
        byteBuf.writeShort(methodLength)
        ByteBufUtil.reserveAndWriteUtf8(byteBuf, method, methodLength)

        byteBuf.writeShort(tracing.readableBytes())
        byteBuf.writeBytes(tracing)

        byteBuf.writeBytes(metadata, metadata.readerIndex(), metadata.readableBytes())

        return byteBuf
    }

    @JvmStatic
    fun getVersion(byteBuf: ByteBuf): Int {
        return byteBuf.getShort(0).toInt() and 0x7FFF
    }

    @JvmStatic
    fun getService(byteBuf: ByteBuf): String {
        var offset = SHORT_BYTES

        val serviceLength = byteBuf.getShort(offset).toInt()
        offset += SHORT_BYTES

        return byteBuf.toString(offset, serviceLength, StandardCharsets.UTF_8)
    }

    @JvmStatic
    fun getMethod(byteBuf: ByteBuf): String {
        var offset = SHORT_BYTES

        val serviceLength = byteBuf.getShort(offset).toInt()
        offset += SHORT_BYTES + serviceLength

        val methodLength = byteBuf.getShort(offset).toInt()
        offset += SHORT_BYTES

        return byteBuf.toString(offset, methodLength, StandardCharsets.UTF_8)
    }

    @JvmStatic
    fun getTracing(byteBuf: ByteBuf): ByteBuf {
        var offset = SHORT_BYTES

        val serviceLength = byteBuf.getShort(offset).toInt()
        offset += SHORT_BYTES + serviceLength

        val methodLength = byteBuf.getShort(offset).toInt()
        offset += SHORT_BYTES + methodLength

        val tracingLength = byteBuf.getShort(offset).toInt()
        offset += SHORT_BYTES

        return if (tracingLength > 0) byteBuf.slice(offset, tracingLength) else Unpooled.EMPTY_BUFFER
    }

    @JvmStatic
    fun getMetadata(byteBuf: ByteBuf): ByteBuf {
        var offset = SHORT_BYTES

        val serviceLength = byteBuf.getShort(offset).toInt()
        offset += SHORT_BYTES + serviceLength

        val methodLength = byteBuf.getShort(offset).toInt()
        offset += SHORT_BYTES + methodLength

        val tracingLength = byteBuf.getShort(offset).toInt()
        offset += SHORT_BYTES + tracingLength

        val metadataLength = byteBuf.readableBytes() - offset
        return if (metadataLength > 0) byteBuf.slice(offset, metadataLength) else Unpooled.EMPTY_BUFFER
    }

    private const val UNSIGNED_SHORT_SIZE = 16
    private const val UNSIGNED_SHORT_MAX_VALUE = (1 shl UNSIGNED_SHORT_SIZE) - 1

    private fun requireUnsignedShort(i: Int): Int {
        if (i > UNSIGNED_SHORT_MAX_VALUE) {
            throw IllegalArgumentException(
                    String.format("%d is larger than %d bits", i, UNSIGNED_SHORT_SIZE))
        }

        return i
    }
}
