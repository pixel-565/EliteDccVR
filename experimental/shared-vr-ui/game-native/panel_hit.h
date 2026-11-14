#pragma once
#include <unordered_map>
#include <string>
namespace NativeUI::PanelHit {
using Value=Ship::Value;
using Member=bool(__fastcall*)(void*,void*,const char*,Value*,bool);
using Release=void(__fastcall*)(void*,Value*,void*);
inline uintptr_t base{};
inline unsigned lastVisits{},lastFailure{};
inline Member memberOverride{};
inline Ship::Invoke invokeOverride{};
inline Release releaseOverride{};
inline bool allows(uintptr_t expected,uintptr_t observed,float u,float v,float lastU,float lastV,uint64_t age,int state,float radiusU=.002f,float radiusV=.002f){
 return expected&&expected==observed&&state==0&&age<100&&std::abs(u-lastU)<radiusU&&std::abs(v-lastV)<radiusV;
}
struct Owned {
 Value value{};
 Owned(){value.type=0;}
 Owned(const Owned&)=delete;Owned& operator=(const Owned&)=delete;
 ~Owned(){if((value.type&64)&&value.iface&&value.data[0])(releaseOverride?releaseOverride:reinterpret_cast<Release>(base+0x3a2460))(value.iface,&value,reinterpret_cast<void*>(value.data[0]));}
};
struct Object{void* iface{};uintptr_t data{};bool display{};};
inline Object object(const Value& value){const auto type=value.type&0x8f;return {value.iface,(type==8||type==10)?value.data[0]:0,type==10};}
inline bool get(Object owner,const char* name,Owned& result){return owner.iface&&owner.data&&(memberOverride?memberOverride:reinterpret_cast<Member>(base+0x3a2820))(owner.iface,reinterpret_cast<void*>(owner.data),name,&result.value,owner.display);}
inline bool call(Object owner,const char* method,Owned& result,const Value* args=nullptr,uint64_t count=0){return owner.iface&&owner.data&&(invokeOverride?invokeOverride:reinterpret_cast<Ship::Invoke>(base+0x3a3020))(owner.iface,reinterpret_cast<void*>(owner.data),&result.value,method,args,count,owner.display);}
inline const char* string(const Value& value){return (value.type&0x8f)==6&&value.data[0]?*reinterpret_cast<const char* const*>(value.data[0]):nullptr;}
inline bool interactiveClass(const char* name){return name&&(strstr(name,"Button")||strstr(name,"InputField")||strstr(name,"ScrollPane")||strstr(name,"ScrollBar")||strstr(name,"Slider")||strstr(name,"CheckBox")||strstr(name,"ComboBox")||strstr(name,"MenuItem"));}
inline double number(const Value& value){if(value.type==3)return static_cast<int32_t>(value.data[0]);if(value.type==4)return static_cast<uint32_t>(value.data[0]);double d=0;if(value.type==5)memcpy(&d,value.data,8);return d;}
struct Query {
 Value coordinates[3]{},stage{};float clearance=64;uintptr_t background{};unsigned visits{};bool complete=true;unsigned failure{};
 std::unordered_map<uintptr_t,bool> classes;
 void restrictArea(Object owner){
  if(!stage.iface){clearance=0;return;}
  Owned rect,x,y,width,height;
  if(!call(owner,"getBounds",rect,&stage,1)||!get(object(rect.value),"x",x)||!get(object(rect.value),"y",y)||!get(object(rect.value),"width",width)||!get(object(rect.value),"height",height)){clearance=0;return;}
  const double l=number(x.value),t=number(y.value),w=number(width.value),h=number(height.value);
  if(!std::isfinite(l)||!std::isfinite(t)||!std::isfinite(w)||!std::isfinite(h)||w<0||h<0){clearance=0;return;}
  const double px=number(coordinates[0]),py=number(coordinates[1]);
  const double dx=std::max({l-px,0.,px-(l+w)}),dy=std::max({t-py,0.,py-(t+h)});
  clearance=std::min(clearance,float(std::max(0.,std::max(dx,dy)-4.)));
 }
 bool walk(Object owner,unsigned depth){
  if(!owner.data)return false;
  if(++visits>512||depth>24){complete=false;failure=1;return true;}
  if(owner.data==background)return false;
  Owned visible;if(!get(owner,"visible",visible)||visible.value.type!=2){complete=false;failure=2;return true;}
  if(!visible.value.data[0])return false;
  // Bounds prune entire branches; shape testing is reserved for actual controls.
  Owned bounds;coordinates[2].data[0]=0;
  if(!call(owner,"hitTestPoint",bounds,coordinates,3)||bounds.value.type!=2){complete=false;failure=3;return true;}
  if(!bounds.value.data[0]){restrictArea(owner);return false;}
  bool control=false;Owned constructor;
  if(get(owner,"constructor",constructor)){
   const auto key=constructor.value.data[0];auto cached=classes.find(key);
   if(cached!=classes.end())control=cached->second;
   else {Owned label;if(call(object(constructor.value),"toString",label)){
    const auto text=string(label.value);control=interactiveClass(text);
    // Native editable text fields also have their own real interaction area.
    if(text&&strstr(text,"TextField")){Owned type;if(get(owner,"type",type)){const auto t=string(type.value);control=t&&!strcmp(t,"input");}}
    classes.emplace(key,control);
   }}
  }
  if(control){Owned shape;coordinates[2].data[0]=1;if(!call(owner,"hitTestPoint",shape,coordinates,3)||shape.value.type!=2){complete=false;return true;}if(shape.value.data[0])return true;restrictArea(owner);}
  Owned count;if(!get(owner,"numChildren",count))return false;
  unsigned children=0;
  if(count.value.type==3||count.value.type==4)children=unsigned(count.value.data[0]);
  else {complete=false;failure=5;return true;}
  if(children>512){complete=false;failure=6;return true;}
  for(unsigned n=0;n<children;++n){Value index{};index.type=3;index.data[0]=n;Owned child;
   if(!call(owner,"getChildAt",child,&index,1)){complete=false;failure=7;return true;}
   if(walk(object(child.value),depth+1))return true;
  }
  return false;
 }
};
inline int sample(uintptr_t wrapper,float* radiusU=nullptr,float* radiusV=nullptr){
 if(radiusU)*radiusU=.002f;if(radiusV)*radiusV=.002f;
 Value closure{};const auto root=Ship::eventReceiver(wrapper,"Show",&closure);
 const auto content=Ship::eventReceiver(wrapper,"SetControls");if(!root||!content||!closure.iface)return -1;
 Object rootObject{closure.iface,root,false},contentObject{closure.iface,content,false};
 Owned stage,x,y,background;
 if(!get(rootObject,"stage",stage)||!get(object(stage.value),"mouseX",x)||!get(object(stage.value),"mouseY",y))return -1;
 if((x.value.type!=3&&x.value.type!=4&&x.value.type!=5)||(y.value.type!=3&&y.value.type!=4&&y.value.type!=5))return -1;
 if(!get(contentObject,"backgroundButton",background))return -1;
 Query query;query.stage=stage.value;query.coordinates[0]=x.value;query.coordinates[1]=y.value;query.coordinates[2].type=2;query.coordinates[2].data[0]=0;query.background=background.value.data[0];
 const bool hit=query.walk(rootObject,0);lastVisits=query.visits;lastFailure=query.failure;
 if(query.complete&&!hit&&query.clearance>0){
  Owned width,height;if(get(object(stage.value),"stageWidth",width)&&get(object(stage.value),"stageHeight",height)){
   const double w=number(width.value),h=number(height.value);
   if(w>0&&h>0){if(radiusU)*radiusU=float(query.clearance/w);if(radiusV)*radiusV=float(query.clearance/h);}
  }
 }
 return query.complete?(hit?1:0):-1;
}
inline int safeSample(uintptr_t wrapper,float* radiusU=nullptr,float* radiusV=nullptr){__try{return sample(wrapper,radiusU,radiusV);}__except(EXCEPTION_EXECUTE_HANDLER){return -1;}}
}
