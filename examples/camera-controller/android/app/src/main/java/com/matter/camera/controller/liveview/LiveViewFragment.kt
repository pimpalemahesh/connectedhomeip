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
package com.matter.camera.controller.liveview

import android.os.Bundle
import android.util.Log
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.ArrayAdapter
import android.widget.Toast
import androidx.core.content.ContextCompat
import androidx.fragment.app.Fragment
import androidx.lifecycle.lifecycleScope
import com.matter.camera.controller.R
import com.matter.camera.controller.databinding.FragmentLiveViewBinding
import com.matter.camera.controller.devices.CameraDevice
import com.matter.camera.controller.devices.DeviceRepository
import com.matter.camera.controller.streaming.CameraStreamManager
import com.matter.camera.controller.webrtc.MatterWebRTCBridge
import com.matter.camera.controller.webrtc.WebRTCManager
import kotlinx.coroutines.launch
import org.webrtc.IceCandidate

/**
 * Fragment for live video streaming from Matter camera devices.
 *
 * Implements the full live view flow:
 * 1. Select a paired camera device
 * 2. Allocate a video stream (CameraAvStreamManagement cluster)
 * 3. Establish WebRTC session (WebRTC Transport Provider cluster)
 * 4. Display live video via SurfaceViewRenderer
 *
 * This is the Android equivalent of the C++ LiveViewStartCommand/LiveViewStopCommand.
 */
class LiveViewFragment : Fragment() {

    private var _binding: FragmentLiveViewBinding? = null
    private val binding get() = _binding!!

    private lateinit var webRTCManager: WebRTCManager
    private lateinit var streamManager: CameraStreamManager
    private lateinit var webRTCBridge: MatterWebRTCBridge

    private var devices = listOf<CameraDevice>()
    private var selectedDevice: CameraDevice? = null
    private var currentStreamId: Int = -1
    private var currentSessionId: Int = -1
    private var isStreaming = false

    companion object {
        private const val TAG = "LiveViewFragment"
    }

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?
    ): View {
        _binding = FragmentLiveViewBinding.inflate(inflater, container, false)
        return binding.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)

        streamManager = CameraStreamManager(requireContext())
        webRTCManager = WebRTCManager(requireContext())
        webRTCBridge = MatterWebRTCBridge(requireContext())

        webRTCManager.initialize()

        setupWebRTCListeners()
        setupBridgeCallbacks()
        setupUI()
        loadDevices()

        // If launched with a specific node ID from the devices screen
        arguments?.getLong("nodeId", -1)?.let { nodeId ->
            if (nodeId > 0) {
                selectDeviceByNodeId(nodeId)
            }
        }
    }

    private fun setupUI() {
        binding.btnStartStream.setOnClickListener { startLiveView() }
        binding.btnStopStream.setOnClickListener { stopLiveView() }

        binding.cameraSpinner.onItemSelectedListener =
            object : android.widget.AdapterView.OnItemSelectedListener {
                override fun onItemSelected(
                    parent: android.widget.AdapterView<*>?,
                    view: View?,
                    position: Int,
                    id: Long
                ) {
                    if (position < devices.size) {
                        selectedDevice = devices[position]
                    }
                }

                override fun onNothingSelected(parent: android.widget.AdapterView<*>?) {
                    selectedDevice = null
                }
            }
    }

    private fun loadDevices() {
        devices = DeviceRepository.getDevices(requireContext())
        val names = devices.map { "${it.name} (${it.displayNodeId})" }
        val adapter = ArrayAdapter(requireContext(), android.R.layout.simple_spinner_item, names)
        adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item)
        binding.cameraSpinner.adapter = adapter

        if (devices.isEmpty()) {
            binding.btnStartStream.isEnabled = false
        }
    }

    private fun selectDeviceByNodeId(nodeId: Long) {
        val index = devices.indexOfFirst { it.nodeId == nodeId }
        if (index >= 0) {
            binding.cameraSpinner.setSelection(index)
            selectedDevice = devices[index]
        }
    }

    private fun setupWebRTCListeners() {
        webRTCManager.setListener(object : WebRTCManager.WebRTCListener {
            override fun onLocalSdpReady(sdp: String, type: String) {
                Log.i(TAG, "Local SDP ready ($type), sending to camera device")
                sendOfferToDevice(sdp)
            }

            override fun onIceCandidateReady(candidate: IceCandidate) {
                Log.d(TAG, "Local ICE candidate ready")
            }

            override fun onIceGatheringComplete() {
                Log.i(TAG, "ICE gathering complete, sending candidates to device")
                sendIceCandidatesToDevice()
            }

            override fun onConnected() {
                activity?.runOnUiThread {
                    onStreamConnected()
                }
            }

            override fun onDisconnected() {
                activity?.runOnUiThread {
                    onStreamDisconnected()
                }
            }

            override fun onError(error: String) {
                activity?.runOnUiThread {
                    showStreamError(error)
                }
            }

            override fun onVideoTrackReceived() {
                activity?.runOnUiThread {
                    Log.i(TAG, "Video track received, showing renderer")
                    binding.videoRenderer.visibility = View.VISIBLE
                    binding.videoPlaceholder.visibility = View.GONE
                }
            }
        })
    }

    private fun setupBridgeCallbacks() {
        webRTCBridge.setCallback(object : MatterWebRTCBridge.BridgeCallback {
            override fun onOfferSent(sessionId: Int) {
                currentSessionId = sessionId
                Log.i(TAG, "WebRTC session created: $sessionId")
            }

            override fun onAnswerReceived(sessionId: Int, sdp: String) {
                Log.i(TAG, "Received SDP answer for session $sessionId")
                webRTCManager.setRemoteAnswer(sdp)
            }

            override fun onIceCandidatesReceived(candidates: List<IceCandidate>) {
                candidates.forEach { webRTCManager.addRemoteIceCandidate(it) }
            }

            override fun onError(error: String) {
                activity?.runOnUiThread {
                    showStreamError(error)
                }
            }
        })
    }

    /**
     * Start live view: allocate video stream then establish WebRTC session.
     * Mirrors the C++ LiveViewStartCommand::RunCommand() flow.
     */
    private fun startLiveView() {
        val device = selectedDevice ?: run {
            Toast.makeText(requireContext(), getString(R.string.no_camera_selected), Toast.LENGTH_SHORT).show()
            return
        }

        val width = binding.inputWidth.text?.toString()?.toIntOrNull()
        val height = binding.inputHeight.text?.toString()?.toIntOrNull()
        val frameRate = binding.inputFramerate.text?.toString()?.toIntOrNull()
        val bitRate = binding.inputBitrate.text?.toString()?.toLongOrNull()

        showStreamStatus(getString(R.string.connecting), isProgress = true)
        binding.btnStartStream.isEnabled = false
        binding.btnStopStream.isEnabled = true

        // Initialize the video renderer
        binding.videoRenderer.visibility = View.GONE
        binding.videoPlaceholder.visibility = View.VISIBLE
        binding.placeholderText.text = getString(R.string.connecting)
        webRTCManager.attachRenderer(binding.videoRenderer)

        // Step 1: Allocate video stream
        lifecycleScope.launch {
            streamManager.allocateVideoStream(
                nodeId = device.nodeId,
                streamUsage = CameraStreamManager.STREAM_USAGE_LIVE_VIEW,
                minWidth = width,
                minHeight = height,
                minFrameRate = frameRate,
                minBitRate = bitRate,
                callback = object : CameraStreamManager.StreamCallback {
                    override fun onStreamAllocated(videoStreamId: Int) {
                        Log.i(TAG, "Video stream allocated: $videoStreamId")
                        currentStreamId = videoStreamId
                        showStreamStatus(
                            getString(R.string.video_stream_allocated, videoStreamId),
                            isProgress = true
                        )
                        // Step 2: Establish WebRTC session
                        initiateWebRTCSession()
                    }

                    override fun onStreamDeallocated() {}

                    override fun onError(error: String) {
                        activity?.runOnUiThread {
                            showStreamError(getString(R.string.video_stream_failed, error))
                            binding.btnStartStream.isEnabled = true
                            binding.btnStopStream.isEnabled = false
                        }
                    }
                }
            )
        }
    }

    /**
     * Step 2 of the live view flow: create WebRTC peer connection and send
     * an SDP offer to the camera via Matter commands.
     */
    private fun initiateWebRTCSession() {
        activity?.runOnUiThread {
            showStreamStatus("Establishing WebRTC session...", isProgress = true)
        }
        webRTCManager.createOfferConnection()
    }

    /**
     * Send the locally generated SDP offer to the camera via the Matter
     * WebRTC Transport Provider cluster.
     */
    private fun sendOfferToDevice(sdp: String) {
        val device = selectedDevice ?: return
        lifecycleScope.launch {
            webRTCBridge.sendProvideOffer(
                nodeId = device.nodeId,
                sdpOffer = sdp,
                streamUsage = CameraStreamManager.STREAM_USAGE_LIVE_VIEW,
                videoStreamId = if (currentStreamId >= 0) currentStreamId else null,
                audioStreamId = null
            )
        }
    }

    /**
     * Send locally gathered ICE candidates to the camera device.
     */
    private fun sendIceCandidatesToDevice() {
        val device = selectedDevice ?: return
        val candidates = webRTCManager.iceCandidates
        if (candidates.isEmpty()) return

        lifecycleScope.launch {
            webRTCBridge.sendIceCandidates(
                nodeId = device.nodeId,
                sessionId = currentSessionId,
                candidates = candidates
            )
        }
    }

    /**
     * Stop live view: disconnect WebRTC and deallocate the video stream.
     * Mirrors the C++ LiveViewStopCommand::RunCommand() flow.
     */
    private fun stopLiveView() {
        val device = selectedDevice ?: return

        webRTCManager.disconnect()

        if (currentStreamId >= 0) {
            lifecycleScope.launch {
                streamManager.deallocateVideoStream(
                    nodeId = device.nodeId,
                    videoStreamId = currentStreamId,
                    callback = object : CameraStreamManager.StreamCallback {
                        override fun onStreamAllocated(videoStreamId: Int) {}
                        override fun onStreamDeallocated() {
                            activity?.runOnUiThread {
                                onStreamStopped()
                            }
                        }

                        override fun onError(error: String) {
                            activity?.runOnUiThread {
                                onStreamStopped()
                            }
                        }
                    }
                )
            }
        } else {
            onStreamStopped()
        }
    }

    private fun onStreamConnected() {
        isStreaming = true
        binding.liveBadge.visibility = View.VISIBLE
        showStreamStatus(getString(R.string.webrtc_session_established), isConnected = true)
        binding.btnStartStream.isEnabled = false
        binding.btnStopStream.isEnabled = true
    }

    private fun onStreamDisconnected() {
        if (isStreaming) {
            onStreamStopped()
        }
    }

    private fun onStreamStopped() {
        isStreaming = false
        currentStreamId = -1
        currentSessionId = -1
        binding.liveBadge.visibility = View.GONE
        binding.videoRenderer.visibility = View.GONE
        binding.videoPlaceholder.visibility = View.VISIBLE
        binding.placeholderText.text = getString(R.string.no_camera_selected)
        binding.btnStartStream.isEnabled = true
        binding.btnStopStream.isEnabled = false
        showStreamStatus(getString(R.string.stream_stopped), isConnected = false)
    }

    private fun showStreamStatus(message: String, isProgress: Boolean = false, isConnected: Boolean = false) {
        binding.streamStatusCard.visibility = View.VISIBLE
        binding.streamStatusText.text = message

        if (isProgress) {
            binding.streamProgress.visibility = View.VISIBLE
            binding.streamStatusDot.visibility = View.GONE
        } else {
            binding.streamProgress.visibility = View.GONE
            binding.streamStatusDot.visibility = View.VISIBLE
            val color = if (isConnected) R.color.status_streaming else R.color.status_disconnected
            binding.streamStatusDot.background.setTint(
                ContextCompat.getColor(requireContext(), color)
            )
        }
    }

    private fun showStreamError(error: String) {
        binding.streamStatusCard.visibility = View.VISIBLE
        binding.streamProgress.visibility = View.GONE
        binding.streamStatusDot.visibility = View.VISIBLE
        binding.streamStatusDot.background.setTint(
            ContextCompat.getColor(requireContext(), R.color.error)
        )
        binding.streamStatusText.text = error
        binding.streamStatusText.setTextColor(
            ContextCompat.getColor(requireContext(), R.color.error)
        )
        binding.btnStartStream.isEnabled = true
        binding.btnStopStream.isEnabled = false
    }

    override fun onDestroyView() {
        super.onDestroyView()
        if (isStreaming) {
            webRTCManager.disconnect()
        }
        webRTCManager.release()
        _binding = null
    }
}
