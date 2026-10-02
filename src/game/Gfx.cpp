// gfx: world-space shapes any module can queue and have drawn inside the scene.
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

#include "wxl/game/Gfx.hpp"

#include "wxl/game/Camera.hpp"
#include "wxl/game/Pick.hpp"

#include <cmath>
#include <vector>

namespace wxl::game::gfx
{
    namespace goff = wxl::offsets::engine::gx;

    namespace
    {
        struct Vertex { float x, y, z; Color color; };
        static_assert(sizeof(Vertex) == 16, "Vertex must match the declared vertex format stride");

        // Two queues rather than a flag per segment: the difference between them is one render state,
        // so grouping by it is what keeps a flush at two draw calls however many shapes were asked for.
        std::vector<Vertex> g_tested;
        std::vector<Vertex> g_through;
        std::vector<Vertex> g_fillTested;
        std::vector<Vertex> g_fillThrough;

        std::vector<Vertex>& Queue(Depth depth)
        { return depth == Depth::Tested ? g_tested : g_through; }

        std::vector<Vertex>& FillQueue(Depth depth)
        { return depth == Depth::Tested ? g_fillTested : g_fillThrough; }

        // How far a ground-hugging shape is lifted off the surface it was measured against. Drawn at
        // the measured height it competes with the terrain for the same depth values and breaks into
        // stripes; lifted much more it stops looking like a marking and starts looking like a lid.
        constexpr float kGroundLift = 0.12f;

        /**
         * @brief The surface under a map position, lifted clear of it.
         *
         * A miss keeps the height it was asked to search around, so a shape over a hole in the world
         * stays flat there instead of collapsing to nothing.
         */
        void GroundPoint(float x, float y, float searchZ, float out[3])
        {
            float z = searchZ;
            world::GroundZ(x, y, searchZ, z);
            out[0] = x;
            out[1] = y;
            out[2] = z + kGroundLift;
        }

        void Segment(Depth depth, float ax, float ay, float az, float bx, float by, float bz, Color c)
        {
            std::vector<Vertex>& q = Queue(depth);
            q.push_back(Vertex{ ax, ay, az, c });
            q.push_back(Vertex{ bx, by, bz, c });
        }

        constexpr float kTwoPi = 6.28318530718f;

        /// Every render state the flush writes, so that each one can be put back.
        constexpr unsigned kTouchedStates[] = {
            gx::rs::kZEnable, gx::rs::kShadeMode, gx::rs::kZWrite, gx::rs::kAlphaTest,
            gx::rs::kSrcBlend, gx::rs::kDestBlend, gx::rs::kCullMode, gx::rs::kZFunc,
            gx::rs::kAlphaBlend, gx::rs::kFogEnable, gx::rs::kStencilEnable,
            gx::rs::kLighting, gx::rs::kColorWrite, gx::rs::kScissorTest,
        };
        constexpr size_t kTouchedStateCount = sizeof(kTouchedStates) / sizeof(kTouchedStates[0]);

        /// Likewise for stage 0's colour path and stage 1's off switch.
        constexpr unsigned kTouchedStages[][2] = {
            { 0, gx::tss::kColorOp },  { 0, gx::tss::kColorArg1 },
            { 0, gx::tss::kAlphaOp },  { 0, gx::tss::kAlphaArg1 },
            { 1, gx::tss::kColorOp },
        };
        constexpr size_t kTouchedStageCount = sizeof(kTouchedStages) / sizeof(kTouchedStages[0]);

        /**
         * @brief One ring of a sphere, in the plane spanned by two unit axes.
         *
         * A ring is enough because a wireframe sphere is read from its silhouette: three rings at right
         * angles give that from any angle, at a fraction of the segments a real mesh would cost.
         */
        void Ring(Depth depth, const float center[3], float radius, int segments, Color c,
                  const float u[3], const float v[3])
        {
            float previous[3] = { center[0] + u[0] * radius,
                                  center[1] + u[1] * radius,
                                  center[2] + u[2] * radius };

            for (int i = 1; i <= segments; ++i)
            {
                const float angle = kTwoPi * float(i) / float(segments);
                const float cosine = std::cos(angle);
                const float sine   = std::sin(angle);

                const float point[3] = { center[0] + (u[0] * cosine + v[0] * sine) * radius,
                                         center[1] + (u[1] * cosine + v[1] * sine) * radius,
                                         center[2] + (u[2] * cosine + v[2] * sine) * radius };

                Segment(depth, previous[0], previous[1], previous[2], point[0], point[1], point[2], c);
                previous[0] = point[0]; previous[1] = point[1]; previous[2] = point[2];
            }
        }
    }

    void Line(const float a[3], const float b[3], Color c, Depth depth)
    { Segment(depth, a[0], a[1], a[2], b[0], b[1], b[2], c); }

    void Box(const float min[3], const float max[3], Color c, Depth depth)
    {
        const float x0 = min[0], y0 = min[1], z0 = min[2];
        const float x1 = max[0], y1 = max[1], z1 = max[2];

        // Bottom ring, top ring, then the four uprights joining them.
        Segment(depth, x0, y0, z0, x1, y0, z0, c);
        Segment(depth, x1, y0, z0, x1, y1, z0, c);
        Segment(depth, x1, y1, z0, x0, y1, z0, c);
        Segment(depth, x0, y1, z0, x0, y0, z0, c);

        Segment(depth, x0, y0, z1, x1, y0, z1, c);
        Segment(depth, x1, y0, z1, x1, y1, z1, c);
        Segment(depth, x1, y1, z1, x0, y1, z1, c);
        Segment(depth, x0, y1, z1, x0, y0, z1, c);

        Segment(depth, x0, y0, z0, x0, y0, z1, c);
        Segment(depth, x1, y0, z0, x1, y0, z1, c);
        Segment(depth, x1, y1, z0, x1, y1, z1, c);
        Segment(depth, x0, y1, z0, x0, y1, z1, c);
    }

    void BoxAt(const float center[3], const float extents[3], Color c, Depth depth)
    {
        const float min[3] = { center[0] - extents[0], center[1] - extents[1], center[2] - extents[2] };
        const float max[3] = { center[0] + extents[0], center[1] + extents[1], center[2] + extents[2] };
        Box(min, max, c, depth);
    }

    void Circle(const float center[3], float radius, Color c, Depth depth, int segments)
    {
        if (segments < 3) segments = 3;
        // World axes: x and y span the horizontal, z is up, so a ground ring is the xy plane.
        constexpr float u[3] = { 1.0f, 0.0f, 0.0f };
        constexpr float v[3] = { 0.0f, 1.0f, 0.0f };
        Ring(depth, center, radius, segments, c, u, v);
    }

    void Sphere(const float center[3], float radius, Color c, Depth depth, int segments)
    {
        if (segments < 3) segments = 3;
        constexpr float x[3] = { 1.0f, 0.0f, 0.0f };
        constexpr float y[3] = { 0.0f, 1.0f, 0.0f };
        constexpr float z[3] = { 0.0f, 0.0f, 1.0f };
        Ring(depth, center, radius, segments, c, x, y);
        Ring(depth, center, radius, segments, c, x, z);
        Ring(depth, center, radius, segments, c, y, z);
    }

    void Cross(const float p[3], float size, Color c, Depth depth)
    {
        Segment(depth, p[0] - size, p[1], p[2], p[0] + size, p[1], p[2], c);
        Segment(depth, p[0], p[1] - size, p[2], p[0], p[1] + size, p[2], c);
        Segment(depth, p[0], p[1], p[2] - size, p[0], p[1], p[2] + size, c);
    }

    void Arrow(const float from[3], const float to[3], float headSize, Color c, Depth depth)
    {
        Segment(depth, from[0], from[1], from[2], to[0], to[1], to[2], c);

        float shaft[3] = { from[0] - to[0], from[1] - to[1], from[2] - to[2] };
        const float length = std::sqrt(shaft[0] * shaft[0] + shaft[1] * shaft[1] + shaft[2] * shaft[2]);
        if (length < 1e-4f) return;   // no direction to point in
        shaft[0] /= length; shaft[1] /= length; shaft[2] /= length;

        // Any two directions across the shaft will do for the barbs, and a cross product with an axis
        // gives one -- except where the shaft is that axis, which is why the second axis is a fallback.
        constexpr float kUp[3]   = { 0.0f, 0.0f, 1.0f };
        constexpr float kSide[3] = { 1.0f, 0.0f, 0.0f };
        const float* reference = (std::fabs(shaft[2]) > 0.99f) ? kSide : kUp;

        float across[3] = { shaft[1] * reference[2] - shaft[2] * reference[1],
                            shaft[2] * reference[0] - shaft[0] * reference[2],
                            shaft[0] * reference[1] - shaft[1] * reference[0] };
        const float acrossLength = std::sqrt(across[0] * across[0] + across[1] * across[1]
                                             + across[2] * across[2]);
        if (acrossLength < 1e-4f) return;
        across[0] /= acrossLength; across[1] /= acrossLength; across[2] /= acrossLength;

        for (int side = -1; side <= 1; side += 2)
        {
            const float barb[3] = { to[0] + (shaft[0] + across[0] * float(side)) * headSize,
                                    to[1] + (shaft[1] + across[1] * float(side)) * headSize,
                                    to[2] + (shaft[2] + across[2] * float(side)) * headSize };
            Segment(depth, to[0], to[1], to[2], barb[0], barb[1], barb[2], c);
        }
    }

    bool SceneMatrices(float view[16], float projection[16])
    {
        void* const graphics = gx::RawGraphicsDevice();
        if (!graphics) return false;

        const uintptr_t base = uintptr_t(graphics);
        const int slot = *reinterpret_cast<const int*>(base + goff::kDeviceViewIndex);

        const float* const storedProjection = reinterpret_cast<const float*>(base + goff::kDeviceProjection);
        const float* const storedView = reinterpret_cast<const float*>(base + goff::kDeviceViewBase
                                                                      + size_t(slot) * goff::kDeviceViewStride);
        for (int i = 0; i < 16; ++i)
        {
            view[i]       = storedView[i];
            projection[i] = storedProjection[i];
        }
        return true;
    }

    void Triangle(const float a[3], const float b[3], const float c[3], Color c0, Depth depth)
    {
        std::vector<Vertex>& q = FillQueue(depth);
        q.push_back(Vertex{ a[0], a[1], a[2], c0 });
        q.push_back(Vertex{ b[0], b[1], b[2], c0 });
        q.push_back(Vertex{ c[0], c[1], c[2], c0 });
    }

    void GroundDisc(const float center[3], float radius, Color c, int segments, int rings, Depth depth)
    {
        if (segments < 3) segments = 3;
        if (rings < 1)    rings = 1;

        // Reused rather than allocated per call: a disc is rebuilt every frame it is asked for, and
        // the two rings it needs at a time are the same size each round.
        static std::vector<float> inner, outer;
        inner.assign(size_t(segments) * 3, 0.0f);
        outer.assign(size_t(segments) * 3, 0.0f);

        float middle[3];
        GroundPoint(center[0], center[1], center[2], middle);

        for (int r = 1; r <= rings; ++r)
        {
            const float bandRadius = radius * float(r) / float(rings);
            for (int i = 0; i < segments; ++i)
            {
                const float angle = kTwoPi * float(i) / float(segments);
                GroundPoint(center[0] + std::cos(angle) * bandRadius,
                            center[1] + std::sin(angle) * bandRadius,
                            center[2], &outer[size_t(i) * 3]);
            }

            for (int i = 0; i < segments; ++i)
            {
                const int next = (i + 1) % segments;
                if (r == 1)
                {
                    Triangle(middle, &outer[size_t(i) * 3], &outer[size_t(next) * 3], c, depth);
                }
                else
                {
                    Triangle(&inner[size_t(i) * 3], &outer[size_t(i) * 3], &outer[size_t(next) * 3], c, depth);
                    Triangle(&inner[size_t(i) * 3], &outer[size_t(next) * 3], &inner[size_t(next) * 3], c, depth);
                }
            }
            inner.swap(outer);
        }
    }

    void GroundRing(const float center[3], float radius, Color c, int segments, Depth depth)
    {
        if (segments < 3) segments = 3;

        float previous[3];
        GroundPoint(center[0] + radius, center[1], center[2], previous);

        for (int i = 1; i <= segments; ++i)
        {
            const float angle = kTwoPi * float(i) / float(segments);
            float point[3];
            GroundPoint(center[0] + std::cos(angle) * radius,
                        center[1] + std::sin(angle) * radius,
                        center[2], point);

            Segment(depth, previous[0], previous[1], previous[2],
                    point[0], point[1], point[2], c);
            previous[0] = point[0]; previous[1] = point[1]; previous[2] = point[2];
        }
    }

    void Clear()
    {
        g_tested.clear();
        g_through.clear();
        g_fillTested.clear();
        g_fillThrough.clear();
    }

    size_t Pending()
    {
        return (g_tested.size() + g_through.size()) / 2
             + (g_fillTested.size() + g_fillThrough.size()) / 3;
    }

    long Flush(gx::Device9 dev, void* sceneDepth)
    {
        // Emptied on every path out, including the ones that draw nothing: a module that queues shapes
        // while graphics is down must not have them appear all at once when it comes back.
        struct Emptied { ~Emptied() { Clear(); } } emptied;

        if (!dev || Pending() == 0) return 0;

        // The scene's own matrices, not a reconstruction of them: taking what the world was drawn with
        // is what makes a shape land where its coordinates say, with no field of view or aspect ratio
        // for the caller to keep in step.
        float view[16], projection[16];
        if (!SceneMatrices(view, projection)) return 0;

        long result = 0;

        void* oldVS  = nullptr; dev.GetVertexShader(&oldVS);
        void* oldPS  = nullptr; dev.GetPixelShader(&oldPS);
        void* oldTex = nullptr; dev.GetTexture(0, &oldTex);

        // Whatever is bound here is not necessarily what the world was drawn into.
        void* oldDepth = nullptr;
        if (sceneDepth)
        {
            dev.GetDepthStencil(&oldDepth);
            dev.SetDepthStencil(sceneDepth);
        }

        float oldWorld[16], oldView[16], oldProjection[16];
        dev.GetTransform(gx::ts::kWorld, oldWorld);
        dev.GetTransform(gx::ts::kView, oldView);
        dev.GetTransform(gx::ts::kProjection, oldProjection);

        unsigned oldStates[kTouchedStateCount];
        for (size_t i = 0; i < kTouchedStateCount; ++i)
            oldStates[i] = dev.GetRenderState(kTouchedStates[i]);

        unsigned oldStages[kTouchedStageCount];
        for (size_t i = 0; i < kTouchedStageCount; ++i)
            oldStages[i] = dev.GetTextureStageState(kTouchedStages[i][0], kTouchedStages[i][1]);

        // Untextured, unlit, vertex-coloured geometry through the fixed-function pipeline. Every one of
        // these is inherited from whatever drew last, and any one left wrong rejects the lines outright
        // or repaints them in a colour that is not the one asked for.
        dev.SetVertexShader(nullptr);
        dev.SetPixelShader(nullptr);
        dev.SetTexture(0, nullptr);
        dev.SetFVF(gx::fvf::kXyz | gx::fvf::kDiffuse);

        dev.SetTextureStageState(0, gx::tss::kColorOp,   gx::top::kSelectArg1);
        dev.SetTextureStageState(0, gx::tss::kColorArg1, gx::ta::kDiffuse);
        dev.SetTextureStageState(0, gx::tss::kAlphaOp,   gx::top::kSelectArg1);
        dev.SetTextureStageState(0, gx::tss::kAlphaArg1, gx::ta::kDiffuse);
        dev.SetTextureStageState(1, gx::tss::kColorOp,   gx::top::kDisable);

        // The scene's view matrix is a rotation with no translation in it: the world is drawn about the
        // camera rather than about the map origin, which is how a map tens of thousands of units across
        // keeps its float precision where the viewer is. Geometry given in map coordinates therefore has
        // to be moved to that origin, or it is turned correctly and projected thousands of units off
        // screen -- a draw the device accepts and nothing shows for.
        float eye[3];
        camera::GetPosition(eye);
        const float toCameraOrigin[16] = {
            1.0f,    0.0f,    0.0f,    0.0f,
            0.0f,    1.0f,    0.0f,    0.0f,
            0.0f,    0.0f,    1.0f,    0.0f,
            -eye[0], -eye[1], -eye[2], 1.0f,
        };

        dev.SetTransform(gx::ts::kWorld, toCameraOrigin);
        dev.SetTransform(gx::ts::kView, view);
        dev.SetTransform(gx::ts::kProjection, projection);

        dev.SetRenderState(gx::rs::kLighting, 0);
        dev.SetRenderState(gx::rs::kFogEnable, 0);   // world fog would tint the shapes with distance
        dev.SetRenderState(gx::rs::kCullMode, gx::cull::kNone);
        dev.SetRenderState(gx::rs::kAlphaTest, 0);
        dev.SetRenderState(gx::rs::kStencilEnable, 0);
        dev.SetRenderState(gx::rs::kScissorTest, 0);
        dev.SetRenderState(gx::rs::kColorWrite, gx::colorwrite::kAll);
        dev.SetRenderState(gx::rs::kShadeMode, gx::shade::kGouraud);
        dev.SetRenderState(gx::rs::kAlphaBlend, 1);
        dev.SetRenderState(gx::rs::kSrcBlend, gx::blend::kSrcAlpha);
        dev.SetRenderState(gx::rs::kDestBlend, gx::blend::kInvSrcAlpha);

        // Read the scene's depth, never add to it: a marker is not part of the world, and one that
        // wrote depth would occlude whatever the client draws next.
        dev.SetRenderState(gx::rs::kZWrite, 0);

        // Fills before lines within each depth mode: an outline drawn over its own fill reads as an
        // edge, and one drawn under it disappears into it.
        dev.SetRenderState(gx::rs::kZEnable, 1);
        dev.SetRenderState(gx::rs::kZFunc, gx::cmp::kLessEqual);

        if (!g_fillTested.empty())
            result = dev.DrawPrimitiveUP(gx::prim::kTriangleList, unsigned(g_fillTested.size() / 3),
                                         g_fillTested.data(), sizeof(Vertex));
        if (!g_tested.empty())
            result = dev.DrawPrimitiveUP(gx::prim::kLineList, unsigned(g_tested.size() / 2),
                                         g_tested.data(), sizeof(Vertex));

        // Second, so that where the two overlap the one meant to be found is the one seen.
        dev.SetRenderState(gx::rs::kZEnable, 0);

        if (!g_fillThrough.empty())
            result = dev.DrawPrimitiveUP(gx::prim::kTriangleList, unsigned(g_fillThrough.size() / 3),
                                         g_fillThrough.data(), sizeof(Vertex));
        if (!g_through.empty())
            result = dev.DrawPrimitiveUP(gx::prim::kLineList, unsigned(g_through.size() / 2),
                                         g_through.data(), sizeof(Vertex));

        for (size_t i = 0; i < kTouchedStageCount; ++i)
            dev.SetTextureStageState(kTouchedStages[i][0], kTouchedStages[i][1], oldStages[i]);
        for (size_t i = 0; i < kTouchedStateCount; ++i)
            dev.SetRenderState(kTouchedStates[i], oldStates[i]);

        dev.SetTransform(gx::ts::kWorld, oldWorld);
        dev.SetTransform(gx::ts::kView, oldView);
        dev.SetTransform(gx::ts::kProjection, oldProjection);

        if (sceneDepth)
        {
            dev.SetDepthStencil(oldDepth);
            gx::Release(oldDepth);
        }

        dev.SetTexture(0, oldTex);
        dev.SetPixelShader(oldPS);
        dev.SetVertexShader(oldVS);

        gx::Release(oldTex);
        gx::Release(oldPS);
        gx::Release(oldVS);

        return result;
    }
}
