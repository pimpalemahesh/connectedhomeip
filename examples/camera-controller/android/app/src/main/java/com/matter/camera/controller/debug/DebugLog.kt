/*
 *   Copyright (c) 2025 Project CHIP Authors
 *   All rights reserved.
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   See the License for the specific language governing permissions and limitations.
 */
package com.matter.camera.controller.debug

import android.os.Handler
import android.os.Looper
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.concurrent.CopyOnWriteArrayList

/**
 * In-memory log buffer for in-app debug viewer.
 * When debug mode is enabled, commissioning (and other) logs are appended here
 * and can be shown in the Debug log screen.
 */
object DebugLog {
    private const val MAX_LINES = 500
    private val dateFormat = SimpleDateFormat("HH:mm:ss.SSS", Locale.US)
    private val lines = CopyOnWriteArrayList<String>()
    private val mainHandler = Handler(Looper.getMainLooper())

    @Volatile
    var isEnabled: Boolean = false
        private set

    private val listeners = CopyOnWriteArrayList<OnLogUpdatedListener>()

    fun setEnabled(enabled: Boolean) {
        isEnabled = enabled
        if (enabled) add("Debug mode enabled")
    }

    fun add(message: String) {
        if (!isEnabled) return
        val timestamp = dateFormat.format(Date())
        val line = "$timestamp | $message"
        synchronized(lines) {
            lines.add(line)
            while (lines.size > MAX_LINES) lines.removeAt(0)
        }
        mainHandler.post { listeners.forEach { it.onLogUpdated() } }
    }

    fun getLines(): List<String> = synchronized(lines) { lines.toList() }

    fun clear() {
        synchronized(lines) { lines.clear() }
        mainHandler.post { listeners.forEach { it.onLogUpdated() } }
    }

    fun addListener(listener: OnLogUpdatedListener) {
        listeners.add(listener)
    }

    fun removeListener(listener: OnLogUpdatedListener) {
        listeners.remove(listener)
    }

    interface OnLogUpdatedListener {
        fun onLogUpdated()
    }
}
