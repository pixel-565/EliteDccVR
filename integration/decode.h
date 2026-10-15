#pragma once
#include <cmath>
#include <cstring>
#include <cstdint>
#include <bit>
namespace UiDecode
{
struct Vector
{
    float x, y, z;
};
inline uint32_t word(const void *data, unsigned offset)
{
    uint32_t v;
    memcpy(&v, static_cast<const char *>(data) + offset, 4);
    return v;
}
inline float number(const void *data, unsigned offset)
{
    return std::bit_cast<float>(word(data, offset));
}
inline bool position(const void *record, const void *model, const void *cb1, Vector &result)
{
    uint32_t x = word(record, 0), y = word(record, 4), z = word(record, 8), e = (z >> 24) & 127;
    if (e == 64 || word(model, 0) != 0)
        return false;
    uint32_t divisor = (uint32_t(1) << ((20 - e) & 31)) - 1, bias = uint32_t(1) << ((z >> 24) & 31);
    if (!divisor)
        return false;
    float v[3] = {float(x & 0x1fffff) / divisor - bias,
                  float((x >> 21) + ((y & 1023) << 11)) / divisor - bias,
                  float((y >> 10) & 0x1fffff) / divisor - bias};
    uint32_t packed[2] = {word(model, 8), word(model, 12)};
    float q[4];
    for (int i = 0; i < 4; ++i)
        q[i] = float((packed[i / 2] >> ((i % 2) * 16)) & 65535) * std::bit_cast<float>(uint32_t(0x38000100)) -
               1.f;
    float d = q[0] * v[0] + q[1] * v[1] + q[2] * v[2], k = 2 * q[3] * q[3] - 1, scale = number(model, 4);
    float rotated[3] = {k * v[0] + 2 * d * q[0] + 2 * q[3] * (q[1] * v[2] - q[2] * v[1]),
                        k * v[1] + 2 * d * q[1] + 2 * q[3] * (q[2] * v[0] - q[0] * v[2]),
                        k * v[2] + 2 * d * q[2] + 2 * q[3] * (q[0] * v[1] - q[1] * v[0])};
    float out[3];
    for (int i = 0; i < 3; ++i)
    {
        out[i] = rotated[i] * scale + number(model, 16 + i * 4) - number(cb1, 4400 + i * 4);
        if (!std::isfinite(out[i]))
            return false;
    }
    result = {out[0], out[1], out[2]};
    return true;
}
inline bool project(Vector p, const void *cb0, float *clip)
{
    for (unsigned r = 0; r < 4; ++r)
    {
        unsigned base = 64 + r * 16;
        clip[r] = p.x * number(cb0, base) + p.y * number(cb0, base + 4) + p.z * number(cb0, base + 8) +
                  number(cb0, base + 12);
        if (!std::isfinite(clip[r]))
            return false;
    }
    // This is homogeneous geometry, not a visibility test. A partially visible
    // mesh may have vertices behind the eye; SteamVR clips individual segments.
    return true;
}
inline bool texcoord(const void *record, float *uv)
{
    uint32_t packed = word(record, 16);
    for (unsigned i = 0; i < 2; ++i)
    {
        uv[i] =
            (float((packed >> (i * 16)) & 65535) * std::bit_cast<float>(uint32_t(0x38000100)) - 1.f) * 16.f;
        if (!std::isfinite(uv[i]) || uv[i] < -.01f || uv[i] > 1.01f)
            return false;
    }
    return true;
}
} // namespace UiDecode
