/*
 *   Copyright (c) 2025 Project CHIP Authors
 *   All rights reserved.
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   See the License for the specific language governing permissions and limitations.
 */
package com.matter.camera.controller.debug

import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.ScrollView
import android.widget.TextView
import androidx.fragment.app.DialogFragment
import com.google.android.material.button.MaterialButton
import com.matter.camera.controller.R

/**
 * Full-screen dialog showing the in-app debug log.
 * Updates when new lines are added while the dialog is open.
 */
class DebugLogDialogFragment : DialogFragment() {

    private var logText: TextView? = null
    private var scrollView: ScrollView? = null

    private val logListener = object : DebugLog.OnLogUpdatedListener {
        override fun onLogUpdated() {
            activity?.runOnUiThread { refreshLog() }
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setStyle(STYLE_NORMAL, android.R.style.Theme_Material_Light_NoActionBar)
    }

    override fun onStart() {
        super.onStart()
        dialog?.window?.setLayout(
            ViewGroup.LayoutParams.MATCH_PARENT,
            ViewGroup.LayoutParams.MATCH_PARENT
        )
    }

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?
    ): View {
        val view = inflater.inflate(R.layout.dialog_debug_log, container, false)
        logText = view.findViewById(R.id.debug_log_text)
        scrollView = view.findViewById(R.id.debug_log_scroll)
        view.findViewById<MaterialButton>(R.id.debug_log_clear).setOnClickListener {
            DebugLog.clear()
            refreshLog()
        }
        view.findViewById<MaterialButton>(R.id.debug_log_close).setOnClickListener { dismiss() }
        return view
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        refreshLog()
        DebugLog.addListener(logListener)
    }

    override fun onDestroyView() {
        DebugLog.removeListener(logListener)
        logText = null
        scrollView = null
        super.onDestroyView()
    }

    private fun refreshLog() {
        val lines = DebugLog.getLines()
        val text = if (lines.isEmpty()) getString(R.string.debug_log_empty) else lines.joinToString("\n")
        logText?.text = text
        if (!lines.isEmpty()) {
            scrollView?.post { scrollView?.fullScroll(ScrollView.FOCUS_DOWN) }
        }
    }
}
