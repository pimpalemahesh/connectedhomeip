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
package com.matter.camera.controller

import android.content.Context
import android.util.Log
import chip.devicecontroller.ChipDeviceController
import chip.devicecontroller.ControllerParams
import chip.devicecontroller.GetConnectedDeviceCallbackJni.GetConnectedDeviceCallback
import chip.platform.AndroidBleManager
import chip.platform.AndroidChipPlatform
import chip.platform.AndroidNfcCommissioningManager
import chip.platform.ChipMdnsCallbackImpl
import chip.platform.DiagnosticDataProviderImpl
import chip.platform.NsdManagerServiceBrowser
import chip.platform.NsdManagerServiceResolver
import chip.platform.PreferencesConfigurationManager
import chip.platform.PreferencesKeyValueStoreManager
import kotlin.coroutines.resume
import kotlin.coroutines.resumeWithException
import kotlinx.coroutines.suspendCancellableCoroutine

/**
 * Singleton managing the Matter/CHIP SDK initialization and the DeviceController instance.
 * Mirrors the pattern used by CHIPTool but tailored for camera controller use.
 */
object ChipClient {
    private const val TAG = "ChipClient"
    private lateinit var chipDeviceController: ChipDeviceController
    private lateinit var androidPlatform: AndroidChipPlatform

    // 0xFFF4 is a test vendor ID; replace with your assigned company ID for production
    const val VENDOR_ID = 0xFFF4

    fun getDeviceController(context: Context): ChipDeviceController {
        getAndroidChipPlatform(context)

        if (!this::chipDeviceController.isInitialized) {
            chipDeviceController =
                ChipDeviceController(
                    ControllerParams.newBuilder()
                        .setControllerVendorId(VENDOR_ID)
                        .setEnableServerInteractions(true)
                        .build()
                )
        }

        return chipDeviceController
    }

    fun getAndroidChipPlatform(context: Context?): AndroidChipPlatform {
        if (!this::androidPlatform.isInitialized && context != null) {
            ChipDeviceController.loadJni()
            androidPlatform =
                AndroidChipPlatform(
                    AndroidBleManager(context),
                    AndroidNfcCommissioningManager(),
                    PreferencesKeyValueStoreManager(context),
                    PreferencesConfigurationManager(context),
                    NsdManagerServiceResolver(
                        context,
                        NsdManagerServiceResolver.NsdManagerResolverAvailState()
                    ),
                    NsdManagerServiceBrowser(context),
                    ChipMdnsCallbackImpl(),
                    DiagnosticDataProviderImpl(context)
                )
        }
        return androidPlatform
    }

    /**
     * Returns a connected device pointer for the given node, suitable for sending commands.
     */
    suspend fun getConnectedDevicePointer(context: Context, nodeId: Long): Long {
        return suspendCancellableCoroutine { continuation ->
            getDeviceController(context)
                .getConnectedDevicePointer(
                    nodeId,
                    object : GetConnectedDeviceCallback {
                        override fun onDeviceConnected(devicePointer: Long) {
                            Log.d(TAG, "Connected to device pointer for node $nodeId")
                            continuation.resume(devicePointer)
                        }

                        override fun onConnectionFailure(nodeId: Long, error: Exception) {
                            Log.e(TAG, "Connection failed for node $nodeId", error)
                            continuation.resumeWithException(
                                IllegalStateException("Unable to connect to node $nodeId", error)
                            )
                        }
                    }
                )
        }
    }
}
