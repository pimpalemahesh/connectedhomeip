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

import android.view.LayoutInflater
import android.view.ViewGroup
import androidx.core.content.ContextCompat
import androidx.recyclerview.widget.DiffUtil
import androidx.recyclerview.widget.ListAdapter
import androidx.recyclerview.widget.RecyclerView
import com.matter.camera.controller.R
import com.matter.camera.controller.databinding.ItemDeviceBinding

class DeviceAdapter(
    private val onLiveViewClick: (CameraDevice) -> Unit,
    private val onLongClick: (CameraDevice) -> Unit
) : ListAdapter<CameraDevice, DeviceAdapter.DeviceViewHolder>(DeviceDiffCallback()) {

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): DeviceViewHolder {
        val binding = ItemDeviceBinding.inflate(
            LayoutInflater.from(parent.context), parent, false
        )
        return DeviceViewHolder(binding)
    }

    override fun onBindViewHolder(holder: DeviceViewHolder, position: Int) {
        holder.bind(getItem(position))
    }

    inner class DeviceViewHolder(
        private val binding: ItemDeviceBinding
    ) : RecyclerView.ViewHolder(binding.root) {

        fun bind(device: CameraDevice) {
            binding.deviceName.text = device.name
            binding.deviceNodeId.text =
                binding.root.context.getString(R.string.device_node_id, device.displayNodeId)

            val statusColor = if (device.isStreaming) {
                ContextCompat.getColor(binding.root.context, R.color.status_streaming)
            } else {
                ContextCompat.getColor(binding.root.context, R.color.status_connected)
            }
            binding.statusIndicator.background?.mutate()?.setTint(statusColor)
            binding.deviceStatus.setTextColor(statusColor)
            binding.deviceStatus.text = if (device.isStreaming) {
                binding.root.context.getString(R.string.streaming)
            } else {
                binding.root.context.getString(R.string.device_status_connected)
            }

            binding.btnLiveView.setOnClickListener { onLiveViewClick(device) }
            binding.root.setOnLongClickListener {
                onLongClick(device)
                true
            }
        }
    }

    class DeviceDiffCallback : DiffUtil.ItemCallback<CameraDevice>() {
        override fun areItemsTheSame(oldItem: CameraDevice, newItem: CameraDevice) =
            oldItem.nodeId == newItem.nodeId

        override fun areContentsTheSame(oldItem: CameraDevice, newItem: CameraDevice) =
            oldItem == newItem
    }
}
