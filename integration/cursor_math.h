#pragma once
#include <cmath>
#include <algorithm>
namespace CursorMath
{
struct P
{
    float x, y, z;
    P operator+(P b) const
    {
        return {x + b.x, y + b.y, z + b.z};
    }
    P operator-(P b) const
    {
        return {x - b.x, y - b.y, z - b.z};
    }
    P operator*(float s) const
    {
        return {x * s, y * s, z * s};
    }
};
inline float dot(P a, P b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
struct Hit
{
    float distance = 1e9f, u = 0, v = 0;
};
inline void triangle(P p, const P *points, const float uv[][2], const uint16_t *indices, Hit &hit)
{
    P a = points[indices[0]], b = points[indices[1]], c = points[indices[2]], ab = b - a, ac = c - a,
      ap = p - a;
    auto test = [&](float x, float y, float z) {
        P q = a * x + b * y + c * z;
        float dist = std::sqrt(dot(p - q, p - q));
        if (std::isfinite(dist) && dist < hit.distance)
        {
            hit.distance = dist;
            hit.u = x * uv[indices[0]][0] + y * uv[indices[1]][0] + z * uv[indices[2]][0];
            hit.v = x * uv[indices[0]][1] + y * uv[indices[1]][1] + z * uv[indices[2]][1];
        }
    };
    float aa = dot(ab, ab), bb = dot(ab, ac), cc = dot(ac, ac), dd = dot(ap, ab), ee = dot(ap, ac),
          den = aa * cc - bb * bb;
    if (den > 1e-12f)
    {
        float y = (dd * cc - ee * bb) / den, z = (ee * aa - dd * bb) / den;
        if (y >= 0 && z >= 0 && y + z <= 1)
            test(1 - y - z, y, z);
    }
    if (aa > 1e-12f)
    {
        float t = std::clamp(dd / aa, 0.f, 1.f);
        test(1 - t, t, 0);
    }
    if (cc > 1e-12f)
    {
        float t = std::clamp(ee / cc, 0.f, 1.f);
        test(1 - t, 0, t);
    }
    P bc = c - b;
    float len = dot(bc, bc);
    if (len > 1e-12f)
    {
        float t = std::clamp(dot(p - b, bc) / len, 0.f, 1.f);
        test(0, 1 - t, t);
    }
}
} // namespace CursorMath
