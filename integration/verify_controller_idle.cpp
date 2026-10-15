#include "../controller_idle.h"
#include <cassert>
#include <iostream>

int main()
{
    ControllerIdle idle;
    vr::HmdMatrix34_t pose{};
    pose.m[0][0] = pose.m[1][1] = pose.m[2][2] = 1;
    assert(idle.update(pose, true, false, 0, 1));
    pose.m[0][3] = .0005f;
    assert(idle.update(pose, true, false, 900, 1));
    assert(!idle.update(pose, true, false, 1000, 1));
    pose.m[0][3] = .003f;
    assert(idle.update(pose, true, false, 1100, 1));
    assert(!idle.update(pose, true, false, 2100, 1));
    assert(idle.update(pose, true, true, 2200, 1));
    assert(idle.update(pose, true, true, 3500, 1));
    assert(!idle.update(pose, true, false, 4500, 1));
    float angle = .02f;
    pose.m[0][0] = pose.m[2][2] = std::cos(angle);
    pose.m[0][2] = std::sin(angle);
    pose.m[2][0] = -std::sin(angle);
    assert(idle.update(pose, true, false, 4600, 1));
    assert(!idle.update(pose, false, false, 4700, 1));
    assert(idle.update(pose, true, false, 4800, 1));
    assert(idle.update(pose, true, false, 5999, 2));
    assert(!idle.update(pose, true, false, 6800, 2));
    idle.reset();
    assert(idle.update(pose, true, false, 6900, 1));
    std::cout << "PASS controller idle: jitter rejection, timeout, translation/rotation/input wake, tracking "
                 "recovery and reset\n";
}
