#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
struct GameMouseWheel
{
    uint64_t previous=0;
    double remainder=0;
    int direction=0;
    void reset(){previous=0;remainder=0;direction=0;}
    int update(float axis,uint64_t now)
    {
        if(!std::isfinite(axis) || std::abs(axis)<=.3f){reset();return 0;}
        int sign=axis>0?1:-1;
        if(direction!=sign){reset();direction=sign;previous=now;return sign*120;}
        double seconds=now>=previous?std::min(.05,double(now-previous)/1000.):0.;
        previous=now;
        double magnitude=std::clamp((std::abs(axis)-.3)/.7,0.,1.);
        remainder+=(1.+7.*magnitude*magnitude)*seconds;
        int ticks=static_cast<int>(remainder);
        remainder-=ticks;
        return sign*ticks*120;
    }
};
