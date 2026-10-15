#pragma once
// Use the same physical rectangle for input and rendering, independently of runtime texture metadata.
inline bool intersectPanel(V origin, V direction, V center, V right, V up, V normal, float width,
                           float height, vr::VROverlayIntersectionResults_t &result)
{
    float denominator = dot(direction, normal);
    if (!std::isfinite(denominator) || denominator >= -0.0001f || width <= 0 || height <= 0)
        return false;
    float distance = dot(center - origin, normal) / denominator;
    if (!std::isfinite(distance) || distance < 0)
        return false;
    V point = origin + direction * distance, relative = point - center;
    float x = dot(relative, right), y = dot(relative, up);
    if (!std::isfinite(x) || !std::isfinite(y) || std::abs(x) > width * .5f + .00001f ||
        std::abs(y) > height * .5f + .00001f)
        return false;
    result = {};
    result.vPoint = {{point.x, point.y, point.z}};
    result.vNormal = {{normal.x, normal.y, normal.z}};
    result.vUVs = {{std::clamp(x / width + .5f, 0.f, 1.f), std::clamp(y / height + .5f, 0.f, 1.f)}};
    result.fDistance = distance;
    return true;
}
