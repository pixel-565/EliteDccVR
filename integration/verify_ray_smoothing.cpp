#include "../ray_smoothing.h"
#include <iostream>
#include <stdexcept>
struct Matrix{float m[3][4]={{1,0,0,0},{0,1,0,0},{0,0,1,0}};};
void require(bool v){if(!v)throw std::runtime_error("Ray smoothing regression");}
int main(){try{
    Matrix raw;RaySmoothing f;
    f.apply(raw,1000,100);raw.m[0][3]=1;
    require(f.apply(raw,1010,100).m[0][3]==1);
    f.reset();raw.m[0][3]=0;f.apply(raw,1000,50);raw.m[0][3]=1;
    auto step=f.apply(raw,1010,50);require(step.m[0][3]>0&&step.m[0][3]<.3f);
    require(f.apply(raw,2000,50).m[0][3]==1);
    RaySmoothing a,b;raw.m[0][3]=0;a.apply(raw,1000,40);b.apply(raw,1000,40);raw.m[0][3]=1;
    Matrix outA,outB;for(int t=1010;t<=1100;t+=10)outA=a.apply(raw,t,40);
    for(int t=1020;t<=1100;t+=20)outB=b.apply(raw,t,40);
    require(std::abs(outA.m[0][3]-outB.m[0][3])<.00001f);
    f.reset();raw={};f.apply(raw,1000,30);float maxJitter=0;
    for(int i=1;i<=100;++i){float angle=i%2?.01f:-.01f;raw.m[0][0]=raw.m[2][2]=std::cos(angle);raw.m[0][2]=std::sin(angle);raw.m[2][0]=-std::sin(angle);raw.m[0][3]=i%2?.002f:-.002f;
        auto out=f.apply(raw,1000+i*10,30);maxJitter=std::max(maxJitter,std::abs(out.m[0][2]));
        for(int c=0;c<3;++c)for(int d=0;d<3;++d){float dot=0;for(int r=0;r<3;++r)dot+=out.m[r][c]*out.m[r][d];require(std::abs(dot-(c==d?1.f:0.f))<.00001f);}
    }
    require(maxJitter<.002f);
    f.reset();require(f.apply(raw,2500,30).m[0][3]==raw.m[0][3]);
    std::cout<<"PASS original response, damping, time-step independence, orthonormal pose and reset\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
