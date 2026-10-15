#pragma once
#include <cmath>
#include <cstdint>
#include <openvr.h>

struct ControllerIdle
{
    bool initialized = false;
    uint64_t lastActivity = 0;
    vr::HmdMatrix34_t anchor{};
    void reset()
    {
        initialized = false;
    }
    bool update(const vr::HmdMatrix34_t &pose, bool valid, bool input, uint64_t now, int seconds)
    {
        if (!valid)
        {
            reset();
            return false;
        }
        bool movement = false;
        if (initialized)
        {
            float translation = 0;
            for (int row = 0; row < 3; ++row)
            {
                float delta = pose.m[row][3] - anchor.m[row][3];
                translation += delta * delta;
            }
            movement = translation >= .002f * .002f;
            for (int axis = 0; axis < 3; ++axis)
            {
                float alignment = 0;
                for (int row = 0; row < 3; ++row)
                    alignment += pose.m[row][axis] * anchor.m[row][axis];
                movement = movement || alignment < .9999619f; // Approximately half a degree.
            }
        }
        if (!initialized || movement || input)
        {
            anchor = pose;
            lastActivity = now;
            initialized = true;
        }
        return now - lastActivity < static_cast<uint64_t>(seconds) * 1000;
    }
};
