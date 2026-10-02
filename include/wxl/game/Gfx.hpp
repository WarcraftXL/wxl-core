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

#pragma once

#include <cstddef>
#include <cstdint>

#include "wxl/game/Gx.hpp"

/**
 * @brief Shapes placed in the world by world coordinate, drawn inside the scene.
 *
 * A module queues shapes wherever it computes them and calls Flush once, from the world-to-interface
 * boundary of the frame. Nothing is retained: the queue is emptied by the flush, so a shape is shown
 * for exactly as long as something keeps asking for it, and a module that stops asking leaves nothing
 * behind to clean up.
 *
 * The camera is not a parameter. The shapes are transformed by the same view and projection the scene
 * was drawn with, read from the device at flush time, so a shape lands where its coordinates say it
 * does without the caller knowing anything about the camera.
 *
 * Two things about when to call which, neither of them guessable:
 *
 * The queue belongs to the binary it is compiled into, not to the process. Each extension carries its
 * own, so no module can disturb another's -- and equally, no module can rely on another to empty its
 * own. Whoever queues is who flushes.
 *
 * The ground-following shapes are priced in collision queries, one per vertex, and belong off the draw
 * path for that reason alone. A module that uses them queues on OnUpdate and flushes on
 * OnWorldSceneEnd: the shapes are decided on the client's own tick, and the draw only hands over what
 * is already decided.
 */
namespace wxl::game::gfx
{
    /// Colour as 0xAARRGGBB. Alpha is honoured: the shapes are blended.
    using Color = uint32_t;

    namespace color
    {
        constexpr Color kWhite  = 0xFFFFFFFF;
        constexpr Color kRed    = 0xFFFF0000;
        constexpr Color kGreen  = 0xFF00FF00;
        constexpr Color kBlue   = 0xFF4080FF;
        constexpr Color kYellow = 0xFFFFFF00;
        constexpr Color kCyan   = 0xFF00FFFF;
        constexpr Color kOrange = 0xFFFF8000;
        constexpr Color kGrey   = 0xFF808080;
    }

    /**
     * @brief Whether the scene may hide a shape.
     *
     * Tested is the honest reading: a shape behind a hill is behind it. Through is for shapes whose
     * whole purpose is to be found -- a marker on something out of sight is useless if the thing
     * hiding it also hides the marker.
     */
    enum class Depth { Tested, Through };

    /**
     * @brief Queues a segment between two world points.
     * @param a,b    World positions x, y, z.
     * @param c      Colour.
     * @param depth  Whether the scene may hide it.
     */
    void Line(const float a[3], const float b[3], Color c, Depth depth = Depth::Tested);

    /**
     * @brief Queues the twelve edges of an axis-aligned box.
     * @param min,max  Opposite corners, world axes.
     */
    void Box(const float min[3], const float max[3], Color c, Depth depth = Depth::Tested);

    /**
     * @brief Queues an axis-aligned box given its middle and half-sizes.
     * @param center   World position of the middle.
     * @param extents  Half-size along each axis.
     */
    void BoxAt(const float center[3], const float extents[3], Color c, Depth depth = Depth::Tested);

    /**
     * @brief Queues a horizontal ring, the shape a ground marker takes.
     * @param center    World position of the middle.
     * @param radius    Ring radius.
     * @param segments  Straight pieces the ring is drawn as.
     *
     * Flat, at the height given. A ring on sloping ground therefore cuts into the hillside on one
     * side and floats on the other; following the ground is a projection, not a shape.
     */
    void Circle(const float center[3], float radius, Color c, Depth depth = Depth::Tested,
                int segments = 32);

    /**
     * @brief Queues three rings at right angles, which read as a sphere.
     * @param center    World position of the middle.
     * @param radius    Sphere radius.
     * @param segments  Straight pieces each ring is drawn as.
     */
    void Sphere(const float center[3], float radius, Color c, Depth depth = Depth::Tested,
                int segments = 24);

    /**
     * @brief Queues three axis-aligned segments crossing at a point, which marks it without hiding it.
     * @param p     World position.
     * @param size  Half-length of each arm.
     */
    void Cross(const float p[3], float size, Color c, Depth depth = Depth::Tested);

    /**
     * @brief Queues a segment with a head, so that it reads as pointing rather than merely joining.
     * @param from,to   World positions.
     * @param headSize  Length of the head's barbs.
     */
    void Arrow(const float from[3], const float to[3], float headSize, Color c,
               Depth depth = Depth::Tested);

    /**
     * @brief Draws everything queued and empties the queue.
     * @param dev  The device to draw on.
     *
     * Belongs on OnWorldSceneEnd, and effectively nowhere else. The matrices are read from the device
     * here, and the world pass's caller puts the pre-world ones back the moment that pass returns --
     * so a flush from any later point in the frame transforms every shape by a camera that is not the
     * one the world was seen through, and nothing lands on screen. That slot is also late enough for
     * the scene's depth to be complete, which is what makes Depth::Tested mean what it says.
     *
     * The device is left as it was found. Nothing is drawn, and the queue is still emptied, when the
     * graphics device is not up -- a module's shapes never accumulate behind its back.
     *
     * @param sceneDepth  The surface the world was drawn into, from OnWorldSceneEnd. Bound for the
     *                    duration of the draw and put back afterwards. Pass it whenever any queued
     *                    shape is Depth::Tested: by this point in the frame the bound surface may be
     *                    one a post-process pass left, and depth-testing against a surface the world
     *                    never wrote to rejects every pixel silently. Omitting it makes Depth::Tested
     *                    a guess; Depth::Through does not care either way.
     * @return The device's result for the last draw issued, or 0 when none was. A refused draw and a
     *         draw that lands off screen are indistinguishable on a bare screen, and only one of them
     *         is worth looking for a camera to explain.
     */
    long Flush(gx::Device9 dev, void* sceneDepth = nullptr);

    /**
     * @brief Queues a filled triangle.
     * @param a,b,c  World positions. Wound either way: the flush draws both faces.
     *
     * Blended like everything else here, so an alpha below full is what makes a filled shape read as
     * a marking on the world rather than a lid over it.
     */
    void Triangle(const float a[3], const float b[3], const float c[3], Color c0,
                  Depth depth = Depth::Tested);

    /**
     * @brief Queues a filled disc lying on the ground, following whatever the ground does.
     * @param center    World position; only x and y select the place, z says where to search from.
     * @param radius    Disc radius.
     * @param segments  Pieces around the rim.
     * @param rings     Bands between the middle and the rim. One band spans the whole radius and so
     *                  only follows the ground at the rim; more bands follow it further in, at the
     *                  cost of a surface query each.
     *
     * Every vertex is dropped onto the surface beneath it, which is what makes this a marking on the
     * terrain rather than a plate hovering over it. That costs one collision query per vertex, so a
     * disc is priced in queries -- segments x rings of them, each frame it is asked for. Ask for the
     * shape you need rather than the smoothest one available.
     */
    void GroundDisc(const float center[3], float radius, Color c, int segments = 32, int rings = 3,
                    Depth depth = Depth::Tested);

    /**
     * @brief Queues an outline on the ground, following whatever the ground does.
     * @param center    World position; x and y select the place, z says where to search from.
     * @param radius    Ring radius.
     * @param segments  Pieces around the ring, one surface query each.
     *
     * Reads far more sharply than a filled disc alone, and costs a fraction of it. A disc with a ring
     * over it is the pair that looks deliberate.
     */
    void GroundRing(const float center[3], float radius, Color c, int segments = 48,
                    Depth depth = Depth::Tested);

    /**
     * @brief Reads the view and projection the scene was last drawn with.
     * @param view        Receives sixteen floats.
     * @param projection  Receives sixteen floats.
     * @return False when the graphics device is not up, leaving both untouched.
     *
     * Only meaningful while those matrices are still the ones on the device -- the same window Flush
     * is restricted to. Exposed because a module that projects world points itself needs exactly what
     * the flush uses, and two readings of it would be two things to keep in step.
     */
    bool SceneMatrices(float view[16], float projection[16]);

    /// Empties the queue without drawing it.
    void Clear();

    /// How many segments are queued. All the shapes above are segments, so this is what a flush costs.
    size_t Pending();
}
