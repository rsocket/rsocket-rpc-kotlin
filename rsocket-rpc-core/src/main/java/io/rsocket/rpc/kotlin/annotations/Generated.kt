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

package io.rsocket.rpc.kotlin.annotations

import kotlin.reflect.KClass

/**
 * Annotation that identifies proteus generated services and stores metadata that can
 * be used by dependency injection frameworks and custom annotation processors.
 */
@Annotation
@Target(AnnotationTarget.CLASS, AnnotationTarget.FILE)
@Retention(AnnotationRetention.RUNTIME)
annotation class Generated(
    /**
     * Type of the generated Proteus resource.
     *
     * @return type of generated resource
     */
    val type: ResourceType,
    /**
     * Class of the RSocketRpc service hosted by the annotated class.
     *
     * @return RSocketRpc service class
     */
    val idlClass: KClass<*>
)
