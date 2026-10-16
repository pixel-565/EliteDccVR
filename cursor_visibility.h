#pragma once
#include <cstdint>
namespace CursorVisibility
{
inline bool fresh(uint64_t now, uint64_t stamp, uint64_t limit)
{
    return stamp && now >= stamp && now - stamp <= limit;
}
inline bool active(bool enabled, bool requested, bool feedbackEnabled, uint64_t now,
                   uint64_t heartbeat, uint64_t controlStamp, uint64_t validatedStamp, uint64_t surfaceStamp)
{
    // A short lease restores native rendering if the owner pauses or stops controlling the UI.
    return enabled && requested && feedbackEnabled && fresh(now,heartbeat,1500) &&
           fresh(now,controlStamp,120) && fresh(now,validatedStamp,100) && fresh(now,surfaceStamp,100);
}
}
