#pragma once
#include <algorithm>
#include <cmath>

namespace BrowserPreferences {
inline float opacity(float fraction){return std::round((.3f+std::clamp(fraction,0.f,1.f)*.7f)*100.f)/100.f;}
inline float curve(float degrees,int direction){return direction==0?0.f:std::clamp(degrees+direction*5.f,0.f,120.f);}
inline float zoom(float factor,int direction){return std::clamp(std::round(factor*10.f+direction)/10.f,.5f,2.f);}
}
