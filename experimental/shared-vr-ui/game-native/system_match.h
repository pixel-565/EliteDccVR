#pragma once
#include "system_map.h"
#include <algorithm>
#include <cmath>
namespace NativeSystem {
inline Vec sub(Vec a,Vec b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline Vec mul(Vec a,float b){return {a.x*b,a.y*b,a.z*b};}
inline Vec add(Vec a,Vec b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
inline float dot(Vec a,Vec b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline Vec cross(Vec a,Vec b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
// Recover the map's uniform display scale and relative origin from several
// independently rendered bodies. Ambiguous layouts never produce a selection.
inline bool match(const std::vector<Body>& bodies,const std::vector<Draw>& draws,std::vector<Target>& result){
 result.clear();if(bodies.size()<3||bodies.size()>256||draws.size()<3||draws.size()>128)return false;
 size_t a=0,b=0;float longest=0;
 for(size_t i=0;i<draws.size();++i)for(size_t j=i+1;j<draws.size();++j){
  const float square=dot(sub(draws[i].map,draws[j].map),sub(draws[i].map,draws[j].map));
  if(square>longest){longest=square;a=i;b=j;}
 }
 if(longest<.0001f)return false;
 const auto observed=sub(draws[b].map,draws[a].map);
 unsigned best=0,work=0;bool ambiguous=false;std::vector<int> winners(draws.size(),-1);
 for(size_t i=0;i<bodies.size();++i){if(bodies[i].station||bodies[i].star!=draws[a].star)continue;
  for(size_t j=0;j<bodies.size();++j){if(i==j||bodies[j].station||bodies[j].star!=draws[b].star)continue;
   const auto native=sub(bodies[j].map,bodies[i].map);const float square=dot(native,native);if(square<1.e-8f)continue;
   const float scale=dot(observed,native)/square;if(!std::isfinite(scale)||scale<.001f||scale>100000)continue;
   const float tolerance=std::max(.002f,scale*.001f),limit=tolerance*tolerance;
   const auto residual=sub(observed,mul(native,scale));if(dot(residual,residual)>limit)continue;
   const auto offset=sub(draws[a].map,mul(bodies[i].map,scale));
   std::vector<int> indices(draws.size(),-1);std::vector<bool> used(bodies.size());unsigned score=0;
   for(size_t d=0;d<draws.size();++d){float closest=limit;int index=-1;
    for(size_t n=0;n<bodies.size();++n){if(++work>2000000)return false;if(used[n]||bodies[n].station||bodies[n].star!=draws[d].star)continue;
     const auto delta=sub(draws[d].map,add(mul(bodies[n].map,scale),offset));const float distance=dot(delta,delta);
     if(distance<closest){closest=distance;index=int(n);}
    }
    if(index>=0){indices[d]=index;used[index]=true;++score;}
   }
   if(score>best){best=score;winners=indices;ambiguous=false;}
   else if(score==best&&indices!=winners)ambiguous=true;
  }
 }
 if(ambiguous||best<3||best*4<draws.size()*3)return false;
 for(size_t i=0;i<draws.size();++i)if(winners[i]>=0)result.push_back({bodies[winners[i]],draws[i].world,draws[i].radius});
 return true;
}
inline bool matchViews(const std::vector<Body>& bodies,const std::vector<Draw>& draws,std::vector<Target>& result){
 result.clear();std::vector<Draw> view;std::vector<Target> matched;
 for(unsigned eye=0;eye<2;++eye){
  view.clear();for(const auto& draw:draws)if(draw.eye==eye)view.push_back(draw);
  if(!match(bodies,view,matched))continue;
  for(const auto& target:matched){
   const auto previous=std::find_if(result.begin(),result.end(),[&](const auto& entry){return entry.body.id==target.body.id&&entry.body.object==target.body.object;});
   if(previous==result.end())result.push_back(target);
   else{
    // Relative render origins differ between eyes. Their independently decoded
    // XR positions must still agree; do not merge contradictory identities.
    const auto difference=sub(previous->world,target.world);
    const float tolerance=std::max(.03f,.02f*target.radius);
    if(previous->body.object!=target.body.object||dot(difference,difference)>tolerance*tolerance){result.clear();return false;}
   }
  }
 }
 return !result.empty();
}
// The native layout includes airless bodies whose terrain mesh is not part of
// the sphere pass. Recover its full similarity transform from verified anchors,
// validate every anchor, then extend targeting to the remaining native bodies.
inline bool expandTargets(const std::vector<Body>& bodies,std::vector<Target>& anchors){
 if(anchors.size()<3||bodies.size()>256)return false;
 size_t a=0,b=0,c=0;float baseline=0;
 for(size_t i=0;i<anchors.size();++i)for(size_t j=i+1;j<anchors.size();++j){
  const auto delta=sub(anchors[j].body.map,anchors[i].body.map);const float square=dot(delta,delta);
  if(square>baseline){baseline=square;a=i;b=j;}
 }
 if(!std::isfinite(baseline)||baseline<1.e-10f)return false;
 const auto n1=mul(sub(anchors[b].body.map,anchors[a].body.map),1/std::sqrt(baseline));
 const auto worldAB=sub(anchors[b].world,anchors[a].world);const float worldLength=std::sqrt(dot(worldAB,worldAB));
 if(!std::isfinite(worldLength)||worldLength<.0001f)return false;
 const auto w1=mul(worldAB,1/worldLength);const float scale=worldLength/std::sqrt(baseline);
 if(!std::isfinite(scale)||scale<.001f||scale>100000)return false;
 float area=0;
 for(size_t i=0;i<anchors.size();++i){const auto delta=sub(anchors[i].body.map,anchors[a].body.map);
  const auto perpendicular=sub(delta,mul(n1,dot(delta,n1)));const float square=dot(perpendicular,perpendicular);
  if(square>area){area=square;c=i;}
 }
 if(area<baseline*1.e-6f)return false; // Collinear bodies cannot establish the map plane.
 const auto nativeAC=sub(anchors[c].body.map,anchors[a].body.map);
 const auto n2=mul(sub(nativeAC,mul(n1,dot(nativeAC,n1))),1/std::sqrt(area));const auto n3=cross(n1,n2);
 const auto worldAC=sub(anchors[c].world,anchors[a].world);
 const auto worldPerpendicular=sub(worldAC,mul(w1,dot(worldAC,w1)));
 const float worldHeight=std::sqrt(dot(worldPerpendicular,worldPerpendicular));if(!std::isfinite(worldHeight)||worldHeight<.0001f)return false;
 const auto w2=mul(worldPerpendicular,1/worldHeight),w3=cross(w1,w2);
 const auto transform=[&](Vec point){const auto delta=sub(point,anchors[a].body.map);
  return add(anchors[a].world,mul(add(add(mul(w1,dot(delta,n1)),mul(w2,dot(delta,n2))),mul(w3,dot(delta,n3))),scale));
 };
 const float tolerance=std::max(.01f,worldLength*.0001f);
 for(const auto& anchor:anchors){const auto error=sub(transform(anchor.body.map),anchor.world);
  if(!std::isfinite(dot(error,error))||dot(error,error)>tolerance*tolerance)return false;
 }
 std::vector<Target> expanded=anchors;
 for(const auto& body:bodies){
  if(!body.id||!body.object||!std::isfinite(body.radius)||body.radius<=0)continue;
  if(std::any_of(expanded.begin(),expanded.end(),[&](const auto& target){return target.body.id==body.id&&target.body.object==body.object;}))continue;
  const auto point=transform(body.map);const float radius=body.radius*scale;
  if(!std::isfinite(point.x)||!std::isfinite(point.y)||!std::isfinite(point.z)||!std::isfinite(radius)||radius<=0||radius>10000)continue;
  expanded.push_back({body,point,radius});
 }
 anchors.swap(expanded);return true;
}
}
