// Legion's exterior re-enable rule, added to the 3.3.5 indoor/outdoor gate.
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

// MECHANISM
//
//   The gate    CWorldScene::Render suppresses the exterior while the viewer is inside a map object.
//               At 0x0079AA22 it runs the interior portal traversal, then tests one float:
//                   fld dword [0x00ADF59C] ; fcom 0.0 ; ...
//               negative -> no exterior at all; zero or above -> the exterior pass runs, clipped to the
//               rect at 0x00ADF58C that portal traversal accumulated. The indoor branch seeds that rect
//               EMPTY and the gate at -1; only a portal into an exterior group raises it.
//
//   The delta   Legion suppresses the exterior in exactly the same way, and decides "the viewer is
//               indoors" with the same downward BSP raycast and the same disqualifying mask. What it
//               adds are three further ways to turn the exterior back on (FUN_00CB5FEC). On the active
//               corpus only one of them ever fires: an indoor group that has TRANSITION BATCHES, i.e.
//               an interior open to the sky -- a bridge underside, an archway, a porch. 37 of the 94
//               indoor groups here qualify, and each is a spot where 3.3.5 blanks half the terrain.
//
//   The fix     Post-hook the traversal and, when the rule applies, write the same five floats the
//               engine's own OUTDOOR branch writes: a full 0..1 screen rect and a zero gate. The
//               client then takes its own exterior path. No instruction is patched, no asset byte is
//               touched, and the stock portal rule keeps priority -- if traversal already opened the
//               exterior, its accumulated rect is left exactly as it is.

#include "config.hpp"

#include "engine/hook/Hook.hpp"
#include "engine/hook/Registry.hpp"
#include "features/wmonative/OutdoorGate.hpp"
#include "features/wmonative/WmoNative.hpp"

#include "common/Log.hpp"
#include "offsets/game/WMO.hpp"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <unordered_map>

namespace
{
    namespace off = wxl::offsets::game::wmo;

    std::atomic<uint32_t> g_framesIndoor{0}, g_framesReopened{0}, g_framesPortalOpen{0}, g_resolveFails{0};
    std::atomic<bool> g_installed{false};
    std::atomic<bool> g_ruleEnabled{true};

    off::Wmo_PortalTraverseFn       g_origTraverse = nullptr;
    off::Wmo_LocateViewerMapObjsFn  g_origLocate   = nullptr;
    off::Wmo_CullSortTableFn        g_origCullSort = nullptr;

    /// Set for the current frame when the added rule opened the exterior, so the submission that
    /// follows can be given the outdoor distance band instead of the portal-derived one.
    std::atomic<bool> g_reopenedThisFrame{false};

    /// Clears the per-frame antiportal underside table (defined with the occluder rule below).
    void ResetUnderside();

    /// The group the viewer is inside, as reported by the last CMap::LocateViewerMapObjs of this frame.
    /// Whether the engine hands back a group OBJECT or a group INDEX is resolved defensively below, so
    /// the rule works either way and says so in the log the first time it decides.
    std::atomic<void*> g_viewerGroupRaw{nullptr};
    std::atomic<bool>  g_reportedGroupForm{false};

    inline uint32_t Rd32(const void* p) { uint32_t v; std::memcpy(&v, p, 4); return v; }
    inline uint16_t Rd16(const void* p) { uint16_t v; std::memcpy(&v, p, 2); return v; }

    inline float OutdoorGate() { return *reinterpret_cast<const float*>(off::kOutdoorEnabled); }

    /// Writes the five floats the engine's own outdoor branch writes: full screen rect, gate open.
    void OpenExterior()
    {
        auto* rect = reinterpret_cast<float*>(off::kPortalRect);
        std::memcpy(rect, off::kPortalRectFullScreen, sizeof off::kPortalRectFullScreen);
        rect[4] = off::kOutdoorEnabledOn;
    }

    /**
     * @brief Resolves the viewer's group object from whatever LocateViewerMapObjs reported.
     *
     * The out-parameter is either a group object pointer or a group index into the instance's root
     * array; both forms are handled rather than assumed, because getting it wrong would silently read
     * flags out of unrelated memory. A value that is neither a plausible pointer nor an in-range index
     * yields null and the rule stands down.
     * @param instance  the interior map-object instance (ECX of the traversal).
     * @return the group object, or null when it cannot be resolved.
     */
    void* ResolveViewerGroup(void* instance)
    {
        void* raw = g_viewerGroupRaw.load(std::memory_order_relaxed);
        if (!raw || !instance)
            return nullptr;

        void* root = *reinterpret_cast<void**>(static_cast<uint8_t*>(instance) + off::kOffInstanceRoot);
        if (!root)
            return nullptr;

        const uintptr_t value = reinterpret_cast<uintptr_t>(raw);
        const uint32_t groupCount = Rd32(static_cast<uint8_t*>(root) + off::kOffGroupCount);

        // Small value -> a group index into the root's inline array.
        if (value < 0x10000)
        {
            if (value >= groupCount)
                return nullptr;
            auto* slot = reinterpret_cast<void**>(static_cast<uint8_t*>(root) + off::kOffGroupArray);
            void* group = slot[value];
            if (!g_reportedGroupForm.exchange(true, std::memory_order_relaxed))
                WLOG_INFO("wmo-outdoor: viewer group reported as an INDEX (%u of %u)",
                          static_cast<uint32_t>(value), groupCount);
            return group;
        }

        // Otherwise a group object: only trust it if it really belongs to this root, which also proves
        // the pointer is a group and not some other structure.
        void* backRoot = *reinterpret_cast<void**>(static_cast<uint8_t*>(raw) + off::kOffGroupRoot);
        if (backRoot != root)
            return nullptr;
        if (!g_reportedGroupForm.exchange(true, std::memory_order_relaxed))
            WLOG_INFO("wmo-outdoor: viewer group reported as an OBJECT pointer");
        return raw;
    }

    /// Legion's condition B: an indoor group carrying transition batches keeps the world visible.
    /// A and C are evaluated too, for completeness and so the counters tell the truth on other corpora.
    bool GroupReopensExterior(void* group)
    {
        if (!group)
            return false;
        const uint32_t flags = Rd32(static_cast<uint8_t*>(group) + off::kOffGroupFlags);
        if (flags & off::kGroupFlagExteriorLit)                       // A
            return true;
        if (!(flags & off::kGroupFlagIndoor))
            return false;
        if (flags & off::kGroupFlagExteriorPortal)                    // C
            return true;
        return Rd16(static_cast<uint8_t*>(group) + off::kOffGroupTransBatchCount) != 0;  // B
    }

    char __cdecl hkLocateViewerMapObjs(const float* camA, const float* camB, float radius,
                                       void** instanceOut, void** groupOut)
    {
        // Once per frame, and early enough: this runs at the top of the scene update, well before any
        // occluder is projected or any box is culled.
        ResetUnderside();
        const char r = g_origLocate(camA, camB, radius, instanceOut, groupOut);
        g_viewerGroupRaw.store(r && groupOut ? *groupOut : nullptr, std::memory_order_relaxed);
        return r;
    }

    void __fastcall hkPortalTraverse(void* instance, void* edx, void* state)
    {
        g_origTraverse(instance, edx, state);

        // Only the pre-gate call site matters; the other one runs on a different traversal state.
        if (state != reinterpret_cast<void*>(off::kPortalTraverseGateState) || !instance)
            return;

        g_framesIndoor.fetch_add(1, std::memory_order_relaxed);

        if (OutdoorGate() >= 0.0f)
        {
            g_framesPortalOpen.fetch_add(1, std::memory_order_relaxed); // stock portal rule already won
            return;
        }
        if (!g_ruleEnabled.load(std::memory_order_relaxed))
            return;

        void* group = ResolveViewerGroup(instance);
        if (!group)
        {
            g_resolveFails.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        if (!GroupReopensExterior(group))
            return;

        OpenExterior();
        g_reopenedThisFrame.store(true, std::memory_order_relaxed);
        g_framesReopened.fetch_add(1, std::memory_order_relaxed);
    }

    /**
     * @brief Gives the exterior submission the OUTDOOR distance band when the added rule opened it.
     *
     * Raising the gate is not enough on its own. The interior branch derives the near end of the
     * submission band as `gate + 33.33`, because there the gate holds a portal's near extent -- so a
     * gate of 0 leaves everything within 33 yards of the camera unsubmitted, while the real outdoor
     * branch uses -10000 and draws it all. This is the only seam between that derivation and its use.
     * @param rect        the accumulated screen rect.
     * @param restricted  non-zero when the submission is clipped to the portal rect.
     */
    void __cdecl hkCullSortTable(void* rect, int restricted)
    {
        if (g_reopenedThisFrame.exchange(false, std::memory_order_relaxed))
            *reinterpret_cast<float*>(off::kExteriorNearBand) = off::kExteriorNearBandOpen;
        g_origCullSort(rect, restricted);
    }

    // ------------------------------------------------------------------ antiportal occluders
    off::Wmo_CreateOccludersFn  g_origOccluders  = nullptr;
    off::Wmo_AllocOccluderFn    g_origAlloc      = nullptr;
    off::Wmo_AddOccluderEdgeFn  g_origAddEdge    = nullptr;
    off::Wmo_HorizonAabbTestFn  g_origHorizon    = nullptr;

    // DEFAULT: a modern antiportal contributes no occluder at all. This is the state that was measured
    // working in game, and it is safe by nature -- an occluder can only ever REMOVE geometry, so
    // dropping one costs over-draw and nothing else.
    std::atomic<bool> g_skipOccluders{true};
    // The refinement -- projecting each antiportal's floor into a second table so the client's skyline
    // gains a lower bound instead of being discarded. Sound on paper, WRONG in game on the first
    // attempt, so it stays off until it is debugged against the counters rather than by guesswork.
    std::atomic<bool> g_undersideRule{false};
    std::atomic<uint32_t> g_occludersSkipped{0}, g_occludersBuilt{0};
    std::atomic<uint32_t> g_undersideEdges{0}, g_unculled{0};

    constexpr float kHorizonEmpty = -1000000.0f;    // the client's own "nothing occludes here"

    /// Per-azimuth lowest projected elevation of an antiportal's UNDERSIDE, rebuilt every frame.
    /// kHorizonEmpty means "no modern antiportal covers this azimuth", which makes the band test fail
    /// and leaves the stock verdict untouched.
    float g_underside[off::kClipBufferSlots];
    bool  g_undersideActive = false;

    /// Occluder record -> the Z of its group's lowest point, captured while CreateOccluders runs.
    /// One float per occluder is all the band needs: every occluder of a group shares the same
    /// underside, and using the WHOLE GROUP's floor rather than each triangle's own lower vertex keeps
    /// the rule conservative -- an object is only ever un-culled when it is below the entire slab.
    std::unordered_map<void*, float> g_occluderUnderZ;
    float g_capturingUnderZ = 0.0f;
    bool  g_capturing = false;

    void ResetUnderside()
    {
        for (float& v : g_underside) v = kHorizonEmpty;
        g_undersideActive = false;
    }

    /**
     * @brief Suppresses the CPU occluders 3.3.5 builds from a MODERN antiportal group.
     *
     * Both engines let a group named "antiportal" hide what is behind it, but they consume it very
     * differently: 3.3.5 feeds it to an ANGULAR clip buffer -- a coarse solid-angle test, no depth --
     * while Legion rasterises the mesh into a hi-Z occlusion buffer, so it can only hide what truly
     * falls behind its silhouette. Modern antiportals are built for the second: the one inside
     * 9vm_vampire_bridge03 is a 173 x 48 unit slab. Handed to the angular test it subtends an enormous
     * cone, which is why looking at the bridge head-on erases the world behind it while looking at it
     * from the side changes nothing.
     *
     * Until the occluders can be given a depth-aware test, a modern antiportal builds none. That costs
     * some over-draw behind those walls and costs nothing in correctness -- an occluder can only ever
     * REMOVE geometry. Stock WMOs keep their occluders untouched, so Stormwind is unaffected. The batch
     * counts the original zeroes are still zeroed here, so the invisible shell stays invisible.
     */
    void __fastcall hkCreateOccluders(void* group, void* edx)
    {
        void* root = group ? *reinterpret_cast<void**>(static_cast<uint8_t*>(group) + off::kOffGroupRoot)
                           : nullptr;
        const bool modern = wxl::runtime::wmonative::IsModernRoot(root);

        if (modern && g_skipOccluders.load(std::memory_order_relaxed))
        {
            auto* g = static_cast<uint8_t*>(group);
            *reinterpret_cast<uint16_t*>(g + off::kOffGroupIntBatchCount) = 0;
            *reinterpret_cast<uint16_t*>(g + off::kOffGroupExtBatchCount) = 0;
            g_occludersSkipped.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        // Let the client build its occluders exactly as it always has; we only note, for each record it
        // allocates, where the group's floor is. That is what turns its skyline into a band later.
        g_capturing = modern;
        g_capturingUnderZ = modern ? *reinterpret_cast<const float*>(
                                         static_cast<uint8_t*>(group) + off::kOffGroupBboxMinZ)
                                   : 0.0f;
        g_origOccluders(group, edx);
        g_capturing = false;
        g_occludersBuilt.fetch_add(1, std::memory_order_relaxed);
    }

    /// Pairs each freshly allocated occluder with the floor of the group being processed.
    void* __cdecl hkAllocOccluder()
    {
        void* occ = g_origAlloc();
        if (occ && g_capturing)
            g_occluderUnderZ[occ] = g_capturingUnderZ;
        return occ;
    }

    /**
     * @brief Records the UNDERSIDE of a modern antiportal alongside the horizon the client builds.
     *
     * The client projects the occluder's top edge and raises `clipBuffer[azimuth]` -- a skyline. To give
     * that skyline a lower bound we need the same edge projected at the group's floor height, in the
     * same space, with the same matrix, at the same moment. Rather than re-derive the projection (and
     * risk disagreeing with it in some corner), the engine's own projector is run a second time on a
     * copy of the edge with its Z replaced, over a scratched horizon table, and the result folded into
     * our buffer. The tables are saved and restored around the probe, so the client sees nothing.
     */
    void __fastcall hkAddOccluderEdge(void* vertices, void* edx)
    {
        // The engine passes this either as the occluder record or as its vertex field; the decompiled
        // iteration lost the register, so both keys are tried. A wrong key simply misses -- the map only
        // ever holds real occluder records -- so this costs one failed lookup and removes the guess.
        auto it = g_occluderUnderZ.end();
        if (vertices && g_undersideRule.load(std::memory_order_relaxed))
        {
            it = g_occluderUnderZ.find(static_cast<uint8_t*>(vertices) - off::kOffOccluderVertices);
            if (it == g_occluderUnderZ.end())
                it = g_occluderUnderZ.find(vertices);
        }
        if (it == g_occluderUnderZ.end())
        {
            g_origAddEdge(vertices, edx);
            return;
        }

        float  edge[6];
        std::memcpy(edge, vertices, sizeof edge);
        edge[2] = it->second;   // both vertices dropped to the group's floor
        edge[5] = it->second;

        auto* clip = reinterpret_cast<float*>(off::kClipBuffer);
        auto* mask = reinterpret_cast<uint8_t*>(off::kClipBufferMask);
        float   savedClip[off::kClipBufferSlots];
        uint8_t savedMask[off::kClipBufferSlots];
        std::memcpy(savedClip, clip, sizeof savedClip);
        std::memcpy(savedMask, mask, sizeof savedMask);

        for (size_t i = 0; i < off::kClipBufferSlots; ++i) clip[i] = kHorizonEmpty;
        g_origAddEdge(edge, edx);
        for (size_t i = 0; i < off::kClipBufferSlots; ++i)
            if (clip[i] != kHorizonEmpty && clip[i] < g_underside[i])
            {
                g_underside[i] = clip[i];
                g_undersideActive = true;
            }

        std::memcpy(clip, savedClip, sizeof savedClip);
        std::memcpy(mask, savedMask, sizeof savedMask);
        g_undersideEdges.fetch_add(1, std::memory_order_relaxed);

        g_origAddEdge(vertices, edx);   // and now the real edge, untouched
    }

    /**
     * @brief Gives the horizon cull a lower bound, and can only ever UN-cull.
     *
     * The stock verdict is taken first and kept whenever it says "visible". When it says "culled", the
     * same test is replayed against the underside table: if it culls there too, the box lies entirely
     * BELOW the antiportal's floor, so the slab never hid it and the stock verdict was wrong. Replaying
     * the engine's own function rather than re-deriving its projection is what makes this exact; and
     * because the only possible outcome is 2 -> 0, a mistake here can add over-draw and nothing else.
     */
    uint32_t __cdecl hkHorizonAabbTest(void* bbox, uint32_t mode)
    {
        const uint32_t verdict = g_origHorizon(bbox, mode);
        if (verdict != 2 || !g_undersideActive || !g_undersideRule.load(std::memory_order_relaxed))
            return verdict;

        auto* clip = reinterpret_cast<float*>(off::kClipBuffer);
        float savedClip[off::kClipBufferSlots];
        std::memcpy(savedClip, clip, sizeof savedClip);
        std::memcpy(clip, g_underside, sizeof savedClip);
        const uint32_t belowFloor = g_origHorizon(bbox, mode);
        std::memcpy(clip, savedClip, sizeof savedClip);

        if (belowFloor != 2)
            return verdict;
        g_unculled.fetch_add(1, std::memory_order_relaxed);
        return 0;
    }

    bool InstallOutdoorGate()
    {
        const bool locateOk = wxl::hook::Install("WmoOutdoor_LocateViewer", off::kLocateViewerMapObjs,
                                                 &hkLocateViewerMapObjs, &g_origLocate);
        const bool traverseOk = wxl::hook::Install("WmoOutdoor_PortalTraverse", off::kPortalTraverse,
                                                   &hkPortalTraverse, &g_origTraverse);
        const bool cullOk = wxl::hook::Install("WmoOutdoor_CullSortTable", off::kCullSortTable,
                                               &hkCullSortTable, &g_origCullSort);
        const bool occlOk = wxl::hook::Install("WmoOutdoor_CreateOccluders", off::kCreateOccluders,
                                               &hkCreateOccluders, &g_origOccluders);
        const bool allocOk = wxl::hook::Install("WmoOutdoor_AllocOccluder", off::kAllocOccluder,
                                                &hkAllocOccluder, &g_origAlloc);
        const bool edgeOk = wxl::hook::Install("WmoOutdoor_AddOccluderEdge", off::kAddOccluderEdge,
                                               &hkAddOccluderEdge, &g_origAddEdge);
        const bool horizonOk = wxl::hook::Install("WmoOutdoor_HorizonAabbTest", off::kHorizonAabbTest,
                                                  &hkHorizonAabbTest, &g_origHorizon);
        ResetUnderside();
        // Without the locate detour the rule has no group to test, so it would stand down every frame
        // and only cost a call. Without the sort-table detour it would open the exterior but keep a
        // 33-yard near hole. All-or-nothing keeps a half-working state from looking like a feature.
        const bool ok = locateOk && traverseOk && cullOk && occlOk && allocOk && edgeOk && horizonOk;
        g_installed.store(ok, std::memory_order_relaxed);
        if (!ok)
        {
            // The underside rule needs all four of its detours or it would half-apply: a horizon with
            // no matching band, or a band no cull ever consults. Fall back to the blunt instrument.
            g_undersideRule.store(false, std::memory_order_relaxed);
            g_skipOccluders.store(true, std::memory_order_relaxed);
            WLOG_ERROR("wmo-outdoor: install failed (locate=%d traverse=%d cull=%d occluders=%d "
                       "alloc=%d edge=%d horizon=%d); underside rule off, modern occluders skipped",
                       locateOk ? 1 : 0, traverseOk ? 1 : 0, cullOk ? 1 : 0, occlOk ? 1 : 0,
                       allocOk ? 1 : 0, edgeOk ? 1 : 0, horizonOk ? 1 : 0);
        }
        return ok;
    }
}

namespace wxl::runtime::wmooutdoor
{
    Stats GetStats()
    {
        Stats s{};
        s.framesIndoor      = g_framesIndoor.load(std::memory_order_relaxed);
        s.framesReopened    = g_framesReopened.load(std::memory_order_relaxed);
        s.framesPortalOpen  = g_framesPortalOpen.load(std::memory_order_relaxed);
        s.groupResolveFails = g_resolveFails.load(std::memory_order_relaxed);
        s.occludersSkipped  = g_occludersSkipped.load(std::memory_order_relaxed);
        s.occludersBuilt    = g_occludersBuilt.load(std::memory_order_relaxed);
        s.undersideEdges    = g_undersideEdges.load(std::memory_order_relaxed);
        s.boxesUnculled     = g_unculled.load(std::memory_order_relaxed);
        return s;
    }

    bool OccludersSkipped() { return g_skipOccluders.load(std::memory_order_relaxed); }
    void SetOccludersSkipped(bool on) { g_skipOccluders.store(on, std::memory_order_relaxed); }

    bool UndersideRuleEnabled() { return g_undersideRule.load(std::memory_order_relaxed); }
    void SetUndersideRuleEnabled(bool on) { g_undersideRule.store(on, std::memory_order_relaxed); }

    bool Enabled()   { return wxl::features::kWmoOutdoorGate; }
    bool Installed() { return g_installed.load(std::memory_order_relaxed); }

    bool RuleEnabled() { return g_ruleEnabled.load(std::memory_order_relaxed); }
    void SetRuleEnabled(bool on) { g_ruleEnabled.store(on, std::memory_order_relaxed); }
}

WXL_REGISTER_FEATURE("wmo-outdoor-gate", wxl::features::kWmoOutdoorGate, InstallOutdoorGate)
