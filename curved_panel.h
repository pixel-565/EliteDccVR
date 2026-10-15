#pragma once
#include "panel_hit.h"

// SteamVR curvature is the fraction of a full cylinder; width is the arc length.
struct CurvedPanel
{
    static constexpr float Pi = 3.14159265358979323846f;
    static V point(V center, V right, V up, V normal, float width, float height, float degrees, float u,
                   float v)
    {
        if (degrees < .01f)
            return center + right * ((u - .5f) * width) + up * ((v - .5f) * height);
        float angle = degrees * Pi / 180.f, radius = width / angle, theta = (u - .5f) * angle;
        return center + right * (radius * std::sin(theta)) + normal * (radius * (1 - std::cos(theta))) +
               up * ((v - .5f) * height);
    }
    static bool hit(V origin, V direction, V center, V right, V up, V normal, float width, float height,
                    float degrees, vr::VROverlayIntersectionResults_t &result, bool bounded = true)
    {
        if (width <= 0 || height <= 0 || !std::isfinite(degrees))
            return false;
        if (degrees < .01f)
        {
            if (bounded)
                return intersectPanel(origin, direction, center, right, up, normal, width, height, result);
            float d = dot(direction, normal);
            if (!std::isfinite(d) || d >= -.0001f)
                return false;
            float t = dot(center - origin, normal) / d;
            if (!std::isfinite(t) || t < .05f || t > 20)
                return false;
            V p = origin + direction * t;
            result = {};
            result.fDistance = t;
            result.vPoint = {{p.x, p.y, p.z}};
            result.vNormal = {{normal.x, normal.y, normal.z}};
            result.vUVs = {{dot(p - center, right) / width + .5f, dot(p - center, up) / height + .5f}};
            return true;
        }
        double angle = degrees * Pi / 180., radius = width / angle;
        V local = origin - center;
        double ox = dot(local, right), oz = dot(local, normal) - radius;
        double dx = dot(direction, right), dz = dot(direction, normal);
        double a = dx * dx + dz * dz, b = 2 * (ox * dx + oz * dz), c = ox * ox + oz * oz - radius * radius;
        double disc = b * b - 4 * a * c;
        if (a < 1.e-10 || disc < 0 || !std::isfinite(disc))
            return false;
        double roots[2] = {(-b - std::sqrt(disc)) / (2 * a), (-b + std::sqrt(disc)) / (2 * a)};
        for (double t : roots)
        {
            if (t < .0001 || t > 20 || !std::isfinite(t))
                continue;
            V p = origin + direction * float(t), relative = p - center;
            double theta = std::atan2(dot(relative, right), radius - dot(relative, normal));
            float u = float(theta / angle + .5), v = dot(relative, up) / height + .5f;
            V n = right * float(-std::sin(theta)) + normal * float(std::cos(theta));
            if (dot(direction, n) >= -.0001f)
                continue;
            if (bounded && (u < -.00001f || u > 1.00001f || v < -.00001f || v > 1.00001f))
                continue;
            result = {};
            result.fDistance = float(t);
            result.vPoint = {{p.x, p.y, p.z}};
            result.vNormal = {{n.x, n.y, n.z}};
            result.vUVs = {{bounded ? std::clamp(u, 0.f, 1.f) : u, bounded ? std::clamp(v, 0.f, 1.f) : v}};
            return true;
        }
        return false;
    }
};
