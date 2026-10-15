#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <vector>
#include <fstream>
#include <iterator>
#include <iostream>
#include <stdexcept>
#include <limits>
#include "ship_signature.h"
#include "decode.h"
#include "../ship_controls.h"
static void require(bool v,const char *why){if(!v)throw std::runtime_error(why);}
int main(){try{
    for(int row=0;row<3;++row){
        float v=(189.f+row*39.5f)/294;
        require(ShipControl::row(.5f,v)==row,"Wrong target row");
        require(ShipControl::row(0,v)==-1,"Invisible side hit");
        unsigned char pixels[12]={49,21,0,255,49,21,0,255,49,21,0,255};
        require(ShipControl::selection(pixels,3)==-1,"Ambiguous selection accepted");
        pixels[row*4]=255;pixels[row*4+1]=111;require(ShipControl::selection(pixels,3)==row,"Highlight detection failed");
    }
    require(ShipControl::row(.5f,.40f)==-1 && ShipControl::row(.5f,.714f)==-1 && ShipControl::row(.5f,1)==-1,"Non-button area accepted");
    require(ShipControl::row(std::numeric_limits<float>::quiet_NaN(),.65f)==-1,"NaN hit accepted");
    for(int icon=0;icon<4;++icon)
    {
        require(ShipControl::row((34.f+67*icon)/269,150.f/294)==3+icon,"Icon hit failed");
        unsigned char pixels[28];for(int i=0;i<7;++i){pixels[i*4]=41;pixels[i*4+1]=41;pixels[i*4+2]=41;pixels[i*4+3]=255;}
        pixels[(3+icon)*4]=255;pixels[(3+icon)*4+1]=111;
        require(ShipControl::selection(pixels)==3+icon,"Icon highlight failed");
        pixels[0]=255;pixels[1]=111;require(ShipControl::selection(pixels)==-1,"Ambiguous rows/icons accepted");
    }
    require(ShipControl::row(68.f/269,150.f/294)==-1,"Icon gap accepted");
    // Exercise every source/target pair, including any remembered service column.
    for(int from=0;from<7;++from)for(int to=0;to<7;++to)for(int remembered=0;remembered<4;++remembered)
    {
        ShipControl::Navigation path;int current=from,confirmed=0;
        path.update(1000,true,to,current,1000,true,false);
        for(uint64_t now=1180;now<=2260;now+=180)
        {
            int action=path.update(now,true,to,current,now,false,false);
            if(action==4){require(current==to,"Wrong control confirmed");++confirmed;break;}
            if(action==0)current=current>=3?current:(current==0?3+remembered:current-1);
            if(action==1)current=current>=3?0:std::min(2,current+1);
            if(action==2){require(current>=3,"Left sent on large button");current=std::max(3,current-1);}
            if(action==3){require(current>=3,"Right sent on large button");current=std::min(6,current+1);}
        }
        require(confirmed==1,"Seven-control navigation did not converge");
    }
    ShipControl::Navigation nav;
    require(nav.update(1000,true,2,0,1000,false,false)==-1,"No aim debounce");
    require(nav.update(1100,true,2,0,1100,true,false)==1,"Missing down action");
    require(nav.update(1150,true,2,0,1150,false,false)==-1,"Too-fast navigation");
    require(nav.update(1260,true,2,0,1100,false,false)==-1,"Stale action feedback accepted");
    require(nav.update(1270,true,2,1,1270,false,false)==1,"Second down missing");
    require(nav.update(1430,true,2,2,1430,false,false)==4,"Confirmed target did not select");
    require(nav.update(1600,true,2,2,1600,false,false)==-1,"Repeated confirmation");
    require(nav.update(1700,true,1,2,1700,true,false)==-1,"Target change not debounced");
    require(nav.update(1800,false,1,2,1800,false,false)==-1,"Inactive input accepted");
    require(nav.update(1900,true,1,1,1900,false,false)==-1,"Fresh target not debounced");
    require(nav.update(2000,true,1,1,2000,false,false)==-1,"Confirmation survived invalid context");
    require(nav.update(2100,true,1,-1,2100,true,false)==-1,"Unknown highlight confirmed");
    require(nav.update(2200,true,1,1,2200,false,true)==-1,"Busy input confirmed");
    nav.reset();nav.update(3000,true,0,2,3000,false,false);
    require(nav.update(3100,true,0,2,3100,false,false)==0,"Up action missing");
    require(nav.update(3270,true,0,2,3270,false,false)==0,"Retry missing");
    require(nav.update(3440,true,0,2,3440,false,false)==0,"Third retry missing");
    require(nav.update(3610,true,0,2,3610,false,false)==-1,"Unbounded navigation retry");
    std::cout<<"PASS cockpit row/icon bounds, highlight detection and feedback-gated seven-control navigation/confirmation\n";
    return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
