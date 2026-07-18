// Thin C++ wrapper over the vendored TweetNaCl: Ed25519 verify/sign, SHA-512, hex, RNG.
// Copyright (C) 2026 WarcraftXL
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

/// Cryptographic helpers for the signed-extension gate. Every primitive here is TweetNaCl underneath
/// (public-domain, vendored in deps/tweetnacl); nothing in this project rolls its own crypto. The
/// verify + hash surface links into WarcraftXL.dll (which only ever checks signatures); the keygen +
/// sign surface additionally links into the wxl-sign tool (the DLL never calls it). All functions are
/// non-throwing: failures are reported by return value, never by exception.
namespace wxl::security
{
    /**
     * @brief SHA-512 of a byte range.
     * @param data  first byte (may be null only when len == 0).
     * @param len   number of bytes.
     * @return the 64-byte digest.
     */
    std::array<uint8_t, 64> Sha512(const void* data, size_t len);

    /**
     * @brief Lower-case hex encoding of n bytes (2*n characters, no separators).
     * @param p  first byte.
     * @param n  byte count.
     */
    std::string ToHex(const uint8_t* p, size_t n);

    /**
     * @brief Decodes exactly 2*n hex characters into n bytes.
     * @param hex  input; must be exactly 2*n chars of [0-9a-fA-F].
     * @param out  receives n bytes.
     * @param n    expected byte count.
     * @return false on wrong length or any non-hex character (out is left partially written).
     */
    bool FromHex(const std::string& hex, uint8_t* out, size_t n);

    /**
     * @brief Verifies a detached Ed25519 signature over a message.
     *
     * Rebuilds the attached form sm = sig(64) || msg that crypto_sign_open expects, and requires both
     * a valid signature and a recovered length equal to msglen. Never throws; returns false on any
     * allocation or verification failure.
     * @param pubkey  32-byte Ed25519 public key.
     * @param msg     signed message bytes (may be null only when msglen == 0).
     * @param msglen  message length.
     * @param sig     64-byte detached signature.
     * @return true iff the signature is valid for (pubkey, msg).
     */
    bool VerifyDetached(const uint8_t pubkey[32], const void* msg, size_t msglen, const uint8_t sig[64]);

    /**
     * @brief Generates a fresh Ed25519 keypair (tool-only; the DLL never calls this).
     * @param pk  receives the 32-byte public key.
     * @param sk  receives the 64-byte secret key.
     */
    void GenerateKeypair(uint8_t pk[32], uint8_t sk[64]);

    /**
     * @brief Produces a detached Ed25519 signature over a message (tool-only).
     * @param sk       64-byte secret key.
     * @param msg      message bytes (may be null only when msglen == 0).
     * @param msglen   message length.
     * @param out_sig  receives the 64-byte detached signature.
     */
    void SignDetached(const uint8_t sk[64], const void* msg, size_t msglen, uint8_t out_sig[64]);
}
