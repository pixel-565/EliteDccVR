#pragma once
#include <deque>
#include <cstdint>
// Short-lived tracking samples only; no user calibration or persisted transform.
struct LivePoseHistory
{
    struct Sample
    {
        uint64_t stamp;
        vr::HmdMatrix34_t pose;
    };
    std::deque<Sample> samples;
    void clear()
    {
        samples.clear();
    }
    void add(uint64_t stamp, const vr::HmdMatrix34_t &pose)
    {
        if (!samples.empty() && stamp <= samples.back().stamp)
            return;
        samples.push_back({stamp, pose});
        while (samples.size() > 256 || (!samples.empty() && stamp - samples.front().stamp > 1000))
            samples.pop_front();
    }
    bool at(uint64_t stamp, vr::HmdMatrix34_t &result) const
    {
        if (samples.empty() || stamp < samples.front().stamp || stamp > samples.back().stamp)
            return false;
        for (size_t i = 0; i < samples.size(); ++i)
        {
            if (samples[i].stamp == stamp)
            {
                result = samples[i].pose;
                return true;
            }
            if (samples[i].stamp < stamp)
                continue;
            const auto &a = samples[i - 1];
            const auto &b = samples[i];
            if (b.stamp - a.stamp > 80)
                return false;
            float t = float(stamp - a.stamp) / float(b.stamp - a.stamp);
            V axes[4];
            for (int j = 0; j < 4; ++j)
                axes[j] = {a.pose.m[0][j] * (1 - t) + b.pose.m[0][j] * t,
                           a.pose.m[1][j] * (1 - t) + b.pose.m[1][j] * t,
                           a.pose.m[2][j] * (1 - t) + b.pose.m[2][j] * t};
            // Re-orthogonalize the short-interval interpolation to preserve mesh dimensions.
            if (dot(axes[0], axes[0]) < .5f || dot(axes[1], axes[1]) < .5f)
                return false;
            axes[0] = unit(axes[0]);
            axes[1] = axes[1] - axes[0] * dot(axes[0], axes[1]);
            if (dot(axes[1], axes[1]) < .5f)
                return false;
            axes[1] = unit(axes[1]);
            axes[2] = unit(cross(axes[0], axes[1]));
            result = matrix(axes[0], axes[1], axes[2], axes[3]);
            return true;
        }
        return false;
    }
    static V transform(const vr::HmdMatrix34_t &m, V p)
    {
        return {m.m[0][0] * p.x + m.m[0][1] * p.y + m.m[0][2] * p.z + m.m[0][3],
                m.m[1][0] * p.x + m.m[1][1] * p.y + m.m[1][2] * p.z + m.m[1][3],
                m.m[2][0] * p.x + m.m[2][1] * p.y + m.m[2][2] * p.z + m.m[2][3]};
    }
};
