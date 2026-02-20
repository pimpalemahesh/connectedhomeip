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
import com.matter.camera.controller.ChipClient
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.webrtc.IceCandidate

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
     * Send a ProvideOffer command to the camera device via the WebRTC Transport Provider cluster.
     * The camera device will respond with an answer SDP.
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

            cluster.provideOffer(
                object : ChipClusters.WebRTCTransportProviderCluster.ProvideOfferResponseCallback {
                    override fun onSuccess(
                        webRTCSessionID: Int,
                        videoStreamID: Int,
                        audioStreamID: Int
                    ) {
                        Log.i(TAG, "ProvideOffer response: sessionId=$webRTCSessionID")
                        callback?.onOfferSent(webRTCSessionID)
                    }

                    override fun onError(error: Exception) {
                        Log.e(TAG, "ProvideOffer failed", error)
                        callback?.onError("ProvideOffer failed: ${error.message}")
                    }
                },
                null, // webRTCSessionID (null for new session)
                sdpOffer,
                streamUsage,
                videoStreamId,
                audioStreamId,
                null  // ICEServers
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

            // Convert Android IceCandidates to the format expected by the Matter cluster
            val candidateStrings = candidates.map { it.sdp }

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
                candidateStrings
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

            cluster.solicitOffer(
                object : ChipClusters.WebRTCTransportProviderCluster.SolicitOfferResponseCallback {
                    override fun onSuccess(
                        webRTCSessionID: Int,
                        deferredOffer: Boolean,
                        videoStreamID: Int?,
                        audioStreamID: Int?
                    ) {
                        Log.i(TAG, "SolicitOffer response: sessionId=$webRTCSessionID, deferred=$deferredOffer")
                        callback?.onOfferSent(webRTCSessionID)
                    }

                    override fun onError(error: Exception) {
                        Log.e(TAG, "SolicitOffer failed", error)
                        callback?.onError("SolicitOffer failed: ${error.message}")
                    }
                },
                streamUsage,
                videoStreamId,
                audioStreamId
            )
        } catch (e: Exception) {
            Log.e(TAG, "Failed to send SolicitOffer", e)
            callback?.onError(e.message ?: "Connection failed")
        }
    }
}
