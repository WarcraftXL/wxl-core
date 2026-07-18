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

#include "engine/security/Manifest.hpp"

#include "engine/security/Crypto.hpp"
#include "engine/security/PublicKey.hpp"

#include <windows.h>

// LuaJIT is a C library; luajit.h (generated into the LuaJIT build's src dir, which the DLL and the
// tool both add to their include path) supplies the LUAJIT_VERSION string the manifest is bound to.
extern "C"
{
#include "luajit.h"
}

#include <cstdint>
#include <vector>

namespace wxl::security
{
    namespace
    {
        /// Reads an entire file into a byte buffer. Returns false (empty out) on any error.
        bool ReadWholeFile(const std::wstring& path, std::vector<uint8_t>& out)
        {
            HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h == INVALID_HANDLE_VALUE)
                return false;

            LARGE_INTEGER size{};
            if (!GetFileSizeEx(h, &size) || size.QuadPart < 0 || size.QuadPart > 0x7FFFFFFF)
            {
                CloseHandle(h);
                return false;
            }
            out.resize(static_cast<size_t>(size.QuadPart));

            size_t total = 0;
            while (total < out.size())
            {
                DWORD       got   = 0;
                const DWORD chunk = static_cast<DWORD>(out.size() - total);
                if (!ReadFile(h, out.data() + total, chunk, &got, nullptr) || got == 0)
                {
                    CloseHandle(h);
                    out.clear();
                    return false;
                }
                total += got;
            }
            CloseHandle(h);
            return true;
        }

        /// Lower-cases ASCII in place; used to make path keys case-insensitive (Windows semantics).
        std::string ToLowerAscii(std::string s)
        {
            for (char& c : s)
                if (c >= 'A' && c <= 'Z')
                    c = static_cast<char>(c - 'A' + 'a');
            return s;
        }

        /// Splits `text` on '\n', dropping a single trailing '\r' per line so a CRLF file still parses.
        std::vector<std::string> SplitLines(const std::string& text)
        {
            std::vector<std::string> lines;
            std::string              cur;
            for (const char c : text)
            {
                if (c == '\n')
                {
                    if (!cur.empty() && cur.back() == '\r')
                        cur.pop_back();
                    lines.push_back(std::move(cur));
                    cur.clear();
                }
                else
                {
                    cur.push_back(c);
                }
            }
            if (!cur.empty())
            {
                if (cur.back() == '\r')
                    cur.pop_back();
                lines.push_back(std::move(cur));
            }
            return lines;
        }

        /// Parses the verified manifest text. Strict: any deviation sets err and returns false.
        bool Parse(const std::vector<uint8_t>& bytes, Manifest& out, std::string& err)
        {
            const std::string       text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            std::vector<std::string> lines = SplitLines(text);
            if (lines.size() < 2)
            {
                err = "manifest too short";
                return false;
            }
            if (lines[0] != "wxl-manifest 1")
            {
                err = "bad manifest header";
                return false;
            }
            const std::string kLuajit = "luajit ";
            if (lines[1].compare(0, kLuajit.size(), kLuajit) != 0)
            {
                err = "missing luajit version line";
                return false;
            }
            out.luajitVersion = lines[1].substr(kLuajit.size());
            out.files.clear();

            for (size_t i = 2; i < lines.size(); ++i)
            {
                const std::string& line = lines[i];
                if (line.empty())
                    continue; // tolerate a trailing blank line
                // Format: <128 hex> <space> <relpath>. The path may itself contain spaces, so split
                // only on the first space after the fixed-width hash.
                if (line.size() < 130 || line[128] != ' ')
                {
                    err = "malformed manifest entry";
                    return false;
                }
                std::array<uint8_t, 64> hash{};
                if (!FromHex(line.substr(0, 128), hash.data(), 64))
                {
                    err = "bad hash in manifest entry";
                    return false;
                }
                const std::string rel = ToLowerAscii(line.substr(129));
                if (rel.empty())
                {
                    err = "empty path in manifest entry";
                    return false;
                }
                out.files[rel] = hash;
            }
            return true;
        }
    } // namespace

    bool LoadAndVerify(const std::wstring& extDir, Manifest& out, std::string& err)
    {
        if (!kHasSigningKey)
        {
            err = "no signing key configured";
            return false;
        }

        std::wstring base = extDir;
        if (!base.empty() && base.back() != L'\\' && base.back() != L'/')
            base.push_back(L'\\');

        std::vector<uint8_t> manifestBytes;
        if (!ReadWholeFile(base + L"manifest", manifestBytes))
        {
            err = "manifest not found";
            return false;
        }

        std::vector<uint8_t> sigText;
        if (!ReadWholeFile(base + L"manifest.sig", sigText))
        {
            err = "manifest.sig not found";
            return false;
        }
        // The sig file is 128 hex chars; tolerate trailing whitespace/newlines an editor may add.
        std::string sigHex(reinterpret_cast<const char*>(sigText.data()), sigText.size());
        while (!sigHex.empty() && (sigHex.back() == '\n' || sigHex.back() == '\r' ||
                                   sigHex.back() == ' ' || sigHex.back() == '\t'))
            sigHex.pop_back();

        uint8_t sig[64];
        if (!FromHex(sigHex, sig, 64))
        {
            err = "manifest.sig is not 128 hex characters";
            return false;
        }

        if (!VerifyDetached(kEd25519PublicKey, manifestBytes.data(), manifestBytes.size(), sig))
        {
            err = "manifest signature invalid";
            return false;
        }

        return Parse(manifestBytes, out, err);
    }

    bool VerifyFile(const Manifest& m, const std::wstring& fullPath, const std::string& relPath,
                    std::string& err)
    {
        const auto it = m.files.find(ToLowerAscii(relPath));
        if (it == m.files.end())
        {
            err = "not listed in manifest";
            return false;
        }

        std::vector<uint8_t> bytes;
        if (!ReadWholeFile(fullPath, bytes))
        {
            err = "cannot read file";
            return false;
        }

        const std::array<uint8_t, 64> digest = Sha512(bytes.data(), bytes.size());
        if (digest != it->second)
        {
            err = "hash mismatch";
            return false;
        }
        return true;
    }

    bool LuaJitVersionMatches(const Manifest& m)
    {
        return m.luajitVersion == LUAJIT_VERSION;
    }
}
