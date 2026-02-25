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
package com.matter.camera.controller.webrtc

import android.content.Context
import android.util.Log
import chip.devicecontroller.ChipClusters
import chip.devicecontroller.ChipStructs
import com.matter.camera.controller.ChipClient
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.webrtc.IceCandidate
import java.util.ArrayList
import java.util.Optional

/**
 * Bridges the Matter WebRTC Transport Provider/Requestor cluster commands
 * with the Android WebRTC PeerConnection.
 *
 * This is the Android equivalent of the C++ WebRTCProviderClient + WebRTCRequestorDelegate,
 * sending SDP offers/answers and ICE candidates over Matter cluster commands.
 */
class MatterWebRTCBridge(private val context: Context) {

    companion object {
        private const val TAG = "MatterWebRTCBridge"
        private const val CAMERA_ENDPOINT_ID = 1
        private const val REQUESTOR_ENDPOINT_ID = 1
    }

    interface BridgeCallback {
        fun onOfferSent(sessionId: Int)
        fun onAnswerReceived(sessionId: Int, sdp: String)
        fun onIceCandidatesReceived(candidates: List<IceCandidate>)
        fun onError(error: String)
    }

    private var callback: BridgeCallback? = null

    fun setCallback(callback: BridgeCallback) {
        this.callback = callback
    }

    /**
     * Reduce SDP size to fit within Matter IM payload limits.
     *
     * We remove ICE candidate lines and several non-essential attributes,
     * keeping the SDP valid but smaller so it can be TLV-encoded without
     * hitting buffer limits.  ICE candidates are sent separately via
     * ProvideICECandidates.
     */
    private fun sanitizeSdpForMatter(sdp: String): String {
        val originalLength = sdp.length
        val filteredLines = sdp
            .lines()
            .filter { line ->
                // Drop ICE candidates; they are sent via ProvideICECandidates.
                if (line.startsWith("a=candidate:")) return@filter false
                if (line.startsWith("a=end-of-candidates")) return@filter false

                // Drop verbose, optional attributes that are typically non-critical
                // for basic WebRTC session establishment.
                if (line.startsWith("a=extmap-allow-mixed")) return@filter false
                if (line.startsWith("a=extmap:")) return@filter false
                if (line.startsWith("a=ssrc:")) return@filter false
                if (line.startsWith("a=ssrc-group:")) return@filter false
                if (line.startsWith("a=msid:")) return@filter false

                // Keep everything else.
                true
            }

        val sanitized = filteredLines.joinToString(separator = "\r\n")
        Log.i(TAG, "Sanitized SDP for Matter: originalLength=$originalLength, sanitizedLength=${sanitized.length}")
        return sanitized
    }

    /**
     * Send a ProvideOffer command to the camera device via the WebRTC Transport Provider cluster.
     * The camera device will respond with an answer SDP.
     *
     * Uses the standard ChipClusters API with the rebuilt native library
     * (CHIP_TLV_WRITER_BUFFER_SIZE increased to 8192 bytes).
     *
     * Mirrors C++ WebRTCManager::ProvideOffer()
     */
    suspend fun sendProvideOffer(
        nodeId: Long,
        sdpOffer: String,
        streamUsage: Int,
        videoStreamId: Int?,
        audioStreamId: Int?
    ) = withContext(Dispatchers.Main) {
        try {
            Log.i(TAG, "Sending ProvideOffer to node $nodeId")

            val devicePtr = ChipClient.getConnectedDevicePointer(context, nodeId)
            val cluster = ChipClusters.WebRTCTransportProviderCluster(devicePtr, CAMERA_ENDPOINT_ID)

            val sanitizedSdp = sanitizeSdpForMatter(sdpOffer)
            Log.d(TAG, "ProvideOffer SDP length: ${sanitizedSdp.length}")

            val videoOpt: Optional<Int>? = videoStreamId?.let { Optional.of(it) }
            val audioOpt: Optional<Int>? = audioStreamId?.let { Optional.of(it) }

            cluster.provideOffer(
                object : ChipClusters.WebRTCTransportProviderCluster.ProvideOfferResponseCallback {
                    override fun onSuccess(
                        webRTCSessionID: Int?,
                        videoStreamID: Optional<Int>?,
                        audioStreamID: Optional<Int>?
                    ) {
                        val sessionId = webRTCSessionID ?: 0
                        Log.i(TAG, "ProvideOffer response: sessionId=$sessionId")
                        callback?.onOfferSent(sessionId)
                    }

                    override fun onError(error: Exception) {
                        Log.e(TAG, "ProvideOffer failed", error)
                        callback?.onError("ProvideOffer failed: ${error.message}")
                    }
                },
                null, // webRTCSessionID = null (new session)
                sanitizedSdp,
                streamUsage,
                REQUESTOR_ENDPOINT_ID,
                videoOpt,
                audioOpt,
                Optional.empty<ArrayList<ChipStructs.WebRTCTransportProviderClusterICEServerStruct>>(),
                Optional.empty<String>(),
                Optional.empty<Boolean>(),
                Optional.empty<ChipStructs.WebRTCTransportProviderClusterSFrameStruct>(),
                Optional.empty<ArrayList<Int>>(),
                Optional.empty<ArrayList<Int>>()
            )
        } catch (e: Exception) {
            Log.e(TAG, "Failed to send ProvideOffer", e)
            callback?.onError(e.message ?: "Connection failed")
        }
    }

    /**
     * Send a ProvideAnswer command to the camera device.
     *
     * Mirrors C++ WebRTCManager::ProvideAnswer()
     */
    suspend fun sendProvideAnswer(
        nodeId: Long,
        sessionId: Int,
        sdpAnswer: String
    ) = withContext(Dispatchers.Main) {
        try {
            Log.i(TAG, "Sending ProvideAnswer for session $sessionId to node $nodeId")

            val devicePtr = ChipClient.getConnectedDevicePointer(context, nodeId)
            val cluster = ChipClusters.WebRTCTransportProviderCluster(devicePtr, CAMERA_ENDPOINT_ID)

            cluster.provideAnswer(
                object : ChipClusters.DefaultClusterCallback {
                    override fun onSuccess() {
                        Log.i(TAG, "ProvideAnswer succeeded for session $sessionId")
                    }

                    override fun onError(error: Exception) {
                        Log.e(TAG, "ProvideAnswer failed", error)
                        callback?.onError("ProvideAnswer failed: ${error.message}")
                    }
                },
                sessionId,
                sdpAnswer
            )
        } catch (e: Exception) {
            Log.e(TAG, "Failed to send ProvideAnswer", e)
            callback?.onError(e.message ?: "Connection failed")
        }
    }

    /**
     * Send ICE candidates to the camera device.
     *
     * Mirrors C++ WebRTCManager::ProvideICECandidates()
     */
    suspend fun sendIceCandidates(
        nodeId: Long,
        sessionId: Int,
        candidates: List<IceCandidate>
    ) = withContext(Dispatchers.Main) {
        try {
            Log.i(TAG, "Sending ${candidates.size} ICE candidates for session $sessionId to node $nodeId")

            val devicePtr = ChipClient.getConnectedDevicePointer(context, nodeId)
            val cluster = ChipClusters.WebRTCTransportProviderCluster(devicePtr, CAMERA_ENDPOINT_ID)

            val structs = ArrayList<ChipStructs.WebRTCTransportProviderClusterICECandidateStruct>()
            for (c in candidates) {
                structs.add(
                    ChipStructs.WebRTCTransportProviderClusterICECandidateStruct(
                        c.sdp,
                        c.sdpMid,
                        c.sdpMLineIndex
                    )
                )
            }

            cluster.provideICECandidates(
                object : ChipClusters.DefaultClusterCallback {
                    override fun onSuccess() {
                        Log.i(TAG, "ProvideICECandidates succeeded (${candidates.size} candidates)")
                    }

                    override fun onError(error: Exception) {
                        Log.e(TAG, "ProvideICECandidates failed", error)
                        callback?.onError("ProvideICECandidates failed: ${error.message}")
                    }
                },
                sessionId,
                structs
            )
        } catch (e: Exception) {
            Log.e(TAG, "Failed to send ICE candidates", e)
            callback?.onError(e.message ?: "Connection failed")
        }
    }

    /**
     * Send a SolicitOffer command to ask the camera device to generate an SDP offer.
     *
     * Mirrors C++ WebRTCManager::SolicitOffer()
     */
    suspend fun sendSolicitOffer(
        nodeId: Long,
        streamUsage: Int,
        videoStreamId: Int?,
        audioStreamId: Int?
    ) = withContext(Dispatchers.Main) {
        try {
            Log.i(TAG, "Sending SolicitOffer to node $nodeId")

            val devicePtr = ChipClient.getConnectedDevicePointer(context, nodeId)
            val cluster = ChipClusters.WebRTCTransportProviderCluster(devicePtr, CAMERA_ENDPOINT_ID)

            val videoOpt = videoStreamId?.let { Optional.of(it) } ?: Optional.empty<Int>()
            val audioOpt = audioStreamId?.let { Optional.of(it) } ?: Optional.empty<Int>()

            cluster.solicitOffer(
                object : ChipClusters.WebRTCTransportProviderCluster.SolicitOfferResponseCallback {
                    override fun onSuccess(
                        webRTCSessionID: Int?,
                        deferredOffer: Boolean?,
                        videoStreamID: java.util.Optional<Int>?,
                        audioStreamID: java.util.Optional<Int>?
                    ) {
                        Log.i(TAG, "SolicitOffer response: sessionId=$webRTCSessionID, deferred=$deferredOffer")
                        callback?.onOfferSent(webRTCSessionID ?: 0)
                    }

                    override fun onError(error: Exception) {
                        Log.e(TAG, "SolicitOffer failed", error)
                        callback?.onError("SolicitOffer failed: ${error.message}")
                    }
                },
                streamUsage,
                REQUESTOR_ENDPOINT_ID,
                videoOpt,
                audioOpt,
                Optional.empty<ArrayList<ChipStructs.WebRTCTransportProviderClusterICEServerStruct>>(),
                Optional.empty<String>(),
                Optional.empty<Boolean>(),
                Optional.empty<ChipStructs.WebRTCTransportProviderClusterSFrameStruct>(),
                Optional.empty<ArrayList<Int>>(),
                Optional.empty<ArrayList<Int>>()
            )
        } catch (e: Exception) {
            Log.e(TAG, "Failed to send SolicitOffer", e)
            callback?.onError(e.message ?: "Connection failed")
        }
    }
}
