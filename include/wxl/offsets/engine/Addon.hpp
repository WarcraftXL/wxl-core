// Addon manifest parsing and interface signature verification, for the target client (335).
// Copyright (C) 2026 WarcraftXL

#pragma once

#include <cstddef>
#include <cstdint>

/**
 * @brief What "## Secure: 1" costs, and what satisfies it.
 *
 * A manifest declaring itself secure is checked against a `.sig` beside it. The declaration alone is
 * not enough and the failure is not local: one unsigned secure manifest fails the WHOLE check, and the
 * client then reports "FrameXML is modified or corrupt" whatever the actual offender was.
 *
 * The scheme is entirely standard cryptography -- MD5, SHA-1, RSA-2048 -- so signing needs no custom
 * implementation, only the right key. Everything below marked VERIFIED was reproduced against the
 * client's own shipped signatures; the rest was read off the binary at the address given.
 */
namespace wxl::offsets::engine::addon
{
    // --- the scheme -------------------------------------------------------------------------------
    //
    //   D     = MD5( manifest bytes || the files of its LOAD TREE, in load order )
    //   m     = SHA1( D || uppercased base name of the .sig )        e.g. "FRAMEXML.TOC.SIG"
    //   buf   = kSignaturePadByte x 256, then m over the head, then kSignaturePadTerminator last
    //   block = buf ^ exponent (mod key)
    //   .sig  = D || kSignatureMagic || block
    //
    // Every bignum is LITTLE-ENDIAN -- SBig's convention -- for the key as it sits in .rdata, the block
    // as it sits in the file, and the buffer alike.
    //
    // THE LOAD TREE, NOT THE MANIFEST. A covered .xml is followed immediately by the files its
    // <Script file=> and <Include file=> name, recursively, in document order. Blizzard_ArenaUI.toc
    // lists two files but its D covers four, the third reached through a <Script>. Two consequences:
    // nothing is DE-DUPLICATED (Blizzard_CombatLog hashes one file's bytes twice, because its .toc and
    // its .xml both name it), and a reference that resolves to nothing beside the referring file is
    // SKIPPED rather than refused (Blizzard_InspectUI includes an .xml that only exists under FrameXML).
    //
    // FrameXML alone appends one further file after its tree -- Interface\FrameXML\Bindings.xml, the
    // second path the caller names, which its manifest never mentions. GlueXML needs no such addition,
    // so this belongs to FrameXML and not to root manifests in general.
    //
    //
    // SHA-1 HERE IS TEXTBOOK SHA-1. The export names the finaliser SHA1Broken and that is a misnomer:
    // kSha1Prepare loads the standard IV, the expansion keeps its rol-1 over
    // W[t-3]^W[t-8]^W[t-14]^W[t-16] (so SHA-1, not SHA-0), and blocks load and digests write big-endian.

    // --- the .sig file ----------------------------------------------------------------------------
    // Exactly kSignatureFileSize bytes; any other size is refused before a single hash is computed.
    //
    //   [0x00 .. 0x0F]  D, the MD5
    //   [0x10 .. 0x13]  kSignatureMagic -- the bytes 4E 47 49 53, so the dword reads 'NGIS'
    //   [0x14 .. 0x113] the RSA-2048 block
    constexpr size_t kSignatureFileSize     = 0x114;
    constexpr size_t kSignatureDigestOffset = 0x00;
    constexpr size_t kSignatureMagicOffset  = 0x10;
    constexpr size_t kSignatureBlockOffset  = 0x14;

    constexpr uint32_t kSignatureMagic         = 0x5349474E;
    constexpr uint8_t  kSignaturePadByte       = 0xBB;
    constexpr uint8_t  kSignaturePadTerminator = 0x0B;
    constexpr uint32_t kSignatureExponent      = 65537;  // read at 0x00A4421C as `01 00 01 00`

    // --- the key ----------------------------------------------------------------------------------
    // Blizzard's 2048-bit modulus, 256 bytes in .rdata, byte-identical to the `addonPublicKey` array
    // AzerothCore sends in SMSG_ADDON_INFO for any addon whose CRC is not STANDARD_ADDON_CRC
    // (WorldSession.cpp). Re-keying the client therefore means re-keying that array in the same pass.
    constexpr uintptr_t kAddonPublicKey     = 0x009E2C28;
    constexpr size_t    kAddonPublicKeySize = 256;

    // --- entry points -----------------------------------------------------------------------------

    /// Verifies one manifest. Body, in order: build "<path>.sig"; load it and refuse any size but
    /// kSignatureFileSize; uppercase the base name; kVerifySignatureBlob; keep the leading 16 bytes;
    /// kComputeInterfaceDigest over the real files; compare the two digests four bytes at a time.
    ///
    /// IT ALWAYS RETURNS 3. Every exit ends on `mov eax, 3`: the missing or wrong-sized .sig at
    /// 0x0081663B, the failed blob verify at 0x00816694, and 0x0081675F, which clobbers the digest
    /// comparison the function has just computed. The statuses below are therefore unreachable in
    /// this build, and no signature this function sees can make it refuse anything.
    constexpr uintptr_t kCheckSignature = 0x008165E0;

    /// Four arguments, identical at both call sites -- CGGameUI::Initialize (0x0052ABB2) and the addon
    /// loop in SetAddOnInfoRequest (0x005F7B41). `out` receives kComputeInterfaceDigest's 16 bytes, and
    /// only on the path where the blob verified; that omission is what the caller's comparison detects.
    using CheckSignatureFn = int(__cdecl*)(const char* tocPath, const char* bindingsPath,
                                           const uint8_t* publicKey, uint8_t* out);

    /// MD5 over the manifest and the files it lists, then over one further file the caller names.
    constexpr uintptr_t kComputeInterfaceDigest = 0x008164D0;
    constexpr uintptr_t kProcessFileForMakeSign = 0x008162B0;

    /// The RSA half. Feeds the digest, then the uppercased file name, then the blob -- which is why a
    /// .sig is bound to its own file name and cannot be moved to another file.
    constexpr uintptr_t kVerifySignatureBlob = 0x00816550;

    // --- SSignature -------------------------------------------------------------------------------
    // The module names itself: kSignatureInit allocates its 0x78-byte context with ".\\SSignature.cpp".
    //
    // kSignatureUpdate is a SLIDING WINDOW rather than a plain hash update: it hashes everything it is
    // handed EXCEPT the trailing kSignatureCtxBlobSize bytes, which it retains as the blob. That is what
    // lets kVerifySignatureBlob feed the whole .sig in three calls and have the split fall out by
    // itself -- the digest and the name are hashed, the magic and the block are kept.
    constexpr uintptr_t kSignatureInit   = 0x00770D50;  // (ctx, modulusBytes = 0x100, 4)
    constexpr uintptr_t kSignatureSize   = 0x00770C90;
    constexpr uintptr_t kSignatureUpdate = 0x00770CA0;
    constexpr uintptr_t kSignatureVerify = 0x00770DB0;

    /// Context layout: [0] modulus bytes, [1] the 4 it is passed, [2] bytes currently held,
    /// [3] the retained window size, [4] the window buffer.
    constexpr size_t kSignatureCtxBlobSize = 0x104;  // 256 + the magic
    constexpr size_t kSignatureCtxBlobPtr  = 0x10;   // byte offset of [4] within the context

    /// What kSignatureVerify demands, read off its body: the blob's first dword must equal
    /// kSignatureMagic, else it refuses before doing any maths; then it fills the buffer with
    /// kSignaturePadByte, sets its last byte to kSignaturePadTerminator, writes the digest over its
    /// head, and compares against the block found after the magic.
    constexpr uintptr_t kSha1Prepare = 0x0077AAA0;
    constexpr uintptr_t kSha1Final   = 0x0077ABA0;
    constexpr uintptr_t kSBigNew     = 0x0077BF20;
    constexpr uintptr_t kSBigDel     = 0x0077C6A0;

    // --- statuses ---------------------------------------------------------------------------------
    // Read at kSignatureStatusJumpTable: its four entries are 0x0052ABE5, 0x0052ABF2, 0x0052ABFF and
    // 0x0052AC1C, and `ja` sends anything above 3 to 0x0052AC12. So 0, 1 and 2 are the failures below;
    // 3 and above print nothing. kCheckSignature returns none of 0, 1 or 2, so these are dead here.
    //
    // WHAT A 3 COSTS AN ADDON. SetAddOnInfoRequest answers a 3 with `record[0x24] = 0`, and LoadAddOn
    // (0x005F80B0) reads a zero there as "compare the digests": it MD5s the files it loads and checks
    // them against record+0x1D2, which kCheckSignature filled only if the blob verified. So a
    // `## Secure: 1` addon with an unverifiable signature reaches ClientPostClose(10), and the client
    // reports the interface as corrupt -- naming FrameXML for a failure that belongs to the addon.
    constexpr uintptr_t kSignatureStatusJumpTable = 0x0052AEB4;
    constexpr uintptr_t kMsgFrameXmlNoSignature   = 0x00A02FB0;  // 0, "FrameXML missing signature"
    constexpr uintptr_t kMsgFrameXmlBadSignature  = 0x00A02F90;  // 1, "FrameXML has corrupt signature"
    constexpr uintptr_t kMsgFrameXmlModified      = 0x00A02F70;  // 2, "FrameXML is modified or corrupt"

    // --- call sites -------------------------------------------------------------------------------
    // Each pushes kAddonPublicKey itself, so re-keying the data alone covers both. They are named here
    // for a detour that wants to answer for one screen and not the other.
    constexpr uintptr_t kGameUiInitialize = 0x0052A980;  // pushes the key at 0x0052ABC2
    constexpr uintptr_t kGlueMgrResume    = 0x004DA5F0;  // pushes the key at 0x004DA7D2

    /// The two paths checked for the in-game interface.
    constexpr uintptr_t kFrameXmlTocPath      = 0x00A02FCC;  // "Interface\FrameXML\FrameXML.toc"
    constexpr uintptr_t kFrameXmlBindingsPath = 0x00A02FEC;  // "Interface\FrameXML\Bindings.xml"

    // --- the manifest parser ----------------------------------------------------------------------

    /// Reads a .toc into the client's addon record. "## Secure:" is matched at 0x005F8D58, with
    /// SStrCmpI against the literal below.
    constexpr uintptr_t kLoadAddOnInfo = 0x005F86A0;
    constexpr uintptr_t kTocSecureKey  = 0x00A1D970;  // "Secure:"

    /// The state an addon lands in, as GetAddOnInfo's seventh return value names it. CORRUPT is what a
    /// "## Secure: 1" manifest with no valid signature earns.
    constexpr uintptr_t kSecurityCorrupt  = 0x00A1D6E8;  // "CORRUPT"
    constexpr uintptr_t kSecurityInsecure = 0x00A1D6F0;  // "INSECURE"
    constexpr uintptr_t kSecuritySecure   = 0x00A1D730;  // "SECURE"
}
