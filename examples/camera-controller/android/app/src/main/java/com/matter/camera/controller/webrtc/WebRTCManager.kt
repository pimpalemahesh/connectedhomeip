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
import org.webrtc.DataChannel
import org.webrtc.DefaultVideoDecoderFactory
import org.webrtc.DefaultVideoEncoderFactory
import org.webrtc.EglBase
import org.webrtc.IceCandidate
import org.webrtc.MediaConstraints
import org.webrtc.MediaStream
import org.webrtc.PeerConnection
import org.webrtc.PeerConnectionFactory
import org.webrtc.RtpReceiver
import org.webrtc.RtpTransceiver
import org.webrtc.SdpObserver
import org.webrtc.SessionDescription
import org.webrtc.SurfaceViewRenderer
import org.webrtc.VideoTrack

/**
 * Manages WebRTC peer connections for live video streaming from Matter camera devices.
 * Android equivalent of the C++ WebRTCManager.
 *
 * Handles:
 * - PeerConnection lifecycle (create, connect, disconnect)
 * - SDP offer/answer exchange
 * - ICE candidate management
 * - Video track rendering to a SurfaceViewRenderer
 */
class WebRTCManager(private val context: Context) {

    companion object {
        private const val TAG = "WebRTCManager"
        private const val STUN_SERVER = "stun:stun.l.google.com:19302"
    }

    interface WebRTCListener {
        fun onLocalSdpReady(sdp: String, type: String)
        fun onIceCandidateReady(candidate: IceCandidate)
        fun onIceGatheringComplete()
        fun onConnected()
        fun onDisconnected()
        fun onError(error: String)
        fun onVideoTrackReceived()
    }

    private var peerConnectionFactory: PeerConnectionFactory? = null
    private var peerConnection: PeerConnection? = null
    private var eglBase: EglBase? = null
    private var videoTrack: VideoTrack? = null
    private var renderer: SurfaceViewRenderer? = null
    private var listener: WebRTCListener? = null
    private val localIceCandidates = mutableListOf<IceCandidate>()

    val localSdp: String?
        get() = _localSdp
    private var _localSdp: String? = null

    val iceCandidates: List<IceCandidate>
        get() = localIceCandidates.toList()

    fun setListener(listener: WebRTCListener) {
        this.listener = listener
    }

    fun getEglBase(): EglBase {
        if (eglBase == null) {
            eglBase = EglBase.create()
        }
        return eglBase!!
    }

    /**
     * Initialize the WebRTC peer connection factory and EGL context.
     */
    fun initialize() {
        Log.i(TAG, "Initializing WebRTC")

        if (eglBase == null) {
            eglBase = EglBase.create()
        }

        val initOptions = PeerConnectionFactory.InitializationOptions.builder(context)
            .setEnableInternalTracer(false)
            .createInitializationOptions()
        PeerConnectionFactory.initialize(initOptions)

        val encoderFactory = DefaultVideoEncoderFactory(
            eglBase!!.eglBaseContext, true, true
        )
        val decoderFactory = DefaultVideoDecoderFactory(eglBase!!.eglBaseContext)

        peerConnectionFactory = PeerConnectionFactory.builder()
            .setVideoEncoderFactory(encoderFactory)
            .setVideoDecoderFactory(decoderFactory)
            .createPeerConnectionFactory()

        Log.i(TAG, "WebRTC initialized successfully")
    }

    /**
     * Attach a SurfaceViewRenderer for displaying the remote video.
     */
    fun attachRenderer(surfaceViewRenderer: SurfaceViewRenderer) {
        renderer = surfaceViewRenderer
        renderer?.init(eglBase!!.eglBaseContext, null)
        renderer?.setMirror(false)
        renderer?.setEnableHardwareScaler(true)

        videoTrack?.addSink(renderer)
    }

    /**
     * Create a new peer connection and generate a local SDP offer.
     * This mirrors the C++ WebRTCManager::Connect() + ProvideOffer() flow.
     */
    fun createOfferConnection() {
        Log.i(TAG, "Creating offer-based peer connection")

        localIceCandidates.clear()
        _localSdp = null

        val iceServers = listOf(
            PeerConnection.IceServer.builder(STUN_SERVER).createIceServer()
        )

        val rtcConfig = PeerConnection.RTCConfiguration(iceServers).apply {
            sdpSemantics = PeerConnection.SdpSemantics.UNIFIED_PLAN
            continualGatheringPolicy = PeerConnection.ContinualGatheringPolicy.GATHER_CONTINUALLY
        }

        peerConnection = peerConnectionFactory?.createPeerConnection(
            rtcConfig,
            peerConnectionObserver
        )

        if (peerConnection == null) {
            listener?.onError("Failed to create PeerConnection")
            return
        }

        // Add receive-only transceiver for video (H.264)
        peerConnection?.addTransceiver(
            org.webrtc.MediaStreamTrack.MediaType.MEDIA_TYPE_VIDEO,
            RtpTransceiver.RtpTransceiverInit(RtpTransceiver.RtpTransceiverDirection.RECV_ONLY)
        )

        // Add receive-only transceiver for audio
        peerConnection?.addTransceiver(
            org.webrtc.MediaStreamTrack.MediaType.MEDIA_TYPE_AUDIO,
            RtpTransceiver.RtpTransceiverInit(RtpTransceiver.RtpTransceiverDirection.RECV_ONLY)
        )

        val constraints = MediaConstraints().apply {
            mandatory.add(MediaConstraints.KeyValuePair("OfferToReceiveVideo", "true"))
            mandatory.add(MediaConstraints.KeyValuePair("OfferToReceiveAudio", "true"))
        }

        peerConnection?.createOffer(sdpObserver, constraints)
    }

    /**
     * Set the remote SDP answer received from the camera device.
     */
    fun setRemoteAnswer(sdp: String) {
        Log.i(TAG, "Setting remote SDP answer")
        val sessionDescription = SessionDescription(SessionDescription.Type.ANSWER, sdp)
        peerConnection?.setRemoteDescription(remoteSdpObserver, sessionDescription)
    }

    /**
     * Set a remote SDP offer and create an answer.
     * Used when the camera device sends an offer (SolicitOffer flow).
     */
    fun setRemoteOfferAndCreateAnswer(sdp: String) {
        Log.i(TAG, "Setting remote SDP offer and creating answer")
        val sessionDescription = SessionDescription(SessionDescription.Type.OFFER, sdp)
        peerConnection?.setRemoteDescription(object : SdpObserver {
            override fun onCreateSuccess(desc: SessionDescription?) {}
            override fun onSetSuccess() {
                Log.i(TAG, "Remote offer set successfully, creating answer")
                val constraints = MediaConstraints().apply {
                    mandatory.add(MediaConstraints.KeyValuePair("OfferToReceiveVideo", "true"))
                    mandatory.add(MediaConstraints.KeyValuePair("OfferToReceiveAudio", "true"))
                }
                peerConnection?.createAnswer(sdpObserver, constraints)
            }

            override fun onCreateFailure(error: String?) {
                listener?.onError("Failed to create answer: $error")
            }

            override fun onSetFailure(error: String?) {
                listener?.onError("Failed to set remote offer: $error")
            }
        }, sessionDescription)
    }

    /**
     * Add a remote ICE candidate received from the camera device.
     */
    fun addRemoteIceCandidate(candidate: IceCandidate) {
        Log.d(TAG, "Adding remote ICE candidate: ${candidate.sdp}")
        peerConnection?.addIceCandidate(candidate)
    }

    /**
     * Disconnect and clean up all WebRTC resources.
     */
    fun disconnect() {
        Log.i(TAG, "Disconnecting WebRTC session")

        videoTrack?.removeSink(renderer)
        videoTrack = null

        peerConnection?.close()
        peerConnection?.dispose()
        peerConnection = null

        localIceCandidates.clear()
        _localSdp = null
    }

    /**
     * Release all resources including the factory and EGL context.
     */
    fun release() {
        disconnect()

        renderer?.release()
        renderer = null

        peerConnectionFactory?.dispose()
        peerConnectionFactory = null

        eglBase?.release()
        eglBase = null
    }

    private val peerConnectionObserver = object : PeerConnection.Observer {
        override fun onSignalingChange(state: PeerConnection.SignalingState?) {
            Log.d(TAG, "Signaling state: $state")
        }

        override fun onIceConnectionChange(state: PeerConnection.IceConnectionState?) {
            Log.i(TAG, "ICE connection state: $state")
            when (state) {
                PeerConnection.IceConnectionState.CONNECTED -> listener?.onConnected()
                PeerConnection.IceConnectionState.DISCONNECTED,
                PeerConnection.IceConnectionState.FAILED,
                PeerConnection.IceConnectionState.CLOSED -> listener?.onDisconnected()
                else -> {}
            }
        }

        override fun onIceConnectionReceivingChange(receiving: Boolean) {
            Log.d(TAG, "ICE receiving: $receiving")
        }

        override fun onIceGatheringChange(state: PeerConnection.IceGatheringState?) {
            Log.d(TAG, "ICE gathering state: $state")
            if (state == PeerConnection.IceGatheringState.COMPLETE) {
                listener?.onIceGatheringComplete()
            }
        }

        override fun onIceCandidate(candidate: IceCandidate?) {
            candidate?.let {
                Log.d(TAG, "Local ICE candidate: ${it.sdp}")
                localIceCandidates.add(it)
                listener?.onIceCandidateReady(it)
            }
        }

        override fun onIceCandidatesRemoved(candidates: Array<out IceCandidate>?) {
            Log.d(TAG, "ICE candidates removed")
        }

        override fun onAddStream(stream: MediaStream?) {
            Log.i(TAG, "Remote stream added: ${stream?.videoTracks?.size} video tracks")
            stream?.videoTracks?.firstOrNull()?.let { track ->
                videoTrack = track
                track.setEnabled(true)
                renderer?.let { track.addSink(it) }
                listener?.onVideoTrackReceived()
            }
        }

        override fun onRemoveStream(stream: MediaStream?) {
            Log.i(TAG, "Remote stream removed")
            videoTrack = null
        }

        override fun onDataChannel(channel: DataChannel?) {
            Log.d(TAG, "Data channel: ${channel?.label()}")
        }

        override fun onRenegotiationNeeded() {
            Log.d(TAG, "Renegotiation needed")
        }

        override fun onAddTrack(receiver: RtpReceiver?, streams: Array<out MediaStream>?) {
            Log.i(TAG, "Track added: ${receiver?.track()?.kind()}")
            val track = receiver?.track()
            if (track is VideoTrack) {
                videoTrack = track
                track.setEnabled(true)
                renderer?.let { track.addSink(it) }
                listener?.onVideoTrackReceived()
            }
        }
    }

    private val sdpObserver = object : SdpObserver {
        override fun onCreateSuccess(desc: SessionDescription?) {
            desc?.let {
                Log.i(TAG, "Local SDP created (${it.type})")
                _localSdp = it.description
                peerConnection?.setLocalDescription(localSdpSetObserver, it)
                listener?.onLocalSdpReady(it.description, it.type.canonicalForm())
            }
        }

        override fun onSetSuccess() {}

        override fun onCreateFailure(error: String?) {
            Log.e(TAG, "SDP creation failed: $error")
            listener?.onError("SDP creation failed: $error")
        }

        override fun onSetFailure(error: String?) {
            Log.e(TAG, "SDP set failed: $error")
            listener?.onError("SDP set failed: $error")
        }
    }

    private val localSdpSetObserver = object : SdpObserver {
        override fun onCreateSuccess(desc: SessionDescription?) {}
        override fun onSetSuccess() {
            Log.i(TAG, "Local SDP set successfully")
        }

        override fun onCreateFailure(error: String?) {}
        override fun onSetFailure(error: String?) {
            Log.e(TAG, "Failed to set local SDP: $error")
        }
    }

    private val remoteSdpObserver = object : SdpObserver {
        override fun onCreateSuccess(desc: SessionDescription?) {}
        override fun onSetSuccess() {
            Log.i(TAG, "Remote SDP set successfully")
        }

        override fun onCreateFailure(error: String?) {
            Log.e(TAG, "Remote SDP creation failed: $error")
        }

        override fun onSetFailure(error: String?) {
            Log.e(TAG, "Failed to set remote SDP: $error")
            listener?.onError("Failed to set remote SDP: $error")
        }
    }
}
