/*
 *   Copyright (c) 2025 Project CHIP Authors
 *   All rights reserved.
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 */
package com.matter.camera.controller.devices

data class CameraDevice(
    val nodeId: Long,
    val name: String = "Camera Device",
    val setupCode: Long = 0,
    val discriminator: Int = 0,
    var isConnected: Boolean = false,
    var activeStreamId: Int = -1
) {
    val displayNodeId: String
        get() = "0x${nodeId.toString(16).uppercase()}"

    val isStreaming: Boolean
        get() = activeStreamId >= 0
}
