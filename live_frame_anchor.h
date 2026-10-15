#pragma once
#include <array>
#include <algorithm>
#include <cstdint>
// Discrete placement, never a filtered or continuously corrected mesh.
// Recenter inference is conservative: stationary, persistent changes only.
template <size_t Count> struct LiveFrameAnchor
{
    using Points = std::array<V, Count>;
    using Model = std::array<uint32_t, 7>;
    static constexpr uint64_t MaxGapMs = 100;
    uint64_t settleMs = 300;
    static int interval(int milliseconds)
    {
        return ((std::clamp(milliseconds, 10, 1000) + 2) / 5) * 5;
    }
    void setInterval(int milliseconds)
    {
        auto next = static_cast<uint64_t>(interval(milliseconds));
        if (next != settleMs)
        {
            settleMs = next;
            pause();
        }
    }
    static constexpr float ChangeMeters = .015f, StableMeters = .004f;
    Points points{}, candidate{};
    Model model{}, candidateModel{};
    V viewer{};
    bool valid = false, pending = false;
    uint64_t revision = 0, lastSerial = 0, lastTime = 0, started = 0;
    uint32_t pid = 0;
    void pause()
    {
        pending = false;
        started = 0;
        lastTime = 0;
    }
    void reset()
    {
        valid = false;
        pid = 0;
        lastSerial = 0;
        pause();
        ++revision;
    }
    static float difference(const Points &a, const Points &b)
    {
        float maximum = 0;
        for (size_t i = 0; i < Count; ++i)
            maximum = std::max(maximum, dot(a[i] - b[i], a[i] - b[i]));
        return std::sqrt(maximum);
    }
    bool observe(const Points &next, V head, const Model &key, uint32_t process, uint64_t serial,
                 uint64_t time, bool still)
    {
        if (pid != process)
        {
            reset();
            pid = process;
        }
        if (!still)
        {
            pause();
            lastSerial = serial;
            return false;
        }
        if (serial == lastSerial)
            return false;
        lastSerial = serial;
        for (const auto &p : next)
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
            {
                pause();
                return false;
            }
        if (valid && model == key && difference(next, points) <= ChangeMeters)
        {
            pause();
            return false;
        }
        if (!pending || time < lastTime || time - lastTime > MaxGapMs || key != candidateModel ||
            difference(next, candidate) > StableMeters)
        {
            candidate = next;
            candidateModel = key;
            started = time;
            pending = true;
        }
        lastTime = time;
        if (time - started < settleMs)
            return false;
        // Keep one complete observed mesh. Do not average, reshape or move it
        // again until another independently confirmed placement change.
        points = next;
        model = key;
        viewer = head;
        valid = true;
        pending = false;
        started = 0;
        ++revision;
        return true;
    }
};
