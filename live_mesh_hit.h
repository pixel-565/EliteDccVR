#pragma once
// Intersect the actual triangles and interpolate their captured texture coordinates.
struct LiveMeshHit
{
    float distance = 0, u = 0, v = 0;
    V point{}, normal{};
};
template <size_t Count, size_t IndexCount>
bool intersectLiveMesh(V origin, V direction, const std::array<V, Count> &points, const float (&uv)[Count][2],
                       const uint16_t (&indices)[IndexCount], LiveMeshHit &hit)
{
    float nearest = 1e9f;
    bool found = false;
    for (size_t i = 0; i + 2 < IndexCount; i += 3)
    {
        auto a = indices[i], b = indices[i + 1], c = indices[i + 2];
        if (a >= Count || b >= Count || c >= Count)
            return false;
        V e1 = points[b] - points[a], e2 = points[c] - points[a], p = cross(direction, e2);
        float determinant = dot(e1, p);
        if (!std::isfinite(determinant) || std::abs(determinant) < 1e-8f)
            continue;
        float inverse = 1.f / determinant;
        V t = origin - points[a];
        float x = dot(t, p) * inverse;
        if (x < -.00001f || x > 1.00001f)
            continue;
        V q = cross(t, e1);
        float y = dot(direction, q) * inverse;
        if (y < -.00001f || x + y > 1.00001f)
            continue;
        float distance = dot(e2, q) * inverse;
        if (!std::isfinite(distance) || distance <= .001f || distance >= nearest)
            continue;
        float u = (1 - x - y) * uv[a][0] + x * uv[b][0] + y * uv[c][0];
        float v = (1 - x - y) * uv[a][1] + x * uv[b][1] + y * uv[c][1];
        if (!std::isfinite(u) || !std::isfinite(v) || u < -.01f || u > 1.01f || v < -.01f || v > 1.01f)
            return false;
        nearest = distance;
        found = true;
        hit = {distance, std::clamp(u, 0.f, 1.f), std::clamp(v, 0.f, 1.f), origin + direction * distance,
               unit(cross(e1, e2))};
        if (dot(hit.normal, direction) > 0)
            hit.normal = hit.normal * -1.f;
    }
    return found;
}
