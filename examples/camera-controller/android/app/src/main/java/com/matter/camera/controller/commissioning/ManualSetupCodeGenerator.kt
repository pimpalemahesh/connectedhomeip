/*
 *   Copyright (c) 2025 Project CHIP Authors
 *   All rights reserved.
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   See the License for the specific language governing permissions and limitations.
 */
package com.matter.camera.controller.commissioning

/**
 * Generates Matter manual setup code (11-digit decimal string with Verhoeff check digit)
 * from setup passcode (e.g. 20202021) and short discriminator (0-15).
 * Matches ManualSetupPayloadGenerator logic for short code (standard commissioning flow).
 */
object ManualSetupCodeGenerator {

    private const val BASE = 10
    private const val POLYGON_SIZE = 5

    private val sMultiplyTable = intArrayOf(
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 1, 2, 3, 4, 0, 6, 7, 8, 9, 5, 2, 3, 4, 0, 1, 7, 8, 9, 5, 6, 3, 4, 0, 1,
        2, 8, 9, 5, 6, 7, 4, 0, 1, 2, 3, 9, 5, 6, 7, 8, 5, 9, 8, 7, 6, 0, 4, 3, 2, 1, 6, 5, 9, 8, 7, 1, 0, 4,
        3, 2, 7, 6, 5, 9, 8, 2, 1, 0, 4, 3, 8, 7, 6, 5, 9, 3, 2, 1, 0, 4, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0
    )

    private val sPermTable = intArrayOf(1, 5, 7, 6, 2, 8, 3, 0, 9, 4)

    private fun permute(`val`: Int, iterCount: Long): Int {
        var v = `val` % BASE
        var i = iterCount
        while (i > 0) {
            v = sPermTable[v]
            i--
        }
        return v
    }

    private fun dihedralInvert(`val`: Int, n: Int): Int =
        if (`val` in 1 until n) n - `val` else `val`

    private fun computeCheckChar(str: String): Char {
        var c = 0
        for (i in 1..str.length) {
            val ch = str[str.length - i]
            val digit = ch.digitToIntOrNull() ?: return '0'
            val p = permute(digit, i.toLong())
            c = sMultiplyTable[c * BASE + p]
        }
        c = dihedralInvert(c, POLYGON_SIZE)
        return '0' + c
    }

    /**
     * Generate 11-digit manual pairing code from setup passcode and discriminator.
     * @param setupPasscode 8-digit passcode (e.g. 20202021), 1..99999998
     * @param discriminator Short discriminator 0-15
     * @return 11-character string (10 digits + Verhoeff check digit), or null if invalid
     */
    fun generate(setupPasscode: Long, discriminator: Int): String? {
        if (setupPasscode <= 0 || setupPasscode > 99_999_998) return null
        if (discriminator !in 0..15) return null
        val pin = setupPasscode.toInt() and 0x07FFFFFF
        val disc = discriminator and 0xF
        // Chunk1: 1 digit - discriminator ms 2 bits, vidPidPresent=0 (standard flow)
        val chunk1 = (disc shr 2) and 3
        // Chunk2: 5 digits - pin low 14 bits at pos 0, disc low 2 bits at pos 14
        val chunk2 = (pin and 0x3FFF) or ((disc and 3) shl 14)
        // Chunk3: 4 digits - pin high 13 bits
        val chunk3 = (pin shr 14) and 0x1FFF
        val withoutCheck = String.format("%01d%05d%04d", chunk1, chunk2, chunk3)
        return withoutCheck + computeCheckChar(withoutCheck)
    }
}
