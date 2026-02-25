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
package com.matter.camera.controller.streaming

import android.content.Context
import android.util.Log
import chip.devicecontroller.ChipClusters
import chip.devicecontroller.ChipStructs
import com.matter.camera.controller.ChipClient
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.util.Optional

/**
 * Manages camera AV stream allocation/deallocation via Matter cluster commands.
 * Android equivalent of the C++ AVStreamManagement + DeviceManager classes.
 */
class CameraStreamManager(private val context: Context) {

    companion object {
        private const val TAG = "CameraStreamManager"
        private const val CAMERA_ENDPOINT_ID = 1

        // StreamUsageEnum values from the Matter spec
        const val STREAM_USAGE_LIVE_VIEW = 3
        const val STREAM_USAGE_RECORDING = 1

        // VideoCodecEnum: 0 = H.264
        private const val VIDEO_CODEC_H264 = 0
    }

    interface StreamCallback {
        fun onStreamAllocated(videoStreamId: Int)
        fun onStreamDeallocated()
        fun onError(error: String)
    }

    /**
     * Sends a VideoStreamAllocate command to the given camera node.
     */
    suspend fun allocateVideoStream(
        nodeId: Long,
        streamUsage: Int = STREAM_USAGE_LIVE_VIEW,
        minWidth: Int? = null,
        minHeight: Int? = null,
        minFrameRate: Int? = null,
        minBitRate: Long? = null,
        callback: StreamCallback
    ) = withContext(Dispatchers.Main) {
        try {
            val devicePtr = ChipClient.getConnectedDevicePointer(context, nodeId)
            val cluster = ChipClusters.CameraAvStreamManagementCluster(devicePtr, CAMERA_ENDPOINT_ID)

            val width = minWidth ?: 640
            val height = minHeight ?: 480
            val minRes = ChipStructs.CameraAvStreamManagementClusterVideoResolutionStruct(width, height)
            val maxRes = ChipStructs.CameraAvStreamManagementClusterVideoResolutionStruct(width, height)
            val frameRate = minFrameRate ?: 30

            // Clamp bitrate to at least the value known to work with chip-tool.
            val requestedBitRate = minBitRate ?: 10_000L
            val bitRate = if (requestedBitRate < 10_000L) 10_000L else requestedBitRate
            val keyFrameInterval = 4000

            Log.i(
                TAG,
                "VideoStreamAllocate command: nodeId=$nodeId, usage=$streamUsage, " +
                    "codec=$VIDEO_CODEC_H264, frameRate=$frameRate, width=$width, height=$height, " +
                    "bitRate=$bitRate, keyFrameInterval=$keyFrameInterval"
            )

            cluster.videoStreamAllocate(
                object : ChipClusters.CameraAvStreamManagementCluster.VideoStreamAllocateResponseCallback {
                    override fun onSuccess(videoStreamID: Int) {
                        Log.i(TAG, "VideoStreamAllocate succeeded: streamId=$videoStreamID")
                        callback.onStreamAllocated(videoStreamID)
                    }

                    override fun onError(error: Exception) {
                        Log.e(TAG, "VideoStreamAllocate failed: ${error.message}", error)
                        callback.onError(error.message ?: "Unknown error")
                    }
                },
                streamUsage,
                VIDEO_CODEC_H264,
                frameRate,
                frameRate,
                minRes,
                maxRes,
                bitRate,
                bitRate,
                keyFrameInterval,
                Optional.of(true),
                Optional.of(true)
            )
        } catch (e: Exception) {
            Log.e(TAG, "Failed to allocate video stream", e)
            callback.onError(e.message ?: "Failed to connect to device")
        }
    }

    /**
     * Sends a VideoStreamDeallocate command to the given camera node.
     */
    suspend fun deallocateVideoStream(
        nodeId: Long,
        videoStreamId: Int,
        callback: StreamCallback
    ) = withContext(Dispatchers.Main) {
        try {
            Log.i(TAG, "Deallocating video stream $videoStreamId on node $nodeId")

            val devicePtr = ChipClient.getConnectedDevicePointer(context, nodeId)
            val cluster = ChipClusters.CameraAvStreamManagementCluster(devicePtr, CAMERA_ENDPOINT_ID)

            Log.i(TAG, "VideoStreamDeallocate command: nodeId=$nodeId, streamId=$videoStreamId")

            cluster.videoStreamDeallocate(
                object : ChipClusters.DefaultClusterCallback {
                    override fun onSuccess() {
                        Log.i(TAG, "VideoStreamDeallocate succeeded for stream $videoStreamId")
                        callback.onStreamDeallocated()
                    }

                    override fun onError(error: Exception) {
                        Log.e(TAG, "VideoStreamDeallocate failed", error)
                        callback.onError(error.message ?: "Unknown error")
                    }
                },
                videoStreamId
            )
        } catch (e: Exception) {
            Log.e(TAG, "Failed to deallocate video stream", e)
            callback.onError(e.message ?: "Failed to connect to device")
        }
    }
}
