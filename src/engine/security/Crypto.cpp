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

#define _CRT_RAND_S      // must precede <cstdlib> to expose rand_s
#include <cstdlib>

#include "engine/security/Crypto.hpp"

#include <cstring>
#include <vector>

// TweetNaCl is C; its declarations must be pulled in under C linkage so the names match the archive.
extern "C"
{
#include "tweetnacl.h"
}

// TweetNaCl declares `extern void randombytes(unsigned char*, unsigned long long)` and expects the
// host to define it. It is only reached through crypto_sign_keypair (the wxl-sign tool); the DLL
// never generates keys, yet still links this definition so the symbol resolves in every binary that
// pulls in tweetnacl.c. rand_s is the CSPRNG-backed Windows CRT source (needs _CRT_RAND_S above).
extern "C" void randombytes(unsigned char* p, unsigned long long n)
{
    unsigned long long i = 0;
    while (i < n)
    {
        unsigned int      v     = 0;
        (void)rand_s(&v); // best-effort; rand_s only fails on a broken CRT, and the DLL never calls this
        const unsigned long long chunk = (n - i < 4) ? (n - i) : 4;
        std::memcpy(p + i, &v, static_cast<size_t>(chunk));
        i += chunk;
    }
}

namespace wxl::security
{
    namespace
    {
        int HexNibble(char c)
        {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        }
    } // namespace

    std::array<uint8_t, 64> Sha512(const void* data, size_t len)
    {
        std::array<uint8_t, 64> out{};
        crypto_hash(out.data(), static_cast<const unsigned char*>(data),
                    static_cast<unsigned long long>(len));
        return out;
    }

    std::string ToHex(const uint8_t* p, size_t n)
    {
        static const char kDigits[] = "0123456789abcdef";
        std::string       s(n * 2, '0');
        for (size_t i = 0; i < n; ++i)
        {
            s[2 * i]     = kDigits[p[i] >> 4];
            s[2 * i + 1] = kDigits[p[i] & 0x0F];
        }
        return s;
    }

    bool FromHex(const std::string& hex, uint8_t* out, size_t n)
    {
        if (hex.size() != n * 2)
            return false;
        for (size_t i = 0; i < n; ++i)
        {
            const int hi = HexNibble(hex[2 * i]);
            const int lo = HexNibble(hex[2 * i + 1]);
            if (hi < 0 || lo < 0)
                return false;
            out[i] = static_cast<uint8_t>((hi << 4) | lo);
        }
        return true;
    }

    bool VerifyDetached(const uint8_t pubkey[32], const void* msg, size_t msglen, const uint8_t sig[64])
    {
        // crypto_sign_open verifies the attached form sm = sig(64) || msg and writes the recovered
        // message into a buffer of the same size. Reject unless it returns 0 AND the recovered length
        // matches the message we handed in, so a truncated or extended forgery cannot slip through.
        const unsigned long long smlen = 64ull + static_cast<unsigned long long>(msglen);
        std::vector<unsigned char> sm(static_cast<size_t>(smlen));
        std::memcpy(sm.data(), sig, 64);
        if (msglen)
            std::memcpy(sm.data() + 64, msg, msglen);

        std::vector<unsigned char> recovered(static_cast<size_t>(smlen));
        unsigned long long         mlen = 0;
        const int rc = crypto_sign_open(recovered.data(), &mlen, sm.data(), smlen, pubkey);
        return rc == 0 && mlen == static_cast<unsigned long long>(msglen);
    }

    void GenerateKeypair(uint8_t pk[32], uint8_t sk[64])
    {
        crypto_sign_keypair(pk, sk);
    }

    void SignDetached(const uint8_t sk[64], const void* msg, size_t msglen, uint8_t out_sig[64])
    {
        const unsigned long long n = static_cast<unsigned long long>(msglen);
        std::vector<unsigned char> sm(static_cast<size_t>(64ull + n));
        unsigned long long         smlen = 0;
        crypto_sign(sm.data(), &smlen, static_cast<const unsigned char*>(msg), n, sk);
        std::memcpy(out_sig, sm.data(), 64); // detached sig is the leading 64 bytes of the attached form
    }
}
