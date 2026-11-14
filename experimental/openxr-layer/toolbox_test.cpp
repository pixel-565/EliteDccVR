#include "diagnostic.h"
#include "panel.h"
#include "controller.h"
#include <cmath>
#include <cassert>
static XrSpace hand=reinterpret_cast<XrSpace>(1);
static XrVector3f handPosition{-.38f,0,0};
static float triggerValue=0;
static bool workspaceButton=false;
static XrResult XRAPI_CALL boolState(XrSession,const XrActionStateGetInfo*,XrActionStateBoolean* state){
 state->isActive=XR_TRUE;
 state->currentState=workspaceButton;
 return XR_SUCCESS;
}
static float stickY=0;
static XrResult XRAPI_CALL axisState(XrSession,const XrActionStateGetInfo*,XrActionStateVector2f* state){
 state->isActive=XR_TRUE;
 state->currentState.y=stickY;
 return XR_SUCCESS;
}
static XrResult XRAPI_CALL poseState(XrSession,const XrActionStateGetInfo*,XrActionStatePose* state){
 state->isActive=XR_TRUE;
 return XR_SUCCESS;
}
static XrResult XRAPI_CALL floatState(XrSession,const XrActionStateGetInfo*,XrActionStateFloat* state){
 state->isActive=XR_TRUE;
 state->currentState=triggerValue;
 return XR_SUCCESS;
}
static XrResult XRAPI_CALL locate(XrSpace space,XrSpace,XrTime,XrSpaceLocation* location){
 location->locationFlags=XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
 location->pose.orientation.w=1;
 location->pose.position=space==hand?handPosition:XrVector3f{0,.4f,.1f};
 return XR_SUCCESS;
}
static XrResult XRAPI_CALL dispatch(XrInstance,const char* name,PFN_xrVoidFunction* function){
 if(std::strcmp(name,"xrGetActionStatePose")==0)*function=reinterpret_cast<PFN_xrVoidFunction>(poseState);
 else if(std::strcmp(name,"xrGetActionStateFloat")==0)*function=reinterpret_cast<PFN_xrVoidFunction>(floatState);
 else if(std::strcmp(name,"xrGetActionStateVector2f")==0)*function=reinterpret_cast<PFN_xrVoidFunction>(axisState);
 else if(std::strcmp(name,"xrGetActionStateBoolean")==0)*function=reinterpret_cast<PFN_xrVoidFunction>(boolState);
 else if(std::strcmp(name,"xrLocateSpace")==0)*function=reinterpret_cast<PFN_xrVoidFunction>(locate);
 else return XR_ERROR_FUNCTION_UNSUPPORTED;
 return XR_SUCCESS;
}
static void checkDrag(){
 TestPanel p;
 p.get=dispatch;
 p.anchor.orientation.w=1;
 p.anchor.position.z=-1.5f;
 ControllerRay c;
 c.space=hand;
 c.grip=reinterpret_cast<XrAction>(1);
 c.depth=reinterpret_cast<XrAction>(2);
 XrTime time=0;
 XrCompositionLayerQuad panel{XR_TYPE_COMPOSITION_LAYER_QUAD}, ray{};
 panel.size={.98f,.212f};
 auto frame=[&]{panel.pose=p.anchor;assert(c.draw(p,time+=20000000,panel,ray));};
 frame();
 assert(p.hovered && p.toolbox.hover==300);
 triggerValue=.8f;
 frame();
 assert(c.dragDistance>0);
 handPosition.x=-.13f;
 frame();
 assert(std::abs(p.anchor.position.x-.25f)<.001f);
 assert(std::abs(p.anchor.orientation.y)>.01f);
 assert(!p.toolbox.state.keyboard);
 const float originalZ=p.anchor.position.z;
 stickY=.1f;
 frame();
 assert(std::abs(p.anchor.position.z-originalZ)<.001f);
 stickY=1;
 frame();
 assert(p.anchor.position.z<originalZ-.01f);
 stickY=-1;
 frame();
 assert(std::abs(p.anchor.position.z-originalZ)<.001f);
 stickY=0;
 triggerValue=0;
 frame();
 handPosition.x=.5f;
 frame();
 assert(c.dragDistance==0 && std::abs(p.anchor.position.x-.25f)<.001f);
}
int main(){
 ToolboxRaster t;
 assert(SharedToolbox::hit(105,105,false)==300);
 assert(SharedToolbox::hit(860,105,false)==304);
 assert(SharedToolbox::hit(200,105,false)==-1);
 assert(t.paint(true));
 auto before=t.pixels;
 t.select(300);
 assert(t.state.keyboard && t.paint(true) && before!=t.pixels);
 t.select(304);
 assert(t.collapsed && t.width()==210 && t.paint(true));
 assert(SharedToolbox::hit(105,105,true)==305);
 assert(SharedToolbox::hit(860,105,true)==-1);
 t.select(305);
 assert(!t.collapsed && t.paint(true));
 assert(t.pixels.at(ToolboxRaster::AtlasWidth*(ToolboxRaster::AtlasHeight-1))==0xffffffffu);
 XrPosef initial{{0,0,.5f,.8660254f},{.3f,.2f,-1.5f}};
 faceToolbox(initial,{0,0,0});
 using namespace DirectX;
 const auto q=initial.orientation;
 const auto facing=XMVector3Rotate(XMVectorSet(0,0,1,0),XMVectorSet(q.x,q.y,q.z,q.w));
 const auto expected=XMVector3Normalize(XMVectorSet(-.3f,-.2f,1.5f,0));
 assert(XMVectorGetX(XMVector3Dot(facing,expected))>.9999f);
 t.select(302);
 assert(t.settings.visible && t.hit(870,350,true)==641 && t.hit(105,105)==300 && t.height()==210);
 assert(t.paint(true));
 const auto original=t.pixels;
 t.select(641);
 assert(t.settings.amber && t.paint(true) && original!=t.pixels);
 t.select(643);
 assert(t.settings.idle==6);
 t.select(621);
 t.select(631);
 t.select(633);
 t.select(635);
 t.select(637);
 assert(t.settings.width==3.5f && t.settings.pitch==1 && t.settings.sensitivity==95 && t.settings.left);
 t.select(634);
 assert(t.settings.width==3 && t.settings.pitch==0 && t.settings.sensitivity==100);
 t.select(631);t.select(632);t.select(635);t.select(641);t.select(643);t.select(637);
 t.settings.directGame=true;t.settings.onFootMovement=true;t.settings.movementDeadZone=13;t.settings.turnDeadZone=27;t.settings.triggerInteract=true;t.settings.mapPanSpeed=7;t.settings.mapZoomSpeed=3;t.settings.mapAccelerationMs=2700;t.settings.save();
 OverlaySettings restored;
 restored.load();
 assert(restored.width==3.5f && restored.pitch==-1.f && restored.sensitivity==95 &&
        restored.amber && restored.idle==7 && restored.left && restored.directGame && restored.onFootMovement && restored.movementDeadZone==13 && restored.turnDeadZone==27 && restored.triggerInteract);
 assert(restored.mapPanSpeed==7&&restored.mapZoomSpeed==3&&restored.mapAccelerationMs==2700);
 t.select(620);
 t.select(601);
 assert(t.settings.tab==0 && t.paint(true));
 assert(t.settings.hit(500,250)==618);
 assert(t.settings.hit(500,420)==-1&&t.settings.hit(1100,510)==663&&t.settings.hit(100,760)==-1);
 OverlaySettings toggleMovement;toggleMovement.nativeControls=true;toggleMovement.tab=0;
 assert(!toggleMovement.onFootMovement&&toggleMovement.select(618)&&toggleMovement.onFootMovement);
 assert(toggleMovement.select(618)&&!toggleMovement.onFootMovement);
 assert(!toggleMovement.triggerInteract&&toggleMovement.select(619)&&toggleMovement.triggerInteract);
 assert(toggleMovement.select(619)&&!toggleMovement.triggerInteract);
 assert(toggleMovement.hit(500,350)==619);
 toggleMovement.tab=0;toggleMovement.select(660);toggleMovement.select(663);
 assert(toggleMovement.movementDeadZone==19&&toggleMovement.turnDeadZone==21);
 assert(toggleMovement.hit(950,430)==660&&toggleMovement.hit(1100,510)==663);
 toggleMovement.movementDeadZone=0;toggleMovement.turnDeadZone=80;
 toggleMovement.select(660);toggleMovement.select(663);assert(toggleMovement.movementDeadZone==0&&toggleMovement.turnDeadZone==80);
 assert(toggleMovement.hit(950,590)==664&&toggleMovement.hit(1100,590)==665&&toggleMovement.hit(950,670)==666&&toggleMovement.hit(1100,670)==667);
 toggleMovement.select(664);toggleMovement.select(667);assert(toggleMovement.mapPanSpeed==9&&toggleMovement.mapZoomSpeed==11);
 toggleMovement.mapPanSpeed=1;toggleMovement.mapZoomSpeed=100;toggleMovement.select(664);toggleMovement.select(667);assert(toggleMovement.mapPanSpeed==1&&toggleMovement.mapZoomSpeed==100);
 assert(toggleMovement.hit(950,750)==668&&toggleMovement.hit(1100,750)==669);
 toggleMovement.select(669);assert(toggleMovement.mapAccelerationMs==1600);toggleMovement.mapAccelerationMs=0;toggleMovement.select(668);assert(toggleMovement.mapAccelerationMs==0);toggleMovement.mapAccelerationMs=5000;toggleMovement.select(669);assert(toggleMovement.mapAccelerationMs==5000);
 OverlaySettings legacy;
 wchar_t settingsPath[MAX_PATH]{};assert(OverlaySettings::preferencesPath(settingsPath));
 FILE* oldFile=nullptr;assert(_wfopen_s(&oldFile,settingsPath,L"w")==0);
 fputs("5 95 3 0 0 0 1\n",oldFile);fclose(oldFile);legacy.load();assert(legacy.directGame&&!legacy.onFootMovement&&legacy.movementDeadZone==20&&legacy.turnDeadZone==20&&!legacy.triggerInteract);
 assert(legacy.mapPanSpeed==10&&legacy.mapZoomSpeed==10&&legacy.mapAccelerationMs==1500);
 assert(_wfopen_s(&oldFile,settingsPath,L"w")==0);fputs("5 95 3 0 0 0 1 1 13 27 1\n",oldFile);fclose(oldFile);
 legacy.load();assert(legacy.onFootMovement&&legacy.triggerInteract&&legacy.movementDeadZone==13&&legacy.turnDeadZone==27&&legacy.mapPanSpeed==10&&legacy.mapZoomSpeed==10&&legacy.mapAccelerationMs==1500);
 assert(_wfopen_s(&oldFile,settingsPath,L"w")==0);fputs("5 95 3 0 0 0 1 1 13 27 1 7 3\n",oldFile);fclose(oldFile);legacy.load();assert(legacy.mapPanSpeed==7&&legacy.mapZoomSpeed==3&&legacy.mapAccelerationMs==1500);
 t.select(623);
 t.select(650);
 assert(t.settings.tab==3 && t.paint(true));
 t.select(600);
 assert(!t.settings.visible && t.paint(true));
 checkDrag();
 saveToolboxPosition({-.7f,.3f,-1.1f},true);
 XrVector3f stored{};
 assert(loadToolboxPosition(stored,true) && std::abs(stored.x+.7f)<.001f);
 assert(loadToolboxPosition(stored) && std::abs(stored.x-.25f)<.001f);
 TestPanel p;
 XrCompositionLayerQuad source{XR_TYPE_COMPOSITION_LAYER_QUAD};
 source.pose.orientation.w=1;
 source.size={.98f,.212f};
 const auto placed=p.settingsQuad(source,1);
 source.pose.position.x=1;
 const auto afterToolboxMove=p.settingsQuad(source,2);
 assert(std::abs(placed.pose.position.x+.7f)<.001f);
 assert(afterToolboxMove.pose.position.x==placed.pose.position.x);
 ControllerRay toggle;
 toggle.attached=true;
 toggle.workspaceToggle=reinterpret_cast<XrAction>(3);
 p.get=dispatch;
 p.toolbox.settings.visible=true;
 workspaceButton=true;
 toggle.updateWorkspaceVisibility(p);
 assert(!p.workspaceVisible && p.toolbox.settings.visible);
 toggle.updateWorkspaceVisibility(p);
 assert(!p.workspaceVisible);
 workspaceButton=false;
 toggle.updateWorkspaceVisibility(p);
 workspaceButton=true;
 toggle.updateWorkspaceVisibility(p);
 assert(p.workspaceVisible && p.toolbox.settings.visible);
 puts("PASS Toolbox layout, rendering, button state, collapse/restore and controller drag/release");
}
