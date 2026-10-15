#pragma once
#include <algorithm>
struct GameMouseDrag
{
    bool active = false;
    float rawU = 0, rawV = 0, u = 0, v = 0;
    void reset() { active = false; }
    void update(bool dragging, float rayU, float rayV, float baseU, float baseV,
                float gainX, float gainY, int divisor)
    {
        if (!dragging) { reset(); u = baseU; v = baseV; return; }
        if (!active) { u = baseU; v = baseV; active = true; }
        else
        {
            u = std::clamp(u + (rayU - rawU) * gainX / std::max(1, divisor), 0.f, 1.f);
            v = std::clamp(v + (rayV - rawV) * gainY / std::max(1, divisor), 0.f, 1.f);
        }
        rawU = rayU; rawV = rayV;
    }
};
