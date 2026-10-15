#pragma once
#include "curved_panel.h"
// The opposite top corner and panel orientation stay fixed during a corner drag.
struct BrowserResize
{
    int corner = 0;
    float sideFactor = .5f, depthFactor = 0;
    V anchor{}, right{}, up{}, normal{}, offset{};
    static constexpr float MinWidth = .9f, MaxWidth = 2.5f, MinHeight = .7f, MaxHeight = 2.f;
    static bool intersect(V origin, V direction, V point, V normal, V &hit)
    {
        float denominator = dot(direction, normal);
        if (std::abs(denominator) < .08f)
            return false;
        float t = dot(point - origin, normal) / denominator;
        if (!std::isfinite(t) || t < .05f || t > 10.f)
            return false;
        hit = origin + direction * t;
        return true;
    }
    bool begin(int side, V center, V r, V u, V n, float width, float height, V origin, V direction,
               float degrees = 0)
    {
        float angle = degrees * CurvedPanel::Pi / 180.f;
        sideFactor = angle < .0001f ? .5f : std::sin(angle * .5f) / angle;
        depthFactor = angle < .0001f ? 0.f : (1.f - std::cos(angle * .5f)) / angle;
        V edgeCenter = center + n * (width * depthFactor);
        V hit;
        if (!intersect(origin, direction, edgeCenter, n, hit))
            return false;
        corner = side;
        right = r;
        up = u;
        normal = n;
        anchor = edgeCenter - r * (side * width * sideFactor) + u * (height * .5f);
        offset = edgeCenter + r * (side * width * sideFactor) - u * (height * .5f) - hit;
        return true;
    }
    bool update(V origin, V direction, V &center, float &width, float &height) const
    {
        V hit;
        if (!corner || !intersect(origin, direction, anchor, normal, hit))
            return false;
        V delta = hit + offset - anchor;
        width = std::clamp(corner * dot(delta, right) / (2.f * sideFactor), MinWidth, MaxWidth);
        height = std::clamp(-dot(delta, up), std::max(MinHeight, width * .52f), MaxHeight);
        center = anchor + right * (corner * width * sideFactor) - normal * (width * depthFactor) -
                 up * (height * .5f);
        return true;
    }
};
