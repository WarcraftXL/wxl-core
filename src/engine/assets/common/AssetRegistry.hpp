// Thread-safe set of live model pointers, for a module to mark "this instance came from my
// pipeline" and query it later from a draw-time hook. DLL-only: live runtime pointers only exist
// once a model is loaded in the injected process, so this has no host form.
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

#include <atomic>
#include <cstdint>
#include <shared_mutex>
#include <unordered_map>

namespace wxl::modern::assets::common
{
    /**
     * @brief Thread-safe registry of live model pointers a module reshaped or otherwise owns, with
     *        per-model feature flags.
     *
     * A load hook Remember()s a model with the flags describing its origin (hot-reshaped in memory,
     * or file-tagged by the converter -- see shared/common/WxlTag.hpp); a free/replace path Forget()s
     * it (guarding against a new model reusing a freed model's address); a draw or finalize hook
     * queries Flags()/Contains() to scope itself to the instances and features actually concerned.
     *
     * Flags() is consulted at per-batch frequency, so it keeps two fast paths ahead of the lock:
     * an atomic emptiness test (a session with no modern models pays one relaxed load per query),
     * and a per-thread one-entry cache (consecutive batches of the same model pay two compares).
     * The cache is invalidated by a generation counter bumped on every Remember/Forget.
     */
    class AssetRegistry
    {
    public:
        // Registry-local flag: the model was reshaped in memory by this module (full modern contract,
        // including the skin rebuild). File-tag flags (WxlTag.hpp) occupy the low bits.
        static constexpr uint32_t kFlagHotReshaped = 0x80000000u;

        /**
         * @brief Registers a model pointer with its feature flags (merged if already present).
         * @param model  Runtime model pointer.
         * @param flags  Nonzero feature flags describing the model's origin/features.
         */
        void Remember(void* model, uint32_t flags)
        {
            std::unique_lock lock(mutex_);
            flags_[model] |= flags;
            count_.store(flags_.size(), std::memory_order_relaxed);
            generation_.fetch_add(1, std::memory_order_release);
        }

        /** @brief Drops a model pointer. @param model Runtime model pointer. */
        void Forget(void* model)
        {
            std::unique_lock lock(mutex_);
            // Every model load Forgets defensively, including non-modern models: erasing nothing
            // must not bump the generation, or streaming churn invalidates the render thread's
            // per-batch cache for no reason.
            if (flags_.erase(model) == 0) return;
            count_.store(flags_.size(), std::memory_order_relaxed);
            generation_.fetch_add(1, std::memory_order_release);
        }

        /** @brief Reports whether no model is registered at all. Safe on any hot path. */
        bool Empty() const
        {
            return count_.load(std::memory_order_relaxed) == 0;
        }

        /**
         * @brief Returns a model's registered feature flags, or 0 when the model is unknown.
         * @param model Runtime model pointer.
         * @return the flags passed to Remember(), or 0.
         */
        uint32_t Flags(void* model) const
        {
            if (Empty()) return 0;

            struct Cached { const AssetRegistry* reg; void* model; uint32_t generation; uint32_t flags; };
            thread_local Cached cached{};
            const uint32_t generation = generation_.load(std::memory_order_acquire);
            if (cached.reg == this && cached.model == model && cached.generation == generation)
                return cached.flags;

            uint32_t flags = 0;
            {
                std::shared_lock lock(mutex_);
                auto it = flags_.find(model);
                if (it != flags_.end()) flags = it->second;
            }
            cached = { this, model, generation, flags };
            return flags;
        }

        /**
         * @brief Queries whether a model pointer is registered (any origin).
         * @param model Runtime model pointer.
         * @return true if Remember() was called for this pointer and Forget() has not since.
         */
        bool Contains(void* model) const
        {
            return Flags(model) != 0;
        }

    private:
        mutable std::shared_mutex mutex_;
        std::unordered_map<void*, uint32_t> flags_;
        std::atomic<size_t> count_{ 0 };
        std::atomic<uint32_t> generation_{ 0 };
    };
}
