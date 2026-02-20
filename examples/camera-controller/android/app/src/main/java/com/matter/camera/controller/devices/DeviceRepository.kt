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

import android.content.Context
import android.content.SharedPreferences
import com.google.gson.Gson
import com.google.gson.reflect.TypeToken

/**
 * Simple persistence layer for paired camera devices using SharedPreferences + Gson.
 */
object DeviceRepository {
    private const val PREFS_NAME = "camera_devices"
    private const val KEY_DEVICES = "paired_devices"
    private val gson = Gson()

    private fun getPrefs(context: Context): SharedPreferences =
        context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)

    fun getDevices(context: Context): MutableList<CameraDevice> {
        val json = getPrefs(context).getString(KEY_DEVICES, null) ?: return mutableListOf()
        val type = object : TypeToken<MutableList<CameraDevice>>() {}.type
        return gson.fromJson(json, type)
    }

    fun addDevice(context: Context, device: CameraDevice) {
        val devices = getDevices(context)
        devices.removeAll { it.nodeId == device.nodeId }
        devices.add(device)
        saveDevices(context, devices)
    }

    fun removeDevice(context: Context, nodeId: Long) {
        val devices = getDevices(context)
        devices.removeAll { it.nodeId == nodeId }
        saveDevices(context, devices)
    }

    fun getDevice(context: Context, nodeId: Long): CameraDevice? =
        getDevices(context).find { it.nodeId == nodeId }

    fun updateDevice(context: Context, device: CameraDevice) {
        val devices = getDevices(context)
        val index = devices.indexOfFirst { it.nodeId == device.nodeId }
        if (index >= 0) {
            devices[index] = device
            saveDevices(context, devices)
        }
    }

    private fun saveDevices(context: Context, devices: List<CameraDevice>) {
        getPrefs(context).edit()
            .putString(KEY_DEVICES, gson.toJson(devices))
            .apply()
    }
}
