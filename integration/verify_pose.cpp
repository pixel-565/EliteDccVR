#include <openvr.h>
#include <cmath>
#include <cstdio>
#include <stdexcept>
struct V
{
    float x, y, z;
    V operator+(V b) const
    {
        return {x + b.x, y + b.y, z + b.z};
    }
    V operator-(V b) const
    {
        return {x - b.x, y - b.y, z - b.z};
    }
    V operator*(float t) const
    {
        return {x * t, y * t, z * t};
    }
};
float dot(V a, V b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
V cross(V a, V b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
V unit(V a)
{
    return a * (1 / std::sqrt(dot(a, a)));
}
vr::HmdMatrix34_t matrix(V x, V y, V z, V p)
{
    return {{{x.x, y.x, z.x, p.x}, {x.y, y.y, z.y, p.y}, {x.z, y.z, z.z, p.z}}};
}
#include "../live_pose_history.h"
#include "../live_frame_anchor.h"
void require(bool value, const char *message)
{
    if (!value)
        throw std::runtime_error(message);
}
bool close(V a, V b)
{
    return dot(a - b, a - b) < 1e-8f;
}
vr::HmdMatrix34_t pose(float yaw, V p)
{
    float c = std::cos(yaw), s = std::sin(yaw);
    return matrix({c, 0, -s}, {0, 1, 0}, {s, 0, c}, p);
}
V inverse(const vr::HmdMatrix34_t &m, V p)
{
    p = p - V{m.m[0][3], m.m[1][3], m.m[2][3]};
    return {p.x * m.m[0][0] + p.y * m.m[1][0] + p.z * m.m[2][0],
            p.x * m.m[0][1] + p.y * m.m[1][1] + p.z * m.m[2][1],
            p.x * m.m[0][2] + p.y * m.m[1][2] + p.z * m.m[2][2]};
}
int main()
{
    try
    {
        {
            using Anchor = LiveFrameAnchor<3>;
            require(Anchor::interval(0) == 10 && Anchor::interval(1008) == 1000 &&
                        Anchor::interval(313) == 315,
                    "interval clamping and 5 ms rounding");
            for (int interval : {10, 150, 1000})
            {
                Anchor timed;
                timed.setInterval(interval);
                Anchor::Points points{{{0, 0, -1}, {1, 0, -1}, {0, 1, -1}}};
                Anchor::Model model{};
                require(!timed.observe(points, {}, model, 1, 1, 100, true), "first sample cannot settle");
                for (int elapsed = 1; elapsed < interval; ++elapsed)
                    require(!timed.observe(points, {}, model, 1, elapsed + 1, 100 + elapsed, true),
                            "interval must not finish early");
                require(timed.observe(points, {}, model, 1, interval + 1, 100 + interval, true),
                        "configured interval must finish on time");
                auto revision = timed.revision;
                timed.setInterval(500);
                require(timed.valid && timed.revision == revision,
                        "changing interval must preserve fixed placement");
            }
            Anchor anchor;
            Anchor::Model key{};
            Anchor::Points original{{{-1, 0, -2}, {1, 0, -2}, {0, 1, -2}}};
            uint64_t serial = 1, time = 1000;
            auto feed = [&](const Anchor::Points &p, bool still, uint32_t pid = 42) {
                bool changed = anchor.observe(p, {0, 0, 0}, key, pid, serial++, time, still);
                time += 50;
                return changed;
            };
            for (int i = 0; i < 6; ++i)
                require(!feed(original, true), "must wait for stable acquisition");
            require(feed(original, true) && anchor.valid, "stable initial anchor");
            auto frozen = anchor.points;
            auto revision = anchor.revision;
            auto moved = original;
            for (auto &p : moved)
                p = p + V{.3f, .1f, -.2f};
            for (int i = 0; i < 40; ++i)
                require(!feed(moved, false), "head motion cannot move anchor");
            require(anchor.revision == revision && Anchor::difference(anchor.points, frozen) == 0,
                    "anchor must remain exactly fixed");
            auto jitter = original;
            for (auto &p : jitter)
                p = p + V{.003f, 0, 0};
            for (int i = 0; i < 20; ++i)
                require(!feed(jitter, true), "stationary noise cannot move anchor");
            for (int i = 0; i < 6; ++i)
                require(!feed(moved, true), "recenter needs persistent confirmation");
            require(feed(moved, true), "persistent new position commits once");
            revision = anchor.revision;
            for (int i = 0; i < 20; ++i)
                require(!feed(moved, true), "new anchor must not be recomputed");
            require(anchor.revision == revision, "no repeated placement writes");
            feed(original, true);
            anchor.pause();
            for (int i = 0; i < 6; ++i)
                require(!feed(original, true), "visibility gap restarts confirmation");
            require(feed(original, true), "reappearance confirms changed position");
            anchor.reset();
            anchor.observe(original, {}, key, 42, 900, 100, true);
            require(!anchor.observe(original, {}, key, 42, 900, 1000, true) && !anchor.valid,
                    "duplicate data cannot mature confirmation");
            require(!feed(original, true, 43) && !anchor.valid, "new process must reacquire");
            anchor.reset();
            require(!anchor.valid, "tracking-origin reset discards placement");
            puts("PASS fixed anchor, head-motion rejection, stationary noise, confirmed recenter, visibility "
                 "gaps and process reset");
        }
        LivePoseHistory history;
        auto a = pose(0, {0, 1, 0}), b = pose(.2f, {.2f, 1, 0});
        history.add(100, a);
        history.add(120, b);
        vr::HmdMatrix34_t sample{};
        require(history.at(110, sample), "interpolation unavailable");
        auto expected = pose(.1f, {.1f, 1, 0});
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 4; ++c)
                require(std::abs(sample.m[r][c] - expected.m[r][c]) < 1e-5f, "interpolation wrong");
        V world{.3f, 1.4f, -1.5f};
        auto encoded = inverse(sample, world);
        require(close(LivePoseHistory::transform(sample, encoded), world),
                "capture pose must recover world position");
        require(!close(LivePoseHistory::transform(b, encoded), world),
                "regression must expose use of latest pose with older data");
        auto left = LivePoseHistory::transform(sample, {-1, 0, -2}),
             right = LivePoseHistory::transform(sample, {1, 0, -2});
        require(std::abs(dot(right - left, right - left) - 4) < 1e-5f,
                "interpolation must preserve frame dimensions");
        require(!history.at(99, sample) && !history.at(121, sample),
                "must not extrapolate or reuse unrelated pose");
        history.add(300, b);
        require(!history.at(200, sample), "long gaps must not interpolate");
        history.clear();
        require(!history.at(110, sample), "tracking-origin reset must discard old poses");
        puts("PASS pose timestamp interpolation, world stability, dimensions, missing samples and origin "
             "reset");
        return 0;
    }
    catch (const std::exception &e)
    {
        puts(e.what());
        return 1;
    }
}
