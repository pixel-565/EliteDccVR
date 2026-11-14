#pragma once
#include <cstring>
#include <algorithm>
#include <DirectXMath.h>
#include "../shared-vr-ui/browser/resize.h"
#include "ship_surface.h"
#include "../shared-vr-ui/game-native/context_input.h"

struct BrowserResizePoint {
 float x{},y{},z{};
 BrowserResizePoint operator+(BrowserResizePoint b)const{return {x+b.x,y+b.y,z+b.z};}
 BrowserResizePoint operator-(BrowserResizePoint b)const{return {x-b.x,y-b.y,z-b.z};}
 BrowserResizePoint operator*(float f)const{return {x*f,y*f,z*f};}
};
inline float dot(BrowserResizePoint a,BrowserResizePoint b){return a.x*b.x+a.y*b.y+a.z*b.z;}

struct ControllerRay {
 SharedBrowser::Resize<BrowserResizePoint> browserResize;
 XrActionSet set{};
 XrAction aim{}, trigger{}, grip{}, depth{}, workspaceToggle{};
 XrPath triggerBinding{}, gripBinding{}, depthBinding{}, toggleBinding{};
 bool triggerDown=false, gripDown=false, opacityDrag=false, toggleDown=false;
 bool mapNavigationBlocked=false,mapZoomGrip=false;
 int mapNavigationHand=-1;
 float dragDistance=0;
 int dragSurface=0;
 XrTime previousDragTime=0, lastScroll=0;
 DirectX::XMFLOAT3 dragOffset{};
 XrPath profile{}, binding{};
 XrSpace space{}, leftSpace{};
 XrPath hands[2]{};
 XrActionSuggestedBinding leftBindings[5]{};
 uint32_t leftCount=0;
 bool usingLeft=false;
 OnFootMovement::InteractionLatch interactionLatch;
 bool interactionDown=false;
 int interactionHand=-1;
 XrTime lastInteractionSample=0;
 XrTime lastMotion=0, lastSample=0;
 DirectX::XMFLOAT3 motionOrigin{}, smoothedOrigin{};
 DirectX::XMFLOAT4 motionRotation{}, smoothedRotation{};
 bool attached=false;

 void initialize(TestPanel& panel) {
  auto path=panel.proc<PFN_xrStringToPath>("xrStringToPath");
  if(XR_FAILED(path(panel.instance,"/interaction_profiles/oculus/touch_controller",&profile)) ||
     XR_FAILED(path(panel.instance,"/user/hand/right/input/aim/pose",&binding))) return;
  XrActionSetCreateInfo si{XR_TYPE_ACTION_SET_CREATE_INFO};
  strcpy_s(si.actionSetName,"elitedccvr_ray");
  strcpy_s(si.localizedActionSetName,"EliteDccVR ray");
  if(XR_FAILED(panel.proc<PFN_xrCreateActionSet>("xrCreateActionSet")(panel.instance,&si,&set))) return;
  if(XR_FAILED(path(panel.instance,"/user/hand/right",&hands[0])) ||
     XR_FAILED(path(panel.instance,"/user/hand/left",&hands[1])))return;
  XrActionCreateInfo ai{XR_TYPE_ACTION_CREATE_INFO};
  ai.countSubactionPaths=2;
  ai.subactionPaths=hands;
  ai.actionType=XR_ACTION_TYPE_POSE_INPUT;
  strcpy_s(ai.actionName,"right_aim");
  strcpy_s(ai.localizedActionName,"Right aim");
  if(XR_FAILED(panel.proc<PFN_xrCreateAction>("xrCreateAction")(set,&ai,&aim))) return;
  if(XR_FAILED(path(panel.instance,"/user/hand/right/input/trigger/value",&triggerBinding))) return;
  ai.actionType=XR_ACTION_TYPE_FLOAT_INPUT;
  strcpy_s(ai.actionName,"right_trigger");
  strcpy_s(ai.localizedActionName,"Right trigger");
  auto triggerResult=panel.proc<PFN_xrCreateAction>("xrCreateAction")(set,&ai,&trigger);
  panelLog("create right trigger",triggerResult);
  if(XR_SUCCEEDED(path(panel.instance,"/user/hand/right/input/squeeze/value",&gripBinding))){
   strcpy_s(ai.actionName,"right_grip");
   strcpy_s(ai.localizedActionName,"Right grip");
   panelLog("create right grip",panel.proc<PFN_xrCreateAction>("xrCreateAction")(set,&ai,&grip));
  }
  if(XR_SUCCEEDED(path(panel.instance,"/user/hand/right/input/thumbstick",&depthBinding))){
   ai.actionType=XR_ACTION_TYPE_VECTOR2F_INPUT;
   strcpy_s(ai.actionName,"right_depth");
   strcpy_s(ai.localizedActionName,"Toolbox distance");
   panelLog("create toolbox depth",panel.proc<PFN_xrCreateAction>("xrCreateAction")(set,&ai,&depth));
  }
  if(XR_SUCCEEDED(path(panel.instance,"/user/hand/right/input/b/click",&toggleBinding))){
   ai.actionType=XR_ACTION_TYPE_BOOLEAN_INPUT;
   strcpy_s(ai.actionName,"toggle_workspace");
   strcpy_s(ai.localizedActionName,"Show or hide panels");
   panelLog("create workspace toggle",panel.proc<PFN_xrCreateAction>("xrCreateAction")(set,&ai,&workspaceToggle));
  }
  XrActionSuggestedBinding entries[10]={{aim,binding}};
  uint32_t count=1;
  if(trigger)entries[count++]={trigger,triggerBinding};
  if(grip)entries[count++]={grip,gripBinding};
  if(depth)entries[count++]={depth,depthBinding};
  if(workspaceToggle)entries[count++]={workspaceToggle,toggleBinding};
  const XrAction actions[]={aim,trigger,grip,depth,workspaceToggle};
  const char* paths[]={"/user/hand/left/input/aim/pose","/user/hand/left/input/trigger/value",
    "/user/hand/left/input/squeeze/value","/user/hand/left/input/thumbstick","/user/hand/left/input/y/click"};
  for(int i=0;i<5;++i){
   XrPath leftPath{};
   if(actions[i]&&XR_SUCCEEDED(path(panel.instance,paths[i],&leftPath))){
    leftBindings[leftCount++]={actions[i],leftPath};
    entries[count++]={actions[i],leftPath};
   }
  }
  XrInteractionProfileSuggestedBinding bindings{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
  bindings.interactionProfile=profile;
  bindings.countSuggestedBindings=count;
  bindings.suggestedBindings=entries;
  panelLog("suggest right aim",panel.proc<PFN_xrSuggestInteractionProfileBindings>("xrSuggestInteractionProfileBindings")(panel.instance,&bindings));
 }
 void connect(TestPanel& panel) {
  attached=true;
  XrActionSpaceCreateInfo si{XR_TYPE_ACTION_SPACE_CREATE_INFO};
  si.action=aim;
  si.subactionPath=hands[0];
  si.poseInActionSpace.orientation.w=1;
  auto create=panel.proc<PFN_xrCreateActionSpace>("xrCreateActionSpace");
  panelLog("create right aim space",create(panel.session,&si,&space));
  si.subactionPath=hands[1];
  panelLog("create left aim space",create(panel.session,&si,&leftSpace));
 }
 void updateMapNavigation(TestPanel& panel,XrTime time,bool blocked){
  NativeSystem::setNavigationScale(panel.toolbox.settings.mapPanSpeed,panel.toolbox.settings.mapZoomSpeed);
  NativeSystem::setNavigationAcceleration(panel.toolbox.settings.mapAccelerationMs);
  const auto cancel=[&]{mapZoomGrip=false;NativeSystem::clearNavigation();};
  if(!panel.toolbox.settings.directGame||!attached||!depth||!grip||!panel.world||blocked||!NativeSystem::visible()){cancel();return;}
  const int hand=panel.toolbox.settings.left?1:0;if(mapNavigationHand!=hand){mapNavigationHand=hand;mapZoomGrip=false;NativeSystem::clearNavigation();}
  auto readAxis=panel.proc<PFN_xrGetActionStateVector2f>("xrGetActionStateVector2f");
  auto readGrip=panel.proc<PFN_xrGetActionStateFloat>("xrGetActionStateFloat");
  auto locate=panel.proc<PFN_xrLocateSpace>("xrLocateSpace");
  if(!readAxis||!readGrip||!locate){cancel();return;}
  XrActionStateGetInfo info{XR_TYPE_ACTION_STATE_GET_INFO};info.action=depth;info.subactionPath=hands[hand];
  XrActionStateVector2f axis{XR_TYPE_ACTION_STATE_VECTOR2F};
  XrSpaceLocation pose{XR_TYPE_SPACE_LOCATION};const auto aimSpace=hand?leftSpace:space;
  constexpr auto valid=XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
  if(XR_FAILED(readAxis(panel.session,&info,&axis))||!axis.isActive||!aimSpace||XR_FAILED(locate(aimSpace,panel.world,time,&pose))||(pose.locationFlags&valid)!=valid){cancel();return;}
  info.action=grip;XrActionStateFloat held{XR_TYPE_ACTION_STATE_FLOAT};
  if(XR_FAILED(readGrip(panel.session,&info,&held))||!held.isActive){cancel();return;}
  if(held.currentState>=.65f)mapZoomGrip=true;else if(held.currentState<=.35f)mapZoomGrip=false;
  NativeSystem::navigation(axis.currentState.x,axis.currentState.y,mapZoomGrip);
  if(std::abs(axis.currentState.x)>.2f||std::abs(axis.currentState.y)>.2f)lastMotion=time;
 }
 void updateMovement(TestPanel& panel,XrTime time,bool interactionBlocked){
  OnFootMovement::setDeadZones(panel.toolbox.settings.movementDeadZone,panel.toolbox.settings.turnDeadZone);
  if(!panel.toolbox.settings.onFootMovement||!attached||!depth||!panel.world||interactionBlocked){OnFootMovement::neutral();return;}
  const auto readAxis=panel.proc<PFN_xrGetActionStateVector2f>("xrGetActionStateVector2f");
  const auto locate=panel.proc<PFN_xrLocateSpace>("xrLocateSpace");
  const auto readGrip=panel.proc<PFN_xrGetActionStateFloat>("xrGetActionStateFloat");
  const auto stick=[&](int hand,XrVector2f& output){
   if(!readAxis||!locate)return false;
   XrActionStateGetInfo info{XR_TYPE_ACTION_STATE_GET_INFO};info.action=depth;info.subactionPath=hands[hand];
   XrActionStateVector2f axis{XR_TYPE_ACTION_STATE_VECTOR2F};
   if(XR_FAILED(readAxis(panel.session,&info,&axis))||!axis.isActive)return false;
   XrSpaceLocation pose{XR_TYPE_SPACE_LOCATION};const auto aimSpace=hand?leftSpace:space;
   constexpr XrSpaceLocationFlags valid=XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
   if(!aimSpace||XR_FAILED(locate(aimSpace,panel.world,time,&pose))||(pose.locationFlags&valid)!=valid)return false;
   if(grip){
    info.action=grip;XrActionStateFloat held{XR_TYPE_ACTION_STATE_FLOAT};
    if(!readGrip||XR_FAILED(readGrip(panel.session,&info,&held))||!held.isActive||held.currentState>.35f)return false;
   }
   output=axis.currentState;return true;
  };
  const int primaryHand=panel.toolbox.settings.left?1:0;
  XrVector2f primary{},strafe{};const bool primaryActive=stick(primaryHand,primary),strafeActive=stick(1-primaryHand,strafe);
  OnFootMovement::submit(strafeActive?strafe.x:0.f,primaryActive?primary.y:0.f,primaryActive||strafeActive);
  OnFootMovement::submitTurn(primary.x,primaryActive);
 }
 void updateInteraction(TestPanel& panel,XrTime time,bool interactionBlocked){
  const int selected=panel.toolbox.settings.left?1:0;
  if(interactionHand!=selected||(lastInteractionSample&&time-lastInteractionSample>200000000))interactionLatch.cancel();
  interactionHand=selected;lastInteractionSample=time;
  const auto cancel=[&]{interactionLatch.cancel();OnFootMovement::clearInteraction();};
  if(!panel.toolbox.settings.triggerInteract||!attached||!trigger||!panel.world){cancel();return;}
  const auto read=panel.proc<PFN_xrGetActionStateFloat>("xrGetActionStateFloat");
  XrActionStateGetInfo info{XR_TYPE_ACTION_STATE_GET_INFO};info.action=trigger;info.subactionPath=hands[selected];
  XrActionStateFloat value{XR_TYPE_ACTION_STATE_FLOAT};
  if(!read||XR_FAILED(read(panel.session,&info,&value))||!value.isActive){cancel();return;}
  if(value.currentState>=.65f)interactionDown=true;else if(value.currentState<=.35f)interactionDown=false;
  XrSpaceLocation pose{XR_TYPE_SPACE_LOCATION};const auto aimSpace=selected?leftSpace:space;
  const auto locate=panel.proc<PFN_xrLocateSpace>("xrLocateSpace");
  constexpr auto valid=XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
  if(!aimSpace||!locate||XR_FAILED(locate(aimSpace,panel.world,time,&pose))||(pose.locationFlags&valid)!=valid){cancel();return;}
  bool allowed=!interactionBlocked&&OnFootMovement::interactionAllowed();
  if(grip){info.action=grip;XrActionStateFloat held{XR_TYPE_ACTION_STATE_FLOAT};if(XR_FAILED(read(panel.session,&info,&held))||!held.isActive||held.currentState>.35f)allowed=false;}
  OnFootMovement::submitInteraction(interactionLatch.update(interactionDown,allowed));
 }
 void disconnect(TestPanel& panel) {
  interactionLatch.cancel();OnFootMovement::clearInteraction();
  OnFootMovement::neutral();
  if(space) panel.proc<PFN_xrDestroySpace>("xrDestroySpace")(space);
  if(leftSpace)panel.proc<PFN_xrDestroySpace>("xrDestroySpace")(leftSpace);
  space=leftSpace=XR_NULL_HANDLE;
  lastMotion=lastSample=0;
  attached=false;
  NativeSystem::clearNavigation();mapZoomGrip=false;
  toggleDown=false;
  triggerDown=false;
  opacityDrag=false;
  gripDown=false;
  dragDistance=0;
 }
 void updateWorkspaceVisibility(TestPanel& panel) {
  if(!workspaceToggle||!attached)return;
  XrActionStateGetInfo info{XR_TYPE_ACTION_STATE_GET_INFO};
  info.action=workspaceToggle;
  auto read=panel.proc<PFN_xrGetActionStateBoolean>("xrGetActionStateBoolean");
  bool pressed=false,active=false;
  for(auto hand:hands){
   info.subactionPath=hand;
   XrActionStateBoolean value{XR_TYPE_ACTION_STATE_BOOLEAN};
   if(XR_SUCCEEDED(read(panel.session,&info,&value))&&value.isActive){
    active=true;
    pressed|=value.currentState!=XR_FALSE;
   }
  }
  if(!active)return;
  if(pressed&&!toggleDown){
   releaseBrowser(panel);NativeUI::clearPointer();
   panel.workspaceVisible=!panel.workspaceVisible;
   panel.toolbox.browser.setVisible(panel.workspaceVisible&&panel.toolbox.state.browser&&!panel.toolbox.browserSettings);
   triggerDown=gripDown=true;
   opacityDrag=false;
   if(dragDistance>0){
    const auto& moved=dragSurface==3?panel.keyboardAnchor:dragSurface==2?panel.browserAnchor:dragSurface==1?panel.settingsAnchor:panel.anchor;
    saveToolboxPosition(moved.position,dragSurface);
   }
   dragDistance=0;
   if(browserResize.corner){
    browserResize.corner=0;
    panel.toolbox.resizeCorner=0;
    panel.toolbox.saveBrowserPreferences();
    saveToolboxPosition(panel.browserAnchor.position,2);
   }
   panel.toolbox.hover=-1;
   panel.toolbox.dirty=true;
   panelLog(panel.workspaceVisible?"workspace shown":"workspace hidden");
  }
  toggleDown=pressed;
 }
 bool browserPressed=false;
 POINT browserDragPoint{};
 void releaseBrowser(TestPanel& panel){
  if(browserPressed)panel.toolbox.browser.post({OpenXRBrowser::Up,browserDragPoint});
  browserPressed=false;
 }
 bool draw(TestPanel& panel,XrTime time,const XrCompositionLayerQuad& source,XrCompositionLayerQuad& ray,const XrCompositionLayerQuad* settingsQuad=nullptr,const XrCompositionLayerQuad* browserQuads=nullptr,int browserCount=0,const XrCompositionLayerQuad* keyboardQuads=nullptr,int keyboardCount=0,XrCompositionLayerQuad* dot=nullptr) {
  const auto finishResize=[&]{
   if(!browserResize.corner)return;
   browserResize.corner=0;
   panel.toolbox.resizeCorner=0;
   panel.toolbox.dirty=true;
   panel.toolbox.saveBrowserPreferences();
   saveToolboxPosition(panel.browserAnchor.position,2);
  };
  if(!browserCount||panel.toolbox.browserSettings){finishResize();releaseBrowser(panel);}
  const int oldHover=panel.toolbox.hover;
  panel.toolbox.hover=-1;
  const auto trackingLost=[&]{
   NativeUI::clearPointer();
   releaseBrowser(panel);
   panel.toolbox.dirty|=oldHover!=-1;
   dragDistance=0;
   opacityDrag=false;
   finishResize();
   return false;
  };
  panel.hovered=false;
  mapNavigationBlocked=false;
  if(usingLeft!=panel.toolbox.settings.left){
   releaseBrowser(panel);
   usingLeft=panel.toolbox.settings.left;
   triggerDown=gripDown=true;
   dragDistance=0;
   lastMotion=lastSample=0;
  }
  const auto aimSpace=usingLeft?leftSpace:space;
  if(!aimSpace)return trackingLost();
  XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
  gi.action=aim;
  gi.subactionPath=hands[usingLeft?1:0];
  XrActionStatePose state{XR_TYPE_ACTION_STATE_POSE};
  if(XR_FAILED(panel.proc<PFN_xrGetActionStatePose>("xrGetActionStatePose")(panel.session,&gi,&state))||!state.isActive)return trackingLost();
  XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
  auto locate=panel.proc<PFN_xrLocateSpace>("xrLocateSpace");
  constexpr auto valid=XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
  if(XR_FAILED(locate(aimSpace,panel.world,time,&location))||(location.locationFlags&valid)!=valid)return trackingLost();
  using namespace DirectX;
  const auto q=location.pose.orientation;
  auto rotation=XMVectorSet(q.x,q.y,q.z,q.w);
  const auto origin=location.pose.position;
  auto start=XMVectorSet(origin.x,origin.y,origin.z,0);
  if(!lastMotion || time<lastMotion ||
     XMVectorGetX(XMVector3LengthSq(XMVectorSubtract(start,XMLoadFloat3(&motionOrigin))))>.000025f ||
     std::abs(XMVectorGetX(XMVector4Dot(rotation,XMLoadFloat4(&motionRotation))))<.99998f){
   lastMotion=time;
   XMStoreFloat3(&motionOrigin,start);
   XMStoreFloat4(&motionRotation,rotation);
  }
  const auto elapsed=time-lastSample;
  const int sensitivity=panel.toolbox.settings.sensitivity;
  if(lastSample && elapsed>0 && elapsed<250000000 && sensitivity<100){
   const float tau=.1f*(100-sensitivity)/90.f;
   const float alpha=1-std::exp(-static_cast<float>(elapsed)*1.e-9f/tau);
   start=XMVectorLerp(XMLoadFloat3(&smoothedOrigin),start,alpha);
   rotation=XMQuaternionSlerp(XMLoadFloat4(&smoothedRotation),rotation,alpha);
  }
  lastSample=time;
  XMStoreFloat3(&smoothedOrigin,start);
  XMStoreFloat4(&smoothedRotation,rotation);
  const float pitch=XMConvertToRadians(panel.toolbox.settings.pitch);
  auto forward=XMVector3Rotate(XMVectorSet(0,std::sin(pitch),-std::cos(pitch),0),rotation);
  auto panelPosition=source.pose.position;
  bool hitSettings=false,hitBrowser=false,hitKeyboard=false;
  POINT keyboardPoint{};
  POINT browserPoint{};
  float length=1.5f;
  int button=-1;
  auto intersect=[&](const XrCompositionLayerQuad& quad,bool settingsSurface,bool browserSurface,bool keyboardSurface=false){
   const auto q=quad.pose.orientation;
   const auto p=quad.pose.position;
   auto inverse=XMQuaternionConjugate(XMVectorSet(q.x,q.y,q.z,q.w));
   auto localOrigin=XMVector3Rotate(XMVectorSubtract(start,XMVectorSet(p.x,p.y,p.z,0)),inverse);
   auto localDirection=XMVector3Rotate(forward,inverse);
   const float dz=XMVectorGetZ(localDirection);
   if(dz>=-.0001f)return;
   const float distance=-XMVectorGetZ(localOrigin)/dz;
   if(distance<=.01f||distance>5.f||(panel.hovered&&distance>=length))return;
   auto point=XMVectorMultiplyAdd(localDirection,XMVectorReplicate(distance),localOrigin);
   const float u=XMVectorGetX(point)/quad.size.width+.5f;
   const float v=.5f-XMVectorGetY(point)/quad.size.height;
   if(u<0||u>=1||v<0||v>=1)return;
   panel.hovered=true;
   hitSettings=settingsSurface;
   hitBrowser=browserSurface;
   hitKeyboard=keyboardSurface;
   panelPosition=keyboardSurface?panel.keyboardAnchor.position:browserSurface?panel.browserAnchor.position:p;
   length=distance;
   const auto extent=quad.subImage.imageRect.extent;
   if(keyboardSurface){
    keyboardPoint={quad.subImage.imageRect.offset.x+static_cast<LONG>(u*extent.width),static_cast<LONG>(v*extent.height)};
    button=SharedKeyboard::hit(float(keyboardPoint.x),float(keyboardPoint.y),panel.toolbox.keyboardSettings);
   }else if(browserSurface){
    browserPoint={quad.subImage.imageRect.offset.x+static_cast<LONG>(u*extent.width),
       static_cast<LONG>(v*extent.height)-OpenXRBrowser::Toolbar};
    button=panel.toolbox.browserHit(browserPoint);
   }else{
    button=panel.toolbox.hit(u*(extent.width?extent.width:panel.toolbox.width()),
                            v*(extent.height?extent.height:panel.toolbox.height()),settingsSurface);
   }
  };
  intersect(source,false,false);
  if(settingsQuad)intersect(*settingsQuad,true,false);
  for(int i=0;i<browserCount;++i)intersect(browserQuads[i],false,true);
  for(int i=0;i<keyboardCount;++i)intersect(keyboardQuads[i],false,false,true);
  if(hitKeyboard&&panel.toolbox.keySnap&&!panel.toolbox.keyboardSettings&&!gripDown&&
     oldHover>=SharedKeyboard::First&&oldHover<SharedKeyboard::Keys){
   auto box=SharedKeyboard::keyRects[oldHover-SharedKeyboard::First];
   box.left-=3;box.right+=3;box.top-=3;box.bottom+=3;
   if(BrowserChrome::inside(box,float(keyboardPoint.x),float(keyboardPoint.y)))button=oldHover;
  }
  if(hitBrowser&&!panel.toolbox.browserSettings){
   if(browserPressed){
    browserDragPoint={browserPoint.x,std::clamp(browserPoint.y,0L,static_cast<LONG>(panel.toolbox.pageHeight()-1))};
    panel.toolbox.browser.post({OpenXRBrowser::Move,browserDragPoint});
   }else if(browserPoint.y>=0&&browserPoint.y<panel.toolbox.pageHeight())
    panel.toolbox.browser.post({OpenXRBrowser::Move,browserPoint});
  }
  panel.toolbox.hover=button;
  panel.toolbox.dirty|=oldHover!=button;
  XrSpaceLocation head{XR_TYPE_SPACE_LOCATION};
  if(XR_FAILED(locate(panel.space,panel.world,time,&head))||!(head.locationFlags&XR_SPACE_LOCATION_POSITION_VALID_BIT))return trackingLost();
  auto eye=XMVectorSet(head.pose.position.x,head.pose.position.y,head.pose.position.z,0);
  bool hitGame=false,gameClick=false,hitStar=false,hitBody=false,transparentPanel=false;
  const bool overlayHit=panel.hovered;
  mapNavigationBlocked=overlayHit||dragDistance>0||browserResize.corner!=0||triggerDown;
  NativeUI::Hit panelProbe{};
  NativeUI::Hit gameHit{};
  if(panel.toolbox.settings.directGame&&!gripDown&&!browserResize.corner){
   const auto hq=head.pose.orientation;
   const auto inverseHead=XMQuaternionConjugate(XMVectorSet(hq.x,hq.y,hq.z,hq.w));
   const auto viewOrigin=XMVector3Rotate(XMVectorSubtract(start,eye),inverseHead);
   const auto viewDirection=XMVector3Rotate(forward,inverseHead);
   gameHit.distance=panel.hovered?length:5.f;
   if(NativeUI::hit({XMVectorGetX(viewOrigin),XMVectorGetY(viewOrigin),-XMVectorGetZ(viewOrigin)},
                    {XMVectorGetX(viewDirection),XMVectorGetY(viewDirection),-XMVectorGetZ(viewDirection)},gameHit)||
      ShipSurface::hit({XMVectorGetX(start),XMVectorGetY(start),XMVectorGetZ(start)},
                       {XMVectorGetX(forward),XMVectorGetY(forward),XMVectorGetZ(forward)},gameHit)){
    hitGame=true;hitBrowser=hitKeyboard=hitSettings=false;
    panel.hovered=true;length=gameHit.distance;button=-1;
    panel.toolbox.hover=-1;
   }
  }
  if(hitGame&&!overlayHit&&!NativeUI::pointerHeld()&&NativeUI::mapPointEmpty(gameHit)){
   // Retain a nonvisual hover probe, but remove empty UI from visible collision.
   panelProbe=gameHit;transparentPanel=true;hitGame=false;panel.hovered=false;length=1.5f;
  }
  if(!panel.hovered&&panel.toolbox.settings.directGame&&!gripDown&&!browserResize.corner&&
     ShipSurface::systemHit({XMVectorGetX(start),XMVectorGetY(start),XMVectorGetZ(start)},
                           {XMVectorGetX(forward),XMVectorGetY(forward),XMVectorGetZ(forward)},gameHit)){
   hitBody=hitStar=hitGame=true;panel.hovered=true;length=gameHit.distance;button=-1;
   const auto target=ShipSurface::systemAimPoint;
   forward=XMVector3Normalize(XMVectorSubtract(XMVectorSet(target.x,target.y,target.z,0),start));
  }
  if(!hitBody)ShipSurface::systemHeldId=ShipSurface::systemHeldObject=0;
  if(!panel.hovered&&NativeUI::galaxyVisible()&&panel.toolbox.settings.directGame&&!gripDown&&!browserResize.corner&&
     ShipSurface::galaxyHit({XMVectorGetX(start),XMVectorGetY(start),XMVectorGetZ(start)},
                           {XMVectorGetX(forward),XMVectorGetY(forward),XMVectorGetZ(forward)},gameHit)){
   hitStar=hitGame=true;panel.hovered=true;length=gameHit.distance;button=-1;
   const auto target=ShipSurface::galaxyAimPoint;
   forward=XMVector3Normalize(XMVectorSubtract(XMVectorSet(target.x,target.y,target.z,0),start));
  }
  if(!hitStar)ShipSurface::galaxyHeldId=0;
  auto getValue=panel.proc<PFN_xrGetActionStateFloat>("xrGetActionStateFloat");
  if(grip){
   gi.action=grip;
   XrActionStateFloat value{XR_TYPE_ACTION_STATE_FLOAT};
   if(XR_SUCCEEDED(getValue(panel.session,&gi,&value)) && value.isActive){
    if(value.currentState>.35f)lastMotion=time;
    if(!gripDown && value.currentState>=.65f){
     gripDown=true;
     if(panel.hovered&&!hitGame&&!browserResize.corner){
      dragDistance=length;
      dragSurface=hitKeyboard?3:hitBrowser?2:hitSettings?1:0;
      previousDragTime=time;
      XMStoreFloat3(&dragOffset,XMVectorSubtract(XMVectorSet(panelPosition.x,panelPosition.y,panelPosition.z,0),
                   XMVectorMultiplyAdd(forward,XMVectorReplicate(length),start)));
      panelLog("overlay grip drag start",dragSurface);
     }
    } else if(value.currentState<=.35f){
     if(dragDistance>0)saveToolboxPosition(dragSurface==3?panel.keyboardAnchor.position:dragSurface==2?panel.browserAnchor.position:dragSurface==1?panel.settingsAnchor.position:panel.anchor.position,dragSurface);
     gripDown=false;
     dragDistance=0;
    }
   }else dragDistance=0;
  }
  if((dragSurface==1&&!settingsQuad)||(dragSurface==2&&!browserCount)||(dragSurface==3&&!keyboardCount))dragDistance=0;
  if(dragDistance>0){
   const auto dt=static_cast<float>(time-previousDragTime)*1.e-9f;
   previousDragTime=time;
   if(depth && dt>0 && dt<=.1f){
    gi.action=depth;
    XrActionStateVector2f axis{XR_TYPE_ACTION_STATE_VECTOR2F};
    auto getAxis=panel.proc<PFN_xrGetActionStateVector2f>("xrGetActionStateVector2f");
    if(XR_SUCCEEDED(getAxis(panel.session,&gi,&axis)) && axis.isActive){
     const float y=axis.currentState.y;
     if(std::abs(y)>.2f){
      const float change=std::copysign((std::abs(y)-.2f)/.8f,y)*dt;
      dragDistance=std::clamp(dragDistance+change,.35f,4.f);
     }
    }
   }
   auto position=XMVectorAdd(XMVectorMultiplyAdd(forward,XMVectorReplicate(dragDistance),start),XMLoadFloat3(&dragOffset));
   auto& moving=dragSurface==3?panel.keyboardAnchor:dragSurface==2?panel.browserAnchor:dragSurface==1?panel.settingsAnchor:panel.anchor;
   moving.position={XMVectorGetX(position),XMVectorGetY(position),XMVectorGetZ(position)};
   faceToolbox(moving,head.pose.position);
   length=dragDistance;
  }
  if(trigger){
   gi.action=trigger;
   XrActionStateFloat value{XR_TYPE_ACTION_STATE_FLOAT};
   if(XR_SUCCEEDED(getValue(panel.session,&gi,&value)) && value.isActive){
    if(value.currentState>.35f)lastMotion=time;
    if(!triggerDown && value.currentState>=.65f){
     triggerDown=true;
     if(!gripDown){
      if(hitGame){gameClick=true;}
      else if(button==812||button==813){
       const auto convert=[](XMVECTOR value){
        return BrowserResizePoint{XMVectorGetX(value),XMVectorGetY(value),XMVectorGetZ(value)};
       };
       const auto p=panel.browserAnchor.position;
       const auto browserRotation=panel.browserAnchor.orientation;
       const auto basis=XMVectorSet(browserRotation.x,browserRotation.y,browserRotation.z,browserRotation.w);
       const auto axis=[&](float x,float y,float z){return convert(XMVector3Rotate(XMVectorSet(x,y,z,0),basis));};
       if(browserResize.begin(button==812?-1:1,{p.x,p.y,p.z},axis(1,0,0),axis(0,1,0),axis(0,0,1),
                              panel.toolbox.browserWidth,panel.toolbox.browserHeight,convert(start),convert(forward),
                              browserCount>1?panel.toolbox.browserCurve:0.f)){
        panel.toolbox.resizeCorner=browserResize.corner;
        panel.toolbox.dirty=true;
       }
      }else if(button==808||button==SharedKeyboard::Opacity)opacityDrag=true;
      else if(button>=0)panel.toolbox.select(button);
      else if(hitBrowser&&browserPoint.y>=0&&browserPoint.y<panel.toolbox.pageHeight()&&!panel.toolbox.browserSettings){
       panel.toolbox.addressFocused=false;
       panel.toolbox.inputTarget=ToolboxRaster::InputTarget::Page;
       panel.toolbox.dirty=true;
       browserPressed=true;
       browserDragPoint=browserPoint;
       panel.toolbox.browser.post({OpenXRBrowser::Down,browserPoint});
      }
     }
    }else if(value.currentState<=.35f){releaseBrowser(panel);triggerDown=false;if(opacityDrag&&panel.toolbox.keyboardSettings)panel.toolbox.keyboardPreferences(true);opacityDrag=false;finishResize();}
    if(browserResize.corner){
     BrowserResizePoint center{};
     if(browserResize.update({XMVectorGetX(start),XMVectorGetY(start),XMVectorGetZ(start)},
        {XMVectorGetX(forward),XMVectorGetY(forward),XMVectorGetZ(forward)},center,
        panel.toolbox.browserWidth,panel.toolbox.browserHeight))
      {panel.browserAnchor.position={center.x,center.y,center.z};panel.toolbox.dirty=true;}
    }
    if(opacityDrag && hitBrowser && panel.toolbox.browserSettings)
     panel.toolbox.setBrowserOpacity(float(browserPoint.x));
    if(opacityDrag&&hitKeyboard&&panel.toolbox.keyboardSettings)panel.toolbox.setKeyboardOpacity(float(keyboardPoint.x));
   }else {releaseBrowser(panel);triggerDown=false;}
  }
  if(hitBrowser && browserPoint.y>=0 && browserPoint.y<panel.toolbox.pageHeight() && !panel.toolbox.browserSettings && depth && !gripDown && time-lastScroll>=90000000){
   gi.action=depth;
   XrActionStateVector2f axis{XR_TYPE_ACTION_STATE_VECTOR2F};
   auto getAxis=panel.proc<PFN_xrGetActionStateVector2f>("xrGetActionStateVector2f");
   if(XR_SUCCEEDED(getAxis(panel.session,&gi,&axis)) && axis.isActive && std::abs(axis.currentState.y)>.25f){
    panel.toolbox.browser.post({OpenXRBrowser::Scroll,browserPoint,static_cast<int>(axis.currentState.y*120)});
    lastScroll=lastMotion=time;
   }
  }
  if(time-lastMotion>=static_cast<XrTime>(panel.toolbox.settings.idle)*1000000000){NativeUI::clearPointer();return false;}
  if(hitStar){if(transparentPanel)NativeUI::point(panelProbe,false,false);else NativeUI::clearPointer();if(gameClick&&!gripDown){if(hitBody)ShipSurface::systemSelect();else ShipSurface::galaxySelect();}}
  else if(transparentPanel)NativeUI::point(panelProbe,false,false);
  else if(hitGame&&!gripDown)NativeUI::point(gameHit,gameClick,triggerDown);
  else NativeUI::clearPointer();
  if(hitKeyboard&&panel.toolbox.keySnap&&!panel.toolbox.keyboardSettings&&!gripDown&&
     button>=SharedKeyboard::First&&button<SharedKeyboard::Keys){
   const auto box=SharedKeyboard::keyRects[button-SharedKeyboard::First];
   const float x=(box.left+box.right)*.5f,y=(box.top+box.bottom)*.5f;
   for(int i=0;i<keyboardCount;++i){
    const auto& facet=keyboardQuads[i];
    const auto rect=facet.subImage.imageRect;
    if(x<rect.offset.x||x>=rect.offset.x+rect.extent.width)continue;
    const auto p=facet.pose.position;
    const auto facetRotation=facet.pose.orientation;
    const auto local=XMVectorSet(((x-rect.offset.x)/rect.extent.width-.5f)*facet.size.width,
                                (.5f-y/rect.extent.height)*facet.size.height,0,0);
    const auto endpoint=XMVectorAdd(XMVectorSet(p.x,p.y,p.z,0),XMVector3Rotate(local,XMVectorSet(facetRotation.x,facetRotation.y,facetRotation.z,facetRotation.w)));
    const auto delta=XMVectorSubtract(endpoint,start);
    length=XMVectorGetX(XMVector3Length(delta));
    forward=XMVector3Normalize(delta);
    break;
   }
  }
  auto center=XMVectorMultiplyAdd(forward,XMVectorReplicate(length*.5f),start);
  auto right=XMVector3Cross(forward,XMVectorSubtract(eye,center));
  if(XMVectorGetX(XMVector3LengthSq(right))<.000001f)return false;
  right=XMVector3Normalize(right);
  auto normal=XMVector3Cross(right,forward);
  XMFLOAT4 orientation;
  XMStoreFloat4(&orientation,XMQuaternionRotationMatrix(XMMATRIX(right,forward,normal,XMVectorSet(0,0,0,1))));
  ray=source;
  ray.subImage.imageRect={{0,ToolboxRaster::AtlasHeight-1},{8,1}};
  ray.pose.orientation={orientation.x,orientation.y,orientation.z,orientation.w};
  ray.pose.position={XMVectorGetX(center),XMVectorGetY(center),XMVectorGetZ(center)};
  ray.size={panel.toolbox.settings.width*.001f,length};
  if(dot&&panel.hovered){
   *dot=source;
   dot->layerFlags=XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
   dot->subImage.imageRect={{0,ToolboxRaster::DotY},{ToolboxRaster::DotPixels,ToolboxRaster::DotPixels}};
   const auto endpoint=XMVectorMultiplyAdd(forward,XMVectorReplicate(length),start);
   dot->pose.position={XMVectorGetX(endpoint),XMVectorGetY(endpoint),XMVectorGetZ(endpoint)};
   faceToolbox(dot->pose,head.pose.position);
   // Galaxy geometry can be tens of metres away: keep its marker readable in angle.
   const float diameter=hitStar?std::max(.012f,length*.012f):std::clamp(ray.size.width*2.5f,.005f,.018f);
   dot->size={diameter,diameter};
  }
  static bool logged=false;
  if(!logged){panelLog("controller ray active");logged=true;}
  return true;
 }
};
