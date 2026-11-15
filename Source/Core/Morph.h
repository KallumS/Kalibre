/*
    Morph - the X/Y pad's arithmetic (decision 0018), from VST-Morpher's
    MorphMath.h, unchanged in substance.

    Four corners, each a plug-in or a sample. x runs 0 (left) to 1 (right)
    and y 0 (bottom) to 1 (top), so A is top left, B top right, C bottom left
    and D bottom right. The weights are bilinear and always add up to 1, so
    the sounds are mixed at their own level.

    No JUCE in here.
*/

#pragma once

#include <array>

namespace kal::morph
{
inline constexpr int corners = 4;

enum Corner { cornerA = 0, cornerB = 1, cornerC = 2, cornerD = 3 };

inline const char* cornerName (int corner)
{
    static constexpr const char* names[] { "A", "B", "C", "D" };
    return (corner >= 0 && corner < corners) ? names[corner] : "?";
}

inline std::array<float, corners> weightsAt (float x, float y) noexcept
{
    x = x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
    y = y < 0.0f ? 0.0f : (y > 1.0f ? 1.0f : y);
    return { (1.0f - x) * y,             // A, top left
             x * y,                      // B, top right
             (1.0f - x) * (1.0f - y),    // C, bottom left
             x * (1.0f - y) };           // D, bottom right
}

inline std::array<float, 2> cornerPosition (int corner) noexcept
{
    switch (corner)
    {
        case cornerA: return { 0.0f, 1.0f };
        case cornerB: return { 1.0f, 1.0f };
        case cornerC: return { 0.0f, 0.0f };
        default:      return { 1.0f, 0.0f };
    }
}

// Equal-power version of the same weights, for samples that don't share a
// level: the square roots, so the summed power stays at 1.
inline std::array<float, corners> powerWeightsAt (float x, float y) noexcept;
} // namespace kal::morph

#include <cmath>
inline std::array<float, kal::morph::corners> kal::morph::powerWeightsAt (float x, float y) noexcept
{
    auto w = weightsAt (x, y);
    for (auto& v : w) v = std::sqrt (v);
    return w;
}
