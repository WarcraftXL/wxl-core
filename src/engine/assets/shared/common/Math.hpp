// Row-vector 4x4 matrix and quaternion helpers shared by every asset format's animation bake.
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
#include <cmath>

namespace wxl::modern::assets::common
{
    using Vec3 = std::array<float, 3>;
    using Quat = std::array<float, 4>; // x y z w

    struct Mat4
    {
        float m[4][4];
    };

    inline Mat4 MatIdentity()
    {
        Mat4 r{};
        for (int i = 0; i < 4; ++i) r.m[i][i] = 1.0f;
        return r;
    }

    inline Mat4 MatMul(const Mat4& a, const Mat4& b)
    {
        Mat4 r{};
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
            {
                float s = 0.0f;
                for (int k = 0; k < 4; ++k) s += a.m[i][k] * b.m[k][j];
                r.m[i][j] = s;
            }
        return r;
    }

    /** @brief Affine inverse (3x3 inverse plus rebased translation row). */
    inline Mat4 MatInverse(const Mat4& s)
    {
        const auto& a = s.m;
        const float det = a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1])
                        - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0])
                        + a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]);
        const float d = (det != 0.0f) ? 1.0f / det : 0.0f;
        Mat4 r{};
        r.m[0][0] = (a[1][1] * a[2][2] - a[1][2] * a[2][1]) * d;
        r.m[0][1] = (a[0][2] * a[2][1] - a[0][1] * a[2][2]) * d;
        r.m[0][2] = (a[0][1] * a[1][2] - a[0][2] * a[1][1]) * d;
        r.m[1][0] = (a[1][2] * a[2][0] - a[1][0] * a[2][2]) * d;
        r.m[1][1] = (a[0][0] * a[2][2] - a[0][2] * a[2][0]) * d;
        r.m[1][2] = (a[0][2] * a[1][0] - a[0][0] * a[1][2]) * d;
        r.m[2][0] = (a[1][0] * a[2][1] - a[1][1] * a[2][0]) * d;
        r.m[2][1] = (a[0][1] * a[2][0] - a[0][0] * a[2][1]) * d;
        r.m[2][2] = (a[0][0] * a[1][1] - a[0][1] * a[1][0]) * d;
        for (int j = 0; j < 3; ++j)
            r.m[3][j] = -(a[3][0] * r.m[0][j] + a[3][1] * r.m[1][j] + a[3][2] * r.m[2][j]);
        r.m[3][3] = 1.0f;
        return r;
    }

    inline Mat4 MatFromQuat(const Quat& q)
    {
        float x = q[0], y = q[1], z = q[2], w = q[3];
        const float n = std::sqrt(x * x + y * y + z * z + w * w);
        if (n > 0.0f) { x /= n; y /= n; z /= n; w /= n; }
        Mat4 r = MatIdentity();
        r.m[0][0] = 1 - 2 * (y * y + z * z); r.m[0][1] = 2 * (x * y + z * w);     r.m[0][2] = 2 * (x * z - y * w);
        r.m[1][0] = 2 * (x * y - z * w);     r.m[1][1] = 1 - 2 * (x * x + z * z); r.m[1][2] = 2 * (y * z + x * w);
        r.m[2][0] = 2 * (x * z + y * w);     r.m[2][1] = 2 * (y * z - x * w);     r.m[2][2] = 1 - 2 * (x * x + y * y);
        return r;
    }

    /** @brief Quaternion from an orthonormal row-vector rotation matrix. */
    inline Quat QuatFromMat(const Mat4& r)
    {
        const auto& m = r.m;
        float x, y, z, w;
        const float tr = m[0][0] + m[1][1] + m[2][2];
        if (tr > 0)
        {
            const float s = std::sqrt(tr + 1.0f) * 2;
            w = 0.25f * s;
            x = (m[1][2] - m[2][1]) / s;
            y = (m[2][0] - m[0][2]) / s;
            z = (m[0][1] - m[1][0]) / s;
        }
        else if (m[0][0] > m[1][1] && m[0][0] > m[2][2])
        {
            const float s = std::sqrt(1.0f + m[0][0] - m[1][1] - m[2][2]) * 2;
            w = (m[1][2] - m[2][1]) / s;
            x = 0.25f * s;
            y = (m[1][0] + m[0][1]) / s;
            z = (m[2][0] + m[0][2]) / s;
        }
        else if (m[1][1] > m[2][2])
        {
            const float s = std::sqrt(1.0f + m[1][1] - m[0][0] - m[2][2]) * 2;
            w = (m[2][0] - m[0][2]) / s;
            x = (m[1][0] + m[0][1]) / s;
            y = 0.25f * s;
            z = (m[2][1] + m[1][2]) / s;
        }
        else
        {
            const float s = std::sqrt(1.0f + m[2][2] - m[0][0] - m[1][1]) * 2;
            w = (m[0][1] - m[1][0]) / s;
            x = (m[2][0] + m[0][2]) / s;
            y = (m[2][1] + m[1][2]) / s;
            z = 0.25f * s;
        }
        return { x, y, z, w };
    }

    inline Mat4 ComposeSRT(const Vec3& scale, const Quat& rot, const Vec3& loc)
    {
        Mat4 r = MatFromQuat(rot);
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                r.m[i][j] *= scale[i];
        r.m[3][0] = loc[0]; r.m[3][1] = loc[1]; r.m[3][2] = loc[2];
        return r;
    }

    /** @brief Splits an affine matrix into row scales, unit rotation, and translation. */
    inline void DecomposeSRT(const Mat4& z, Vec3& scale, Quat& rot, Vec3& loc)
    {
        Mat4 r = MatIdentity();
        for (int i = 0; i < 3; ++i)
        {
            float s = std::sqrt(z.m[i][0] * z.m[i][0] + z.m[i][1] * z.m[i][1] + z.m[i][2] * z.m[i][2]);
            if (s == 0.0f) s = 1.0f;
            scale[i] = s;
            for (int j = 0; j < 3; ++j) r.m[i][j] = z.m[i][j] / s;
        }
        rot = QuatFromMat(r);
        loc = { z.m[3][0], z.m[3][1], z.m[3][2] };
    }

    inline Mat4 Translation(const Vec3& v)
    {
        Mat4 r = MatIdentity();
        r.m[3][0] = v[0]; r.m[3][1] = v[1]; r.m[3][2] = v[2];
        return r;
    }

    inline Vec3 RotatePoint(const Vec3& v, const Mat4& m)
    {
        return { v[0] * m.m[0][0] + v[1] * m.m[1][0] + v[2] * m.m[2][0] + m.m[3][0],
                 v[0] * m.m[0][1] + v[1] * m.m[1][1] + v[2] * m.m[2][1] + m.m[3][1],
                 v[0] * m.m[0][2] + v[1] * m.m[1][2] + v[2] * m.m[2][2] + m.m[3][2] };
    }

    inline Vec3 RotateDir(const Vec3& v, const Mat4& m)
    {
        return { v[0] * m.m[0][0] + v[1] * m.m[1][0] + v[2] * m.m[2][0],
                 v[0] * m.m[0][1] + v[1] * m.m[1][1] + v[2] * m.m[2][1],
                 v[0] * m.m[0][2] + v[1] * m.m[1][2] + v[2] * m.m[2][2] };
    }

    inline Vec3 LerpV3(const Vec3& a, const Vec3& b, float f)
    {
        return { a[0] + (b[0] - a[0]) * f, a[1] + (b[1] - a[1]) * f, a[2] + (b[2] - a[2]) * f };
    }

    /** @brief Normalized lerp with hemisphere alignment. */
    inline Quat NlerpQ(const Quat& a, Quat b, float f)
    {
        float dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
        if (dot < 0) for (auto& c : b) c = -c;
        Quat q{};
        for (int i = 0; i < 4; ++i) q[i] = a[i] + (b[i] - a[i]) * f;
        float n = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
        if (n == 0.0f) n = 1.0f;
        for (auto& c : q) c /= n;
        return q;
    }

    /**
     * @brief The model-frame fix: source models face -Y and the client expects +X, so
     *        the whole frame yaws +90 degrees; an optional z lift rides the same matrix.
     */
    inline Mat4 ModelFrame(float lift)
    {
        Mat4 g{};
        g.m[0][1] = 1.0f; g.m[1][0] = -1.0f; g.m[2][2] = 1.0f;
        g.m[3][2] = lift; g.m[3][3] = 1.0f;
        return g;
    }
}
