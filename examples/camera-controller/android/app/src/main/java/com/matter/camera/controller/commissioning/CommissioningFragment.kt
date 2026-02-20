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
import chip.devicecontroller.NetworkCredentials
import com.google.mlkit.vision.barcode.BarcodeScanning
import com.google.mlkit.vision.common.InputImage
import com.matter.camera.controller.ChipClient
import com.matter.camera.controller.R
import com.matter.camera.controller.databinding.FragmentCommissioningBinding
import com.matter.camera.controller.devices.CameraDevice
import com.matter.camera.controller.devices.DeviceRepository
import java.util.concurrent.Executors

/**
 * Fragment handling Matter camera device commissioning.
 * Supports both QR code scanning and manual code entry for pairing.
 */
class CommissioningFragment : Fragment() {

    private var _binding: FragmentCommissioningBinding? = null
    private val binding get() = _binding!!

    private var isScannerActive = false

    companion object {
        private const val TAG = "CommissioningFragment"
        private const val CAMERA_PERMISSION_CODE = 101
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
        // Matter QR codes typically contain the setup payload
        // For now, extract the setup code and populate the fields
        Log.i(TAG, "Processing QR code: $qrCode")
        Toast.makeText(requireContext(), "QR code scanned: $qrCode", Toast.LENGTH_SHORT).show()
        binding.inputSetupCode.setText(qrCode)
    }

    private fun startCommissioning() {
        val nodeIdText = binding.inputNodeId.text?.toString()
        val setupCodeText = binding.inputSetupCode.text?.toString()
        val discriminatorText = binding.inputDiscriminator.text?.toString()

        if (nodeIdText.isNullOrBlank()) {
            binding.inputNodeId.error = "Required"
            return
        }

        if (setupCodeText.isNullOrBlank()) {
            binding.inputSetupCode.error = "Required"
            return
        }

        val nodeId = nodeIdText.toLongOrNull() ?: run {
            binding.inputNodeId.error = "Invalid number"
            return
        }

        val setupCode = setupCodeText.toLongOrNull() ?: run {
            binding.inputSetupCode.error = "Invalid code"
            return
        }

        val discriminator = discriminatorText?.toIntOrNull()

        showCommissioningProgress()

        val deviceController = ChipClient.getDeviceController(requireContext())
        deviceController.setCompletionListener(object : ChipDeviceController.CompletionListener {
            override fun onCommissioningComplete(nodeId: Long, errorCode: Long) {
                activity?.runOnUiThread {
                    if (errorCode == 0L) {
                        onCommissioningSuccess(nodeId, setupCode, discriminator ?: 0)
                    } else {
                        onCommissioningFailed("Error code: $errorCode")
                    }
                }
            }

            override fun onStatusUpdate(status: Int) {
                Log.i(TAG, "Commissioning status: $status")
            }

            override fun onPairingComplete(errorCode: Long) {
                Log.i(TAG, "Pairing complete: errorCode=$errorCode")
            }

            override fun onPairingDeleted(errorCode: Long) {
                Log.i(TAG, "Pairing deleted: errorCode=$errorCode")
            }

            override fun onNotifyChipConnectionClosed() {
                Log.i(TAG, "Connection closed")
            }

            override fun onCloseBleComplete() {}
            override fun onError(error: Throwable?) {
                activity?.runOnUiThread {
                    onCommissioningFailed(error?.message ?: "Unknown error")
                }
            }

            override fun onFirmwareVersionInfo(p0: Int, p1: String?, p2: Long, p3: String?) {}
            override fun onOpCSRGenerationComplete(csr: ByteArray?) {}
            override fun onReadCommissioningInfo(p0: Int, p1: Int, p2: Int, p3: Int) {}
            override fun onICDRegistrationInfoRequired() {}
            override fun onICDRegistrationComplete(errorCode: Long, icdNodeInfo: chip.devicecontroller.ICDDeviceInfo?) {}
        })

        try {
            if (discriminator != null) {
                deviceController.pairDeviceWithAddress(
                    nodeId,
                    "0.0.0.0",
                    5540,
                    discriminator,
                    setupCode,
                    null
                )
            } else {
                // On-network commissioning
                deviceController.pairDevice(
                    nodeId,
                    5540,
                    setupCode.toInt(),
                    null,
                    NetworkCredentials()
                )
            }
        } catch (e: Exception) {
            Log.e(TAG, "Failed to start commissioning", e)
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
    }

    private fun onCommissioningSuccess(nodeId: Long, setupCode: Long, discriminator: Int) {
        val device = CameraDevice(
            nodeId = nodeId,
            name = "Camera #$nodeId",
            setupCode = setupCode,
            discriminator = discriminator,
            isConnected = true
        )
        DeviceRepository.addDevice(requireContext(), device)

        binding.commissioningProgress.visibility = View.GONE
        binding.statusText.text = getString(R.string.commissioning_success)
        binding.statusText.setTextColor(
            ContextCompat.getColor(requireContext(), R.color.success)
        )
        binding.btnCommission.isEnabled = true

        Toast.makeText(requireContext(), getString(R.string.commissioning_success), Toast.LENGTH_LONG).show()
    }

    private fun onCommissioningFailed(error: String) {
        binding.commissioningProgress.visibility = View.GONE
        binding.statusText.text = getString(R.string.commissioning_failed, error)
        binding.statusText.setTextColor(
            ContextCompat.getColor(requireContext(), R.color.error)
        )
        binding.btnCommission.isEnabled = true
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
