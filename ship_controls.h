#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace ShipControl
{
inline int row(float u, float v)
{
    if (!std::isfinite(u) || !std::isfinite(v) || u < .02f || u > .98f) return -1;
    if (v >= 132.f/294 && v <= 168.f/294)
    {
        for (int i=0;i<4;++i)
            if (u >= (4.f+67*i)/269 && u <= (65.f+67*i)/269) return 3+i;
        return -1;
    }
    constexpr float top[] = {171.f / 294, 210.f / 294, 250.f / 294};
    constexpr float bottom[] = {207.f / 294, 246.f / 294, 286.f / 294};
    for (int i = 0; i < 3; ++i) if (v >= top[i] && v <= bottom[i]) return i;
    return -1;
}
inline int selection(const unsigned char *pixels, int count = 7)
{
    int levels[7]{};
    if (count != 3 && count != 7) return -1;
    for (int i = 0; i < count; ++i)
        levels[i] = std::max({int(pixels[i*4]), int(pixels[i*4+1]), int(pixels[i*4+2])});
    int best = int(std::max_element(levels, levels+count)-levels), second = 0;
    for (int i = 0; i < count; ++i) if (i != best) second = std::max(second, levels[i]);
    return levels[best] >= 90 && levels[best] > second + 35 && levels[best] > second * 1.8f ? best : -1;
}
// Only confirmed feedback may trigger Select. No blind navigation or queued action across targets.
struct Navigation
{
    int target = -1, previousSelection = -1, attempts = 0;
    uint64_t aimedAt = 0, lastAction = 0, actionSample = 0, confirmUntil = 0;
    void reset() { *this = {}; }
    int update(uint64_t now, bool valid, int aimed, int selected, uint64_t sample, bool pressed, bool busy)
    {
        if (!valid || aimed < 0 || aimed > 6 || !sample || now < sample || now-sample > 200)
        { reset(); return -1; }
        if (aimed != target)
        { reset(); target = aimed; aimedAt = now; }
        if (pressed) { confirmUntil = now + 1500; attempts = 0; }
        if (confirmUntil && now > confirmUntil) confirmUntil = 0;
        if (busy || now-aimedAt < 100 || selected < 0 || selected > 6 ||
            (lastAction && (now-lastAction < 160 || sample <= actionSample))) return -1;
        if (selected == target)
        {
            attempts = 0;
            if (!confirmUntil) return -1;
            confirmUntil = 0; lastAction = now; actionSample = sample;
            return 4;
        }
        if (selected != previousSelection) attempts = 0;
        if (attempts >= 3) { confirmUntil = 0; return -1; }
        ++attempts; previousSelection = selected; lastAction = now; actionSample = sample;
        if (selected >= 3)
            return target >= 3 ? (selected > target ? 2 : 3) : 1;
        if (target >= 3) return 0;
        return selected > target ? 0 : 1;
    }
};
}
