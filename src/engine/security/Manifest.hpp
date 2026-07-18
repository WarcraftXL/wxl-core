// Signed extension manifest: the Ed25519-authenticated list of file hashes that gates loading.
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
#include <cstdint>
#include <string>
#include <unordered_map>

/// The signed manifest is the trust root for extension loading. On disk it is two files in the
/// extensions directory:
///
///   manifest      — UTF-8 text, LF line endings, no trailing spaces:
///                      wxl-manifest 1
///                      luajit <LUAJIT_VERSION string>
///                      <sha512hex(128)> <relative/path/with/forward/slashes>
///                      ...                             (one line per *.lua / *.out file)
///   manifest.sig  — 128 hex characters = the 64-byte detached Ed25519 signature over the exact
///                   bytes of `manifest`, made with the secret key whose public half is baked into
///                   the DLL (PublicKey.hpp).
///
/// Paths are stored forward-slashed and compared case-insensitively (Windows semantics), so the map
/// keys are lower-cased. The parser is strict: any structural deviation is a parse error, never a
/// silent skip. Producing side (identical byte format) lives in the wxl-sign tool.
namespace wxl::security
{
    struct Manifest
    {
        std::string                                                    luajitVersion;
        std::unordered_map<std::string, std::array<uint8_t, 64>>       files; // lower-cased relpath -> sha512
    };

    /**
     * @brief Reads and cryptographically verifies `<extDir>/manifest` (+ .sig), then parses it.
     *
     * Fails closed: returns false with `err` set on a missing manifest/sig, a placeholder public key
     * (kHasSigningKey == false -> "no signing key configured"), a signature that does not verify, or
     * any parse error. On success `out` holds the LuaJIT version and the path->hash table.
     * @param extDir  extensions directory (wide path).
     * @param out     receives the parsed manifest on success.
     * @param err     receives a human-readable reason on failure.
     * @return true iff the manifest verified and parsed.
     */
    bool LoadAndVerify(const std::wstring& extDir, Manifest& out, std::string& err);

    /**
     * @brief Confirms one file's bytes match its manifest entry.
     *
     * SHA-512s the file at fullPath and compares to the manifest hash recorded for relPath. Fails
     * when relPath is absent from the manifest ("not listed in manifest"), when the file cannot be
     * read, or when the digest differs ("hash mismatch").
     * @param m        a manifest returned by LoadAndVerify.
     * @param fullPath absolute path of the file to hash (wide path).
     * @param relPath  the file's manifest-relative path (forward slashes; case-insensitive).
     * @param err      receives a human-readable reason on failure.
     * @return true iff the file is listed and its hash matches.
     */
    bool VerifyFile(const Manifest& m, const std::wstring& fullPath, const std::string& relPath, std::string& err);

    /**
     * @brief True when the manifest's declared LuaJIT version equals the DLL's embedded LUAJIT_VERSION.
     *
     * Callers apply the policy: a mismatch is a hard failure for compiled `.out` bytecode (ABI-bound
     * to the exact LuaJIT build) but only a warning for `.lua` source (recompiled by this engine).
     */
    bool LuaJitVersionMatches(const Manifest& m);
}
