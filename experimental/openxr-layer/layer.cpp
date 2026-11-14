#include "diagnostic.h"
#include "panel.h"
#include "ship_surface.h"
#include "controller.h"
#include <cstring>
#include "../openxr-sdk/include/openxr/openxr_loader_negotiation.h"
static PFN_xrGetInstanceProcAddr nextGet = nullptr;
static TestPanel panel;
static ControllerRay controller;
static PFN_xrAttachSessionActionSets attachNext;
static PFN_xrSyncActions syncNext;
static bool hostSync=false;
static PFN_xrSuggestInteractionProfileBindings suggestNext;
static PFN_xrPollEvent pollNext;
static XrResult XRAPI_CALL pollHook(XrInstance instance,XrEventDataBuffer* event){
 const auto result=pollNext(instance,event);
 if(result==XR_SUCCESS&&event&&event->type==XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED){
  const auto* state=reinterpret_cast<const XrEventDataSessionStateChanged*>(event);
  if(state->session==panel.session){OnFootMovement::setFocused(state->state==XR_SESSION_STATE_FOCUSED);NativeSystem::setFocused(state->state==XR_SESSION_STATE_FOCUSED);}
 }
 return result;
}
static XrResult XRAPI_CALL suggestHook(XrInstance instance,const XrInteractionProfileSuggestedBinding* info) {
 if(!info || !controller.aim || instance!=panel.instance || info->interactionProfile!=controller.profile)
  return suggestNext(instance,info);
 std::vector<XrActionSuggestedBinding> bindings;
 for(uint32_t i=0;i<info->countSuggestedBindings;++i)bindings.push_back(info->suggestedBindings[i]);
 bindings.push_back({controller.aim,controller.binding});
 if(controller.trigger)bindings.push_back({controller.trigger,controller.triggerBinding});
 if(controller.grip)bindings.push_back({controller.grip,controller.gripBinding});
 if(controller.depth)bindings.push_back({controller.depth,controller.depthBinding});
 if(controller.workspaceToggle)bindings.push_back({controller.workspaceToggle,controller.toggleBinding});
 for(uint32_t i=0;i<controller.leftCount;++i)bindings.push_back(controller.leftBindings[i]);
 auto copy=*info;copy.countSuggestedBindings=(uint32_t)bindings.size();copy.suggestedBindings=bindings.data();
 auto result=suggestNext(instance,&copy);
 return XR_FAILED(result)?suggestNext(instance,info):result;
}
static XrResult XRAPI_CALL attachHook(XrSession session,const XrSessionActionSetsAttachInfo* info) {
 if(!info || !controller.aim || session!=panel.session || controller.attached)return attachNext(session,info);
 std::vector<XrActionSet> sets;
 for(uint32_t i=0;i<info->countActionSets;++i)sets.push_back(info->actionSets[i]);
 sets.push_back(controller.set);
 auto copy=*info;copy.countActionSets=(uint32_t)sets.size();copy.actionSets=sets.data();
 auto result=attachNext(session,&copy);panelLog("attach controller action set",result);
 if(XR_FAILED(result))return attachNext(session,info);
 controller.connect(panel);
 return result;
}
static XrResult XRAPI_CALL syncHook(XrSession session,const XrActionsSyncInfo* info) {
 if(!info || !controller.space || session!=panel.session)return syncNext(session,info);
 hostSync=true;
 std::vector<XrActiveActionSet> sets;
 for(uint32_t i=0;i<info->countActiveActionSets;++i)sets.push_back(info->activeActionSets[i]);
 sets.push_back({controller.set,XR_NULL_PATH});
 auto copy=*info;copy.countActiveActionSets=(uint32_t)sets.size();copy.activeActionSets=sets.data();
 auto result=syncNext(session,&copy);
 return XR_FAILED(result)?syncNext(session,info):result;
}
static PFN_xrCreateSession createSessionNext;
static PFN_xrEndFrame endFrameNext;
static PFN_xrDestroySession destroySessionNext;
static PFN_xrLocateViews locateViewsNext;
static XrResult XRAPI_CALL locateViewsHook(XrSession session,const XrViewLocateInfo* info,XrViewState* state,uint32_t capacity,uint32_t* count,XrView* output){
 const auto result=locateViewsNext(session,info,state,capacity,count,output);
 if(XR_SUCCEEDED(result)&&info&&state&&count&&output&&*count==2&&capacity>=2&&session==panel.session&&panel.world&&
    (state->viewStateFlags&(XR_VIEW_STATE_POSITION_VALID_BIT|XR_VIEW_STATE_ORIENTATION_VALID_BIT))==(XR_VIEW_STATE_POSITION_VALID_BIT|XR_VIEW_STATE_ORIENTATION_VALID_BIT)){
  XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
  const auto locate=panel.proc<PFN_xrLocateSpace>("xrLocateSpace");
  if(locate&&XR_SUCCEEDED(locate(info->space,panel.world,info->displayTime,&location))&&
     (location.locationFlags&(XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT))==(XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)){
   using namespace DirectX;
   const auto pose=location.pose;
   const auto q=XMVectorSet(pose.orientation.x,pose.orientation.y,pose.orientation.z,pose.orientation.w);
   const auto p=XMVectorSet(pose.position.x,pose.position.y,pose.position.z,0);
   ShipSurface::Views frame{};frame.stamp=GetTickCount64();
   for(int i=0;i<2;++i){
    const auto v=output[i].pose;
    const auto position=XMVectorAdd(XMVector3Rotate(XMVectorSet(v.position.x,v.position.y,v.position.z,0),q),p);
    const auto rotation=XMQuaternionMultiply(XMVectorSet(v.orientation.x,v.orientation.y,v.orientation.z,v.orientation.w),q);
    XMStoreFloat3(reinterpret_cast<XMFLOAT3*>(&frame.eye[i].pose.position),position);
    XMStoreFloat4(reinterpret_cast<XMFLOAT4*>(&frame.eye[i].pose.orientation),rotation);
    frame.eye[i].fov=output[i].fov;
   }
   std::lock_guard lock(ShipSurface::viewMutex);ShipSurface::views=frame;
  }
 }
 return result;
}
static XrResult XRAPI_CALL destroySessionHook(XrSession session){if(session==panel.session){OnFootMovement::setFocused(false);NativeSystem::setFocused(false);ShipSurface::active=false;controller.disconnect(panel);panel.shutdown();}return destroySessionNext(session);}
static XrResult XRAPI_CALL createSessionHook(XrInstance instance,const XrSessionCreateInfo* info,XrSession* session){auto r=createSessionNext(instance,info,session);panelLog("xrCreateSession",r);if(XR_SUCCEEDED(r)){panel.initialize(*session,info);if(panel.device)panelLog("ship surface capture initialized",ShipSurface::initialize(panel.device)?0:-1);}return r;}
static XrResult XRAPI_CALL endFrameHook(XrSession session,const XrFrameEndInfo* info){
 static bool entered=false;if(!entered){panelLog("first xrEndFrame hook");entered=true;}
 if(!info||session!=panel.session)return endFrameNext(session,info);
 if(controller.aim && !hostSync && info->layerCount) {
  static bool tried=false;
  if(!tried && !controller.attached){
   tried=true;
   XrSessionActionSetsAttachInfo attach{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
   attach.countActionSets=1;attach.actionSets=&controller.set;
   auto result=panel.proc<PFN_xrAttachSessionActionSets>("xrAttachSessionActionSets")(session,&attach);
   panelLog("attach standalone controller action set",result);
   if(XR_SUCCEEDED(result))controller.connect(panel);
  }
  if(controller.space){
   XrActiveActionSet active{controller.set,XR_NULL_PATH};
   XrActionsSyncInfo sync{XR_TYPE_ACTIONS_SYNC_INFO};sync.countActiveActionSets=1;sync.activeActionSets=&active;
   panel.proc<PFN_xrSyncActions>("xrSyncActions")(session,&sync);
  }
 }
 if(panel.toolbox.settings.movementReady!=OnFootMovement::ready()){
  panel.toolbox.settings.movementReady=OnFootMovement::ready();panel.toolbox.dirty=true;
 }
 controller.updateWorkspaceVisibility(panel);
 ShipSurface::diagnostics();
 if(panel.toolbox.settings.onFootMovement||panel.toolbox.settings.triggerInteract){
  static ULONGLONG lastMovementLog=0;
  if(GetTickCount64()-lastMovementLog>=10000){
   lastMovementLog=GetTickCount64();uint64_t values[5]{};OnFootMovement::counters(values);
   char text[200]{};sprintf_s(text,"on-foot analog polls=%llu outputs=%llu status_allowed=%llu x=%d y=%d turn=%d buttons=%04x slot=%llu",values[0],values[1],values[2],static_cast<short>(values[3]&0xffff),static_cast<short>(values[3]>>16),static_cast<short>(values[3]>>32),static_cast<unsigned>((values[3]>>48)&0xffff),values[4]);panelLog(text);
  }
 }
 ShipSurface::active=NativeUI::ready()&&panel.toolbox.settings.directGame&&panel.workspaceVisible;
 if(!panel.workspaceVisible){NativeUI::clearPointer();controller.updateMapNavigation(panel,info->displayTime,false);controller.updateMovement(panel,info->displayTime,false);controller.updateInteraction(panel,info->displayTime,false);return endFrameNext(session,info);}
 if(!panel.anchored){
  bool hasScene=false;
  for(uint32_t i=0;i<info->layerCount;++i)
   if(info->layers[i] && info->layers[i]->type==XR_TYPE_COMPOSITION_LAYER_PROJECTION)hasScene=true;
  if(!hasScene)return endFrameNext(session,info);
 }
 XrCompositionLayerQuad quad{};if(!panel.render(quad,info->displayTime))return endFrameNext(session,info);
 std::vector<const XrCompositionLayerBaseHeader*> layers;
 for(uint32_t i=0;i<info->layerCount;++i)layers.push_back(info->layers[i]);
 layers.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader*>(&quad));
 XrCompositionLayerQuad settings{};
 if(panel.toolbox.settings.visible&&panel.submittedSettings)settings=panel.settingsQuad(quad,info->displayTime);
 const auto* visibleSettings=panel.toolbox.settings.visible&&panel.submittedSettings?&settings:nullptr;
 if(visibleSettings)layers.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader*>(visibleSettings));
 std::array<XrCompositionLayerQuad,TestPanel::BrowserFacets> browser{};
 int browserCount=0;
 if(panel.toolbox.state.browser&&panel.submittedBrowser){
  const auto flat=panel.browserQuad(quad,info->displayTime);
  browserCount=panel.browserSurfaces(flat,browser,static_cast<uint32_t>(layers.size()+2+(panel.toolbox.state.keyboard?1:0)));
  for(int i=0;i<browserCount;++i)layers.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader*>(&browser[i]));
 }
 std::array<XrCompositionLayerQuad,TestPanel::BrowserFacets> keyboard{};
 int keyboardCount=0;
 if(panel.toolbox.state.keyboard&&panel.submittedKeyboard&&layers.size()+1<panel.maxLayers){
  const auto flat=panel.keyboardQuad(quad,info->displayTime);
  keyboardCount=panel.browserSurfaces(flat,keyboard,static_cast<uint32_t>(layers.size()+2),panel.toolbox.keyboardCurve);
  for(int i=0;i<keyboardCount;++i)layers.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader*>(&keyboard[i]));
 }
 XrCompositionLayerQuad ray{},dot{};
 if(controller.draw(panel,info->displayTime,quad,ray,visibleSettings,browser.data(),browserCount,keyboard.data(),keyboardCount,&dot)&&layers.size()<panel.maxLayers){
  layers.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader*>(&ray));
  if(dot.type&&layers.size()<panel.maxLayers)
   layers.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader*>(&dot));
 }
 const bool interactionBlocked=panel.hovered||controller.dragDistance>0||controller.browserResize.corner!=0;
 controller.updateMapNavigation(panel,info->displayTime,controller.mapNavigationBlocked||controller.triggerDown||controller.dragDistance>0||controller.browserResize.corner!=0);
 controller.updateMovement(panel,info->displayTime,interactionBlocked);
 controller.updateInteraction(panel,info->displayTime,interactionBlocked);
 auto copy=*info;
 copy.layerCount=static_cast<uint32_t>(layers.size());
 copy.layers=layers.data();
 auto result=endFrameNext(session,&copy);
 static bool sent=false;
 if(!sent){panelLog("xrEndFrame with panel",result);sent=true;}
 return result;
}
static XrResult XRAPI_CALL layerGet(XrInstance instance,const char* name,PFN_xrVoidFunction* fn) {
    if(!name || !fn || !nextGet) return XR_ERROR_VALIDATION_FAILURE;
    auto r=nextGet(instance,name,fn);if(XR_FAILED(r)||!*fn)return r;
    if(!std::strcmp(name,"xrPollEvent")){pollNext=(PFN_xrPollEvent)*fn;*fn=(PFN_xrVoidFunction)pollHook;}
    if(!std::strcmp(name,"xrCreateSession")){createSessionNext=(PFN_xrCreateSession)*fn;*fn=(PFN_xrVoidFunction)createSessionHook;}
    if(!std::strcmp(name,"xrDestroySession")){destroySessionNext=(PFN_xrDestroySession)*fn;*fn=(PFN_xrVoidFunction)destroySessionHook;}
    if(!std::strcmp(name,"xrEndFrame")){endFrameNext=(PFN_xrEndFrame)*fn;*fn=(PFN_xrVoidFunction)endFrameHook;}
    if(!std::strcmp(name,"xrLocateViews")){locateViewsNext=(PFN_xrLocateViews)*fn;*fn=(PFN_xrVoidFunction)locateViewsHook;}
    if(!std::strcmp(name,"xrAttachSessionActionSets")){attachNext=(PFN_xrAttachSessionActionSets)*fn;*fn=(PFN_xrVoidFunction)attachHook;}
    if(!std::strcmp(name,"xrSyncActions")){syncNext=(PFN_xrSyncActions)*fn;*fn=(PFN_xrVoidFunction)syncHook;}
    if(!std::strcmp(name,"xrSuggestInteractionProfileBindings")){suggestNext=(PFN_xrSuggestInteractionProfileBindings)*fn;*fn=(PFN_xrVoidFunction)suggestHook;}
    return r;
}
static XrResult XRAPI_CALL layerCreate(const XrInstanceCreateInfo* info,const XrApiLayerCreateInfo* layer,XrInstance* instance) {
    if(!info || !layer || !layer->nextInfo || !instance) return XR_ERROR_INITIALIZATION_FAILED;
    auto next=layer->nextInfo;
    if(!next->nextGetInstanceProcAddr || !next->nextCreateApiLayerInstance) return XR_ERROR_INITIALIZATION_FAILED;
    XrApiLayerCreateInfo forwarded=*layer;forwarded.nextInfo=next->next;
    auto result=next->nextCreateApiLayerInstance(info,&forwarded,instance);
    panelLog("xrCreateApiLayerInstance",result);if(XR_SUCCEEDED(result)){nextGet=next->nextGetInstanceProcAddr;panel.get=nextGet;panel.instance=*instance;controller={};hostSync=false;if(panel.proc<PFN_xrStringToPath>("xrStringToPath"))controller.initialize(panel);}
    return result;
}
extern "C" __declspec(dllexport) XrResult XRAPI_CALL xrNegotiateLoaderApiLayerInterface(const XrNegotiateLoaderInfo* loader,const char* name,XrNegotiateApiLayerRequest* request) {
    if(!loader || !name || !request || std::strcmp(name,"XR_APILAYER_ELITEDCCVR_overlay") || loader->structType!=XR_LOADER_INTERFACE_STRUCT_LOADER_INFO || request->structType!=XR_LOADER_INTERFACE_STRUCT_API_LAYER_REQUEST || loader->structSize<sizeof(*loader) || request->structSize<sizeof(*request) || loader->minInterfaceVersion>1 || loader->maxInterfaceVersion<1) return XR_ERROR_INITIALIZATION_FAILED;
    panelLog("layer negotiation accepted");
    request->layerInterfaceVersion=1;request->layerApiVersion=XR_MAKE_VERSION(1,0,0);request->getInstanceProcAddr=layerGet;request->createApiLayerInstance=layerCreate;
    return XR_SUCCESS;
}
