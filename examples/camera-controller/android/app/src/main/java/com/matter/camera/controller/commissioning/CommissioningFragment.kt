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
package com.matter.camera.controller.commissioning

import android.Manifest
import android.bluetooth.BluetoothAdapter
import android.content.pm.ApplicationInfo
import android.content.pm.PackageManager
import android.os.Bundle
import android.util.Log
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.Toast
import androidx.camera.core.CameraSelector
import androidx.camera.core.ImageAnalysis
import androidx.camera.core.Preview
import androidx.camera.lifecycle.ProcessCameraProvider
import androidx.core.content.ContextCompat
import androidx.fragment.app.Fragment
import chip.devicecontroller.ChipDeviceController
import chip.devicecontroller.CommissionParameters
import com.google.mlkit.vision.barcode.BarcodeScanning
import com.google.mlkit.vision.common.InputImage
import com.matter.camera.controller.ChipClient
import com.matter.camera.controller.R
import com.matter.camera.controller.databinding.FragmentCommissioningBinding
import com.matter.camera.controller.debug.DebugLog
import com.matter.camera.controller.debug.DebugLogDialogFragment
import com.matter.camera.controller.devices.CameraDevice
import com.matter.camera.controller.devices.DeviceRepository
import java.util.concurrent.Executors

/**
 * Fragment handling Matter device commissioning (any device type: lights, cameras, etc.).
 * Supports both QR code scanning (MT:... or manual pairing code) and manual code entry.
 */
class CommissioningFragment : Fragment() {

    private var _binding: FragmentCommissioningBinding? = null
    private val binding get() = _binding!!

    private var isScannerActive = false
    private var commissioningNodeId: Long? = null

    companion object {
        private const val TAG = "CommissioningFragment"
        private const val CAMERA_PERMISSION_CODE = 101
    }

    private fun isDebugBuild(): Boolean =
        (requireContext().applicationInfo.flags and ApplicationInfo.FLAG_DEBUGGABLE) != 0

    private fun logDebug(message: String) {
        if (isDebugBuild()) Log.d(TAG, message)
        if (DebugLog.isEnabled) DebugLog.add("$TAG: $message")
    }

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?
    ): View {
        _binding = FragmentCommissioningBinding.inflate(inflater, container, false)
        return binding.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)

        binding.btnScanQr.setOnClickListener { toggleQrScanner() }
        binding.btnCommission.setOnClickListener { startCommissioning() }

        binding.debugModeSwitch.isChecked = DebugLog.isEnabled
        binding.debugModeSwitch.setOnCheckedChangeListener { _, isChecked ->
            DebugLog.setEnabled(isChecked)
        }
        binding.btnViewDebugLog.setOnClickListener {
            DebugLogDialogFragment().show(parentFragmentManager, "DebugLog")
        }
        binding.btnStopCommissioning.setOnClickListener { stopCommissioning() }
    }

    private fun toggleQrScanner() {
        if (isScannerActive) {
            stopQrScanner()
        } else {
            if (ContextCompat.checkSelfPermission(requireContext(), Manifest.permission.CAMERA)
                == PackageManager.PERMISSION_GRANTED
            ) {
                startQrScanner()
            } else {
                requestPermissions(arrayOf(Manifest.permission.CAMERA), CAMERA_PERMISSION_CODE)
            }
        }
    }

    @androidx.camera.core.ExperimentalGetImage
    private fun startQrScanner() {
        isScannerActive = true
        binding.cameraPreview.visibility = View.VISIBLE
        binding.btnScanQr.text = "Stop Scanner"

        val cameraProviderFuture = ProcessCameraProvider.getInstance(requireContext())
        cameraProviderFuture.addListener({
            val cameraProvider = cameraProviderFuture.get()
            val preview = Preview.Builder().build().also {
                it.setSurfaceProvider(binding.cameraPreview.surfaceProvider)
            }

            val barcodeScanner = BarcodeScanning.getClient()
            val analysis = ImageAnalysis.Builder()
                .setBackpressureStrategy(ImageAnalysis.STRATEGY_KEEP_ONLY_LATEST)
                .build()

            analysis.setAnalyzer(Executors.newSingleThreadExecutor()) { imageProxy ->
                val mediaImage = imageProxy.image ?: run {
                    imageProxy.close()
                    return@setAnalyzer
                }
                val image = InputImage.fromMediaImage(
                    mediaImage,
                    imageProxy.imageInfo.rotationDegrees
                )
                barcodeScanner.process(image)
                    .addOnSuccessListener { barcodes ->
                        for (barcode in barcodes) {
                            barcode.rawValue?.let { code ->
                                Log.i(TAG, "QR code scanned: $code")
                                activity?.runOnUiThread {
                                    handleQrCode(code)
                                    stopQrScanner()
                                    cameraProvider.unbindAll()
                                }
                            }
                        }
                    }
                    .addOnCompleteListener { imageProxy.close() }
            }

            try {
                cameraProvider.unbindAll()
                cameraProvider.bindToLifecycle(
                    this,
                    CameraSelector.DEFAULT_BACK_CAMERA,
                    preview,
                    analysis
                )
            } catch (e: Exception) {
                Log.e(TAG, "Camera binding failed", e)
            }
        }, ContextCompat.getMainExecutor(requireContext()))
    }

    private fun stopQrScanner() {
        isScannerActive = false
        binding.cameraPreview.visibility = View.GONE
        binding.btnScanQr.text = getString(R.string.scan_qr_code)
    }

    private fun handleQrCode(qrCode: String) {
        Log.i(TAG, "Processing QR code")
        Toast.makeText(requireContext(), "QR code scanned", Toast.LENGTH_SHORT).show()
        binding.inputQrOrManual.setText(qrCode)
    }

    private fun stopCommissioning() {
        commissioningNodeId?.let { nodeId ->
            logDebug("Stopping commissioning for nodeId=$nodeId")
            try {
                ChipClient.getDeviceController(requireContext()).stopDevicePairing(nodeId)
            } catch (e: Exception) {
                Log.e(TAG, "Stop commissioning failed", e)
            }
            commissioningNodeId = null
        }
        hideCommissioningProgress()
    }

    private fun startCommissioning() {
        val nodeIdText = binding.inputNodeId.text?.toString()?.trim()
        val passcodeText = binding.inputPasscode.text?.toString()?.trim()
        val discriminatorText = binding.inputDiscriminator.text?.toString()?.trim()
        val qrOrManualText = binding.inputQrOrManual.text?.toString()?.trim()
        val useNetworkOnly = binding.checkboxNetworkOnly.isChecked

        if (nodeIdText.isNullOrBlank()) {
            binding.inputNodeId.error = "Required"
            return
        }

        val nodeId = nodeIdText.toLongOrNull() ?: run {
            binding.inputNodeId.error = "Invalid number"
            return
        }

        val setupCodeOnly: String = when {
            !qrOrManualText.isNullOrBlank() -> {
                if (qrOrManualText.startsWith("MT:") || qrOrManualText.startsWith("mt:")) {
                    qrOrManualText
                } else if (qrOrManualText.all { it.isDigit() } && (qrOrManualText.length == 11 || qrOrManualText.length == 21)) {
                    qrOrManualText
                } else {
                    binding.inputQrOrManual.error = getString(R.string.setup_code_integrity_failed)
                    return
                }
            }
            !passcodeText.isNullOrBlank() -> {
                if (passcodeText.length != 8 || !passcodeText.all { it.isDigit() }) {
                    binding.inputPasscode.error = "Passcode must be 8 digits"
                    return
                }
                val disc = discriminatorText?.toIntOrNull()?.coerceIn(0, 15) ?: 0
                val passcode = passcodeText.toLongOrNull() ?: run {
                    binding.inputPasscode.error = "Invalid passcode"
                    return
                }
                ManualSetupCodeGenerator.generate(passcode, disc) ?: run {
                    binding.inputPasscode.error = "Invalid passcode (use 1–99999998)"
                    return
                }
            }
            else -> {
                binding.inputPasscode.error = "Enter passcode or paste QR / 11-digit code"
                return
            }
        }

        if (!useNetworkOnly) {
            val bluetoothAdapter = BluetoothAdapter.getDefaultAdapter()
            if (bluetoothAdapter == null || !bluetoothAdapter.isEnabled) {
                Toast.makeText(
                    requireContext(),
                    getString(R.string.bluetooth_required_for_commissioning),
                    Toast.LENGTH_LONG
                ).show()
                return
            }
        }

        logDebug("Starting commissioning: nodeId=$nodeId, networkOnly=$useNetworkOnly")

        commissioningNodeId = nodeId
        showCommissioningProgress()

        val deviceController = ChipClient.getDeviceController(requireContext())
        deviceController.setCompletionListener(object : ChipDeviceController.CompletionListener {
            override fun onCommissioningComplete(commissionedNodeId: Long, errorCode: Long) {
                logDebug("onCommissioningComplete: nodeId=$commissionedNodeId, errorCode=$errorCode")
                activity?.runOnUiThread {
                    commissioningNodeId = null
                    hideCommissioningProgress()
                    if (errorCode == 0L) {
                        onCommissioningSuccess(commissionedNodeId, setupCodeOnly, discriminatorText?.toIntOrNull()?.coerceIn(0, 15) ?: 0)
                    } else {
                        val userMessage = when (errorCode) {
                            32L, 45L -> getString(R.string.commissioning_failed_timeout)
                            else -> "Error code: $errorCode"
                        }
                        onCommissioningFailed(userMessage)
                    }
                }
            }

            override fun onStatusUpdate(status: Int) {
                logDebug("Commissioning status: $status")
            }

            override fun onPairingComplete(errorCode: Long) {
                logDebug("Pairing complete: errorCode=$errorCode")
            }

            override fun onPairingDeleted(errorCode: Long) {
                logDebug("Pairing deleted: errorCode=$errorCode")
            }

            override fun onNotifyChipConnectionClosed() {
                logDebug("Connection closed")
            }

            override fun onCloseBleComplete() {}

            override fun onError(error: Throwable?) {
                if (isDebugBuild()) Log.d(TAG, "Commissioning error", error)
                logDebug("Error: ${error?.message}")
                activity?.runOnUiThread {
                    commissioningNodeId = null
                    hideCommissioningProgress()
                    val msg = error?.message ?: "Unknown error"
                    val userMessage = if (msg.contains("Integrity check failed", ignoreCase = true)) {
                        getString(R.string.setup_code_integrity_failed)
                    } else {
                        msg
                    }
                    onCommissioningFailed(userMessage)
                }
            }

            override fun onConnectDeviceComplete() {}
            override fun onReadCommissioningInfo(vendorId: Int, productId: Int, wifiEndpointId: Int, threadEndpointId: Int) {
                logDebug("ReadCommissioningInfo: vendorId=$vendorId productId=$productId")
            }
            override fun onCommissioningStatusUpdate(nId: Long, stage: String, errorCode: Long) {
                logDebug("CommissioningStatusUpdate: nodeId=$nId stage=$stage errorCode=$errorCode")
            }
            override fun onCommissioningStageStart(nId: Long, stage: String) {
                logDebug("CommissioningStageStart: nodeId=$nId stage=$stage")
            }
            override fun onOpCSRGenerationComplete(csr: ByteArray?) {}
            override fun onICDRegistrationInfoRequired() {}
            override fun onICDRegistrationComplete(errorCode: Long, icdNodeInfo: chip.devicecontroller.ICDDeviceInfo?) {}
        })

        try {
            val params = CommissionParameters.Builder()
                .setCsrNonce(null)
                .setNetworkCredentials(null)
                .setICDRegistrationInfo(null)
                .build()
            // discoverOnce=false: allow rediscovery if needed; useOnlyOnNetworkDiscovery=false: use BLE to find device (required for most commissioning)
            deviceController.pairDeviceWithCode(
                nodeId,
                setupCodeOnly,
                false,  // discoverOnce
                useNetworkOnly,  // useOnlyOnNetworkDiscovery - true when "Commission via network only" is checked
                params
            )
            logDebug("pairDeviceWithCode invoked (networkOnly=$useNetworkOnly)")
        } catch (e: Exception) {
            Log.e(TAG, "Failed to start commissioning", e)
            commissioningNodeId = null
            hideCommissioningProgress()
            onCommissioningFailed(e.message ?: "Failed to start commissioning")
        }
    }

    private fun showCommissioningProgress() {
        binding.statusCard.visibility = View.VISIBLE
        binding.commissioningProgress.visibility = View.VISIBLE
        binding.statusIcon.visibility = View.GONE
        binding.statusText.text = getString(R.string.commissioning_in_progress)
        binding.statusText.setTextColor(
            ContextCompat.getColor(requireContext(), R.color.on_surface)
        )
        binding.btnCommission.isEnabled = false
        binding.btnStopCommissioning.visibility = View.VISIBLE
    }

    private fun hideCommissioningProgress() {
        binding.commissioningProgress.visibility = View.GONE
        binding.btnCommission.isEnabled = true
        binding.btnStopCommissioning.visibility = View.GONE
    }

    private fun onCommissioningSuccess(nodeId: Long, setupCode: String, discriminator: Int) {
        val device = CameraDevice(
            nodeId = nodeId,
            name = "Device #$nodeId",
            setupCode = setupCode.toLongOrNull() ?: 0L,
            discriminator = discriminator,
            isConnected = true
        )
        DeviceRepository.addDevice(requireContext(), device)

        logDebug("Commissioning success: nodeId=$nodeId")
        binding.statusText.text = getString(R.string.commissioning_success)
        binding.statusText.setTextColor(
            ContextCompat.getColor(requireContext(), R.color.success)
        )
        Toast.makeText(requireContext(), getString(R.string.commissioning_success), Toast.LENGTH_LONG).show()
    }

    private fun onCommissioningFailed(error: String) {
        binding.statusText.text = getString(R.string.commissioning_failed, error)
        binding.statusText.setTextColor(
            ContextCompat.getColor(requireContext(), R.color.error)
        )
    }

    override fun onRequestPermissionsResult(
        requestCode: Int,
        permissions: Array<out String>,
        grantResults: IntArray
    ) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode == CAMERA_PERMISSION_CODE && grantResults.isNotEmpty() &&
            grantResults[0] == PackageManager.PERMISSION_GRANTED
        ) {
            startQrScanner()
        } else {
            Toast.makeText(
                requireContext(),
                getString(R.string.camera_permission_required),
                Toast.LENGTH_SHORT
            ).show()
        }
    }

    override fun onDestroyView() {
        super.onDestroyView()
        _binding = null
    }
}
