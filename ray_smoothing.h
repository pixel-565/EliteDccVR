#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
// Filter the shared controller pose once per input update, not separate hit/visual rays.
struct RaySmoothing
{
    float values[3][4]{};
    uint64_t last=0;
    bool initialized=false;
    void reset(){initialized=false;last=0;}
    template<class Matrix> Matrix apply(const Matrix &raw,uint64_t now,int sensitivity)
    {
        sensitivity=std::clamp(sensitivity,10,100);
        if(!initialized || sensitivity==100 || now<last || now-last>250){
            for(int r=0;r<3;++r)for(int c=0;c<4;++c)values[r][c]=raw.m[r][c];
            last=now;initialized=true;return raw;
        }
        float tau=.1f*(100-sensitivity)/90.f;
        float alpha=1-std::exp(-float(now-last)/1000.f/tau);last=now;
        Matrix result=raw;
        for(int r=0;r<3;++r)for(int c=0;c<4;++c)result.m[r][c]=values[r][c]+alpha*(raw.m[r][c]-values[r][c]);
        // Rebuild an orthonormal controller basis after interpolation.
        auto normalize=[&](int col){float length=0;for(int r=0;r<3;++r)length+=result.m[r][col]*result.m[r][col];
            if(length<1e-8f)return false;length=std::sqrt(length);for(int r=0;r<3;++r)result.m[r][col]/=length;return true;};
        if(!normalize(2)){reset();return apply(raw,now,sensitivity);}
        float projection=0;for(int r=0;r<3;++r)projection+=result.m[r][0]*result.m[r][2];
        for(int r=0;r<3;++r)result.m[r][0]-=projection*result.m[r][2];
        if(!normalize(0)){reset();return apply(raw,now,sensitivity);}
        for(int r=0;r<3;++r){int a=(r+1)%3,b=(r+2)%3;result.m[r][1]=result.m[a][2]*result.m[b][0]-result.m[b][2]*result.m[a][0];}
        for(int r=0;r<3;++r)for(int c=0;c<4;++c)values[r][c]=result.m[r][c];
        return result;
    }
};
