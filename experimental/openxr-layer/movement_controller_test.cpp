#include "diagnostic.h"
#include "panel.h"
#include "controller.h"
#include <cassert>
static int submitted=0,turnSubmitted=0,neutralized=0,moveZone=0,turnZone=0;
static float sentX=0,sentY=0,sentTurn=0,testTrigger=0;
static bool interactHeld=false,contextAllowed=true;static XrPath inactiveHand=0,gripHand=0;static XrSpace invalidSpace{};
static bool mapVisible=true,mapZoom=false;static float mapX=0,mapY=0;static unsigned mapCalls=0;
namespace NativeSystem {
bool initialize(uintptr_t){return true;}void enable(bool){}bool visible(){return mapVisible;}void observe(uintptr_t){}void advance(void*){}
void submit(const std::vector<Draw>&,uint64_t){}bool snapshot(std::vector<Target>&,uint64_t&){return false;}void select(const Body&){}
void counters(uint64_t (&v)[5]){for(auto& x:v)x=0;}void navigationCounters(uint64_t (&v)[5]){for(auto& x:v)x=0;}
void navigation(float x,float y,bool zoom){mapX=x;mapY=y;mapZoom=zoom;++mapCalls;}
void clearNavigation(){mapX=mapY=0;mapZoom=false;}void setFocused(bool){}
void setNavigationScale(int,int){}void setNavigationAcceleration(int){}
}
namespace OnFootMovement {
bool initialize(){return true;}bool configure(bool){return true;}
bool configureInteraction(bool){return true;}bool interactionAllowed(){return contextAllowed;}
void submitInteraction(bool down){interactHeld=down;}void clearInteraction(){interactHeld=false;}
void submit(float x,float y,bool allowed){++submitted;sentX=allowed?x:0;sentY=allowed?y:0;}
void submitTurn(float x,bool allowed){++turnSubmitted;sentTurn=allowed?x:0;}
void setDeadZones(int movement,int turn){moveZone=movement;turnZone=turn;}
void neutral(){++neutralized;}void setFocused(bool){}bool ready(){return true;}unsigned slot(){return 0;}void counters(uint64_t (&v)[5]){for(auto& n:v)n=0;}
}
static XrResult XRAPI_CALL axis(XrSession,const XrActionStateGetInfo* info,XrActionStateVector2f* value){value->isActive=info->subactionPath!=inactiveHand;value->currentState=info->subactionPath==10?XrVector2f{.4f,.8f}:XrVector2f{-.6f,.1f};return XR_SUCCESS;}
static XrResult XRAPI_CALL grip(XrSession,const XrActionStateGetInfo* info,XrActionStateFloat* value){value->isActive=info->subactionPath!=inactiveHand;value->currentState=info->action==reinterpret_cast<XrAction>(6)?testTrigger:(info->subactionPath==gripHand?1.f:0.f);return XR_SUCCESS;}
static XrResult XRAPI_CALL locate(XrSpace space,XrSpace,XrTime,XrSpaceLocation* value){value->locationFlags=space==invalidSpace?0:XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;return XR_SUCCESS;}
static XrResult XRAPI_CALL get(XrInstance,const char* name,PFN_xrVoidFunction* fn){
 if(!strcmp(name,"xrGetActionStateVector2f"))*fn=reinterpret_cast<PFN_xrVoidFunction>(&axis);
 else if(!strcmp(name,"xrGetActionStateFloat"))*fn=reinterpret_cast<PFN_xrVoidFunction>(&grip);
 else if(!strcmp(name,"xrLocateSpace"))*fn=reinterpret_cast<PFN_xrVoidFunction>(&locate);
 else {*fn=nullptr;return XR_ERROR_FUNCTION_UNSUPPORTED;}return XR_SUCCESS;
}
int main(){TestPanel panel;panel.get=get;panel.world=reinterpret_cast<XrSpace>(1);panel.toolbox.settings.onFootMovement=true;panel.toolbox.settings.movementDeadZone=13;panel.toolbox.settings.turnDeadZone=27;
 ControllerRay ray;ray.attached=true;ray.depth=reinterpret_cast<XrAction>(2);ray.grip=reinterpret_cast<XrAction>(3);ray.space=reinterpret_cast<XrSpace>(4);ray.leftSpace=reinterpret_cast<XrSpace>(5);ray.hands[0]=10;ray.hands[1]=20;
 ray.updateMovement(panel,1,false);assert(submitted==1&&turnSubmitted==1&&sentX==-.6f&&sentY==.8f&&sentTurn==.4f&&moveZone==13&&turnZone==27);
 panel.toolbox.settings.left=true;ray.updateMovement(panel,1,false);assert(submitted==2&&sentX==.4f&&sentY==.1f&&sentTurn==-.6f);
 ray.updateMovement(panel,1,true);assert(submitted==2&&neutralized==1);
 gripHand=20;ray.updateMovement(panel,1,false);assert(sentX==.4f&&sentY==0&&sentTurn==0);gripHand=0;
 inactiveHand=10;ray.updateMovement(panel,1,false);assert(sentX==0&&sentY==.1f&&sentTurn==-.6f);inactiveHand=0;
 invalidSpace=ray.leftSpace;ray.updateMovement(panel,1,false);assert(sentX==.4f&&sentY==0&&sentTurn==0);
 panel.toolbox.settings.onFootMovement=false;ray.updateMovement(panel,1,false);assert(neutralized==2);
 // Context interaction is independent of the movement enable switch.
 panel.toolbox.settings.triggerInteract=true;panel.toolbox.settings.left=false;invalidSpace=nullptr;inactiveHand=0;
 ray.trigger=reinterpret_cast<XrAction>(6);
 testTrigger=1;ray.updateInteraction(panel,10,false);assert(!interactHeld);
 testTrigger=0;ray.updateInteraction(panel,20,false);
 testTrigger=1;ray.updateInteraction(panel,30,false);assert(interactHeld);
 ray.updateInteraction(panel,40,true);assert(!interactHeld);
 ray.updateInteraction(panel,50,false);assert(!interactHeld);
 testTrigger=0;ray.updateInteraction(panel,60,false);testTrigger=1;ray.updateInteraction(panel,70,false);assert(interactHeld);
 contextAllowed=false;ray.updateInteraction(panel,80,false);assert(!interactHeld);contextAllowed=true;
 ray.updateInteraction(panel,90,false);assert(!interactHeld);
 testTrigger=0;ray.updateInteraction(panel,100,false);testTrigger=1;ray.updateInteraction(panel,110,false);assert(interactHeld);
 invalidSpace=ray.space;ray.updateInteraction(panel,120,false);assert(!interactHeld);invalidSpace=nullptr;
 ray.updateInteraction(panel,130,false);assert(!interactHeld);
 testTrigger=0;ray.updateInteraction(panel,140,false);testTrigger=1;ray.updateInteraction(panel,150,false);assert(interactHeld);
 ray.updateInteraction(panel,400000000,false);assert(!interactHeld);
 puts("PASS trigger interaction: fresh press, independent movement switch, panel/context priority, lost tracking and frame-gap cancellation");
 puts("PASS dual-hand routing, role swapping, independent dead zones and per-hand grip/action/tracking cancellation");
 panel.toolbox.settings.directGame=true;panel.toolbox.settings.left=false;ray.grip=reinterpret_cast<XrAction>(5);gripHand=0;
 ray.updateMapNavigation(panel,1,false);assert(mapCalls&&mapX==.4f&&mapY==.8f&&!mapZoom);
 gripHand=10;ray.updateMapNavigation(panel,2,false);assert(mapZoom);
 ray.updateMapNavigation(panel,3,true);assert(mapX==0&&mapY==0&&!mapZoom);
 gripHand=0;panel.toolbox.settings.left=true;ray.updateMapNavigation(panel,4,false);assert(mapX==-.6f&&mapY==.1f&&!mapZoom);
 invalidSpace=ray.leftSpace;ray.updateMapNavigation(panel,5,false);assert(mapX==0&&mapY==0);invalidSpace=nullptr;
 mapVisible=false;ray.updateMapNavigation(panel,6,false);assert(mapX==0&&mapY==0);
 puts("PASS System Map selected-hand routing, grip zoom, overlay priority, tracking loss and map-context cancellation");
}
