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

import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.appcompat.app.AlertDialog
import androidx.fragment.app.Fragment
import androidx.navigation.fragment.findNavController
import androidx.recyclerview.widget.LinearLayoutManager
import com.matter.camera.controller.R
import com.matter.camera.controller.databinding.FragmentDevicesBinding

class DevicesFragment : Fragment() {

    private var _binding: FragmentDevicesBinding? = null
    private val binding get() = _binding!!

    private lateinit var adapter: DeviceAdapter

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?
    ): View {
        _binding = FragmentDevicesBinding.inflate(inflater, container, false)
        return binding.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)

        adapter = DeviceAdapter(
            onLiveViewClick = { device -> navigateToLiveView(device) },
            onLongClick = { device -> showRemoveDialog(device) }
        )

        binding.devicesRecyclerView.layoutManager = LinearLayoutManager(requireContext())
        binding.devicesRecyclerView.adapter = adapter

        binding.swipeRefresh.setOnRefreshListener {
            loadDevices()
            binding.swipeRefresh.isRefreshing = false
        }

        loadDevices()
    }

    override fun onResume() {
        super.onResume()
        loadDevices()
    }

    private fun loadDevices() {
        val devices = DeviceRepository.getDevices(requireContext())
        adapter.submitList(devices)

        if (devices.isEmpty()) {
            binding.emptyState.visibility = View.VISIBLE
            binding.devicesRecyclerView.visibility = View.GONE
        } else {
            binding.emptyState.visibility = View.GONE
            binding.devicesRecyclerView.visibility = View.VISIBLE
        }
    }

    private fun navigateToLiveView(device: CameraDevice) {
        val navController = findNavController()
        val bundle = Bundle().apply {
            putLong("nodeId", device.nodeId)
        }
        navController.navigate(R.id.nav_live_view, bundle)
    }

    private fun showRemoveDialog(device: CameraDevice) {
        AlertDialog.Builder(requireContext())
            .setTitle(R.string.remove_device)
            .setMessage(R.string.remove_device_confirm)
            .setPositiveButton(R.string.yes) { _, _ ->
                DeviceRepository.removeDevice(requireContext(), device.nodeId)
                loadDevices()
            }
            .setNegativeButton(R.string.no, null)
            .show()
    }

    override fun onDestroyView() {
        super.onDestroyView()
        _binding = null
    }
}
