#pragma once
#include "diagnostic.h"
#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11
#include <windows.h>
#include <d3d11.h>
#include <vector>
#include <array>
#include <DirectXMath.h>
#include <cmath>
#include "../openxr-sdk/include/openxr/openxr_platform.h"
#include "toolbox_raster.h"
inline void faceToolbox(XrPosef& pose, XrVector3f eyePosition) {
 using namespace DirectX;
 const auto position=pose.position;
 auto facing=XMVectorSet(eyePosition.x-position.x,eyePosition.y-position.y,eyePosition.z-position.z,0);
 auto right=XMVector3Cross(XMVectorSet(0,1,0,0),facing);
 if(XMVectorGetX(XMVector3LengthSq(right))<=.000001f)return;
 facing=XMVector3Normalize(facing);
 right=XMVector3Normalize(right);
 auto up=XMVector3Cross(facing,right);
 XMFLOAT4 rotation;
 XMStoreFloat4(&rotation,XMQuaternionRotationMatrix(XMMATRIX(right,up,facing,XMVectorSet(0,0,0,1))));
 pose.orientation={rotation.x,rotation.y,rotation.z,rotation.w};
}

inline bool toolboxPlacementPath(wchar_t (&path)[MAX_PATH],int surface) {
 wchar_t directory[MAX_PATH]{};
 const DWORD size=GetEnvironmentVariableW(L"LOCALAPPDATA",directory,MAX_PATH);
 return size>0 && size<MAX_PATH && swprintf_s(path,L"%ls\\EliteDccVR-OpenXR-%ls.position",directory,surface==3?L"Keyboard":surface==2?L"Browser":surface==1?L"Settings":L"Toolbox")>0;
}

inline bool loadToolboxPosition(XrVector3f& position,int surface=0) {
 wchar_t path[MAX_PATH]{};
 FILE* file=nullptr;
 if(!toolboxPlacementPath(path,surface)||_wfopen_s(&file,path,L"r")!=0)return false;
 XrVector3f value{};
 const int count=fscanf_s(file,"%f %f %f",&value.x,&value.y,&value.z);
 fclose(file);
 if(count!=3||!std::isfinite(value.x)||!std::isfinite(value.y)||!std::isfinite(value.z))return false;
 position=value;
 return true;
}

inline void saveToolboxPosition(XrVector3f position,int surface=0) {
 wchar_t path[MAX_PATH]{},temporary[MAX_PATH]{};
 if(!toolboxPlacementPath(path,surface)||swprintf_s(temporary,L"%ls.%lu.tmp",path,GetCurrentProcessId())<0)return;
 FILE* file=nullptr;
 if(_wfopen_s(&file,temporary,L"w")!=0)return;
 const bool written=fprintf(file,"%.9g %.9g %.9g\n",position.x,position.y,position.z)>0;
 const bool closed=fclose(file)==0;
 if(written&&closed&&MoveFileExW(temporary,path,MOVEFILE_REPLACE_EXISTING))panelLog(surface==2?"browser position saved":surface==1?"settings position saved":"toolbox position saved");
 else DeleteFileW(temporary);
}

struct TestPanel {
 ToolboxRaster toolbox;
 bool workspaceVisible=true;
 std::vector<XrSwapchainImageD3D11KHR> images;
 uint64_t releasedGeneration=0,releasedPage=0;
 BrowserGpuFrame latestPage;
 HANDLE openedPage{};
 ComPtr<ID3D11Texture2D> pageTexture;
 ComPtr<IDXGIKeyedMutex> pageMutex;
 ComPtr<ID3D11Texture2D> chromeTexture;
 uint64_t chromeGeneration=0;
 uint32_t gpuUploads=0;
 bool pollPage(){
  if(!toolbox.state.browser||toolbox.browserSettings)return false;
  toolbox.browser.latestGpu(latestPage);
  if(!latestPage.handle)return false;
  if(openedPage!=latestPage.handle){
   pageTexture.Reset();pageMutex.Reset();openedPage=nullptr;
   if(FAILED(device->OpenSharedResource(latestPage.handle,IID_PPV_ARGS(&pageTexture)))||
      FAILED(pageTexture.As(&pageMutex))){
    static bool logged=false;
    if(!logged){panelLog("open browser GPU texture failed");logged=true;}
    return false;
   }
   openedPage=latestPage.handle;
  }
  return true;
 }
 uint32_t acquiredIndex=0;
 bool imageAcquired=false;
 XrTime nextPaint=0;
 bool submittedKeyboard=false;
 bool submittedBrowser=false,submittedSettings=false;
 int submittedWidth=0,submittedHeight=0,submittedToolboxWidth=0;
 uint32_t timedOutImages=0,uploadCount=0,frameCount=0;
 long maxFrameWorkUs=0;
 ULONGLONG nextTimingLog=0;
 LARGE_INTEGER clockFrequency{};

 bool updateTexture(bool useGpu=false){
  if(releasedGeneration==toolbox.paintGeneration && (!useGpu||releasedPage==latestPage.serial))return true;
  struct Lock {
   IDXGIKeyedMutex* gate=nullptr;
   ~Lock(){if(gate)gate->ReleaseSync(0);}
  } pageLock;
  if(useGpu){
   if(!pageMutex||pageMutex->AcquireSync(0,0)!=S_OK)return false;
   pageLock.gate=pageMutex.Get();
  }
  if(images.empty()){
   uint32_t count=0;
   auto enumerate=proc<PFN_xrEnumerateSwapchainImages>("xrEnumerateSwapchainImages");
   auto result=enumerate(swapchain,0,&count,nullptr);
   if(XR_FAILED(result)||!count){panelLog("enumerate images count",result);return false;}
   images.resize(count,{XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
   result=enumerate(swapchain,count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data()));
   if(XR_FAILED(result)){images.clear();panelLog("enumerate images",result);return false;}
  }
  if(!imageAcquired){
   XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
   auto result=proc<PFN_xrAcquireSwapchainImage>("xrAcquireSwapchainImage")(swapchain,&acquire,&acquiredIndex);
   if(XR_FAILED(result))return false;
   imageAcquired=true;
  }
  XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
  wait.timeout=0;
  const auto waited=proc<PFN_xrWaitSwapchainImage>("xrWaitSwapchainImage")(swapchain,&wait);
  if(waited==XR_TIMEOUT_EXPIRED){++timedOutImages;return false;}
  if(XR_FAILED(waited))return false;
  auto* texture=images.at(acquiredIndex).texture;
  ID3D11DeviceContext* context=nullptr;
  device->GetImmediateContext(&context);
  const UINT rows=toolbox.paintedBrowser?ToolboxRaster::BrowserY+toolbox.paintedHeight:
      toolbox.paintedKeyboard?ToolboxRaster::KeyboardY+SharedKeyboard::Height:toolbox.paintedSettings?ToolboxRaster::SettingsY+900:210;
  const UINT columns=std::max(1200,toolbox.paintedBrowser?toolbox.paintedWidth:0);
  const D3D11_BOX content{0,0,0,columns,rows,1};
  const D3D11_BOX ray{0,ToolboxRaster::DotY,0,ToolboxRaster::DotPixels,ToolboxRaster::AtlasHeight,1};
  if(!chromeTexture){
   D3D11_TEXTURE2D_DESC desc{};
   desc.Width=ToolboxRaster::AtlasWidth;desc.Height=ToolboxRaster::AtlasHeight;
   desc.MipLevels=desc.ArraySize=1;desc.Format=imageFormat;desc.SampleDesc.Count=1;
   desc.Usage=D3D11_USAGE_DEFAULT;
   device->CreateTexture2D(&desc,nullptr,&chromeTexture);
  }
  auto* chrome=chromeTexture?chromeTexture.Get():texture;
  if(!chromeTexture||chromeGeneration!=toolbox.paintGeneration){
   context->UpdateSubresource(chrome,0,&content,toolbox.pixels.data(),ToolboxRaster::AtlasWidth*4,0);
   context->UpdateSubresource(chrome,0,&ray,toolbox.pixels.data()+
       ToolboxRaster::DotY*ToolboxRaster::AtlasWidth,ToolboxRaster::AtlasWidth*4,0);
   chromeGeneration=toolbox.paintGeneration;
  }
  if(chromeTexture){
   context->CopySubresourceRegion(texture,0,0,0,0,chrome,0,&content);
   context->CopySubresourceRegion(texture,0,0,ray.top,0,chrome,0,&ray);
  }
  if(useGpu){
   const UINT width=static_cast<UINT>(std::min(latestPage.width,toolbox.paintedWidth));
   const UINT height=static_cast<UINT>(std::min(latestPage.height,
       toolbox.paintedHeight-OpenXRBrowser::Toolbar-OpenXRBrowser::Footer));
   const D3D11_BOX region{0,0,0,width,height,1};
   context->CopySubresourceRegion(texture,0,0,ToolboxRaster::BrowserY+OpenXRBrowser::Toolbar,0,pageTexture.Get(),0,&region);
  }
  context->Flush();
  context->Release();
  XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
  const auto result=proc<PFN_xrReleaseSwapchainImage>("xrReleaseSwapchainImage")(swapchain,&release);
  if(XR_FAILED(result)){panelLog("release image",result);return false;}
  imageAcquired=false;
  releasedGeneration=toolbox.paintGeneration;
  if(useGpu){releasedPage=latestPage.serial;++gpuUploads;}else releasedPage=0;
  submittedKeyboard=toolbox.paintedKeyboard;
  submittedBrowser=toolbox.paintedBrowser;
  submittedSettings=toolbox.paintedSettings;
  submittedWidth=toolbox.paintedWidth;
  submittedHeight=toolbox.paintedHeight;
  submittedToolboxWidth=toolbox.paintedToolboxWidth;
  ++uploadCount;
  return true;
 }
 XrPosef settingsAnchor{}, browserAnchor{};
 bool browserAnchored=false;
 uint32_t maxLayers=0;
 bool settingsAnchored=false;
 XrSession session{};XrSpace space{};XrSpace world{};XrPosef anchor{};bool anchored=false;bool hovered=false,clicked=false;XrSwapchain swapchain{};ID3D11Device* device{};
 PFN_xrGetInstanceProcAddr get{};XrInstance instance{};DXGI_FORMAT imageFormat=DXGI_FORMAT_UNKNOWN;
 template<class T>T proc(const char* name){PFN_xrVoidFunction f{};get(instance,name,&f);return reinterpret_cast<T>(f);}
 void shutdown(){
  NativeUI::enable(false);
  OnFootMovement::configure(false);
  OnFootMovement::configureInteraction(false);
  if(swapchain){proc<PFN_xrDestroySwapchain>("xrDestroySwapchain")(swapchain);swapchain=XR_NULL_HANDLE;}
  if(world){proc<PFN_xrDestroySpace>("xrDestroySpace")(world);world=XR_NULL_HANDLE;}anchored=false;settingsAnchored=false;browserAnchored=false;hovered=false;clicked=false;
  if(space){proc<PFN_xrDestroySpace>("xrDestroySpace")(space);space=XR_NULL_HANDLE;}
  session=XR_NULL_HANDLE;device=nullptr;
  toolbox.browser.stop();
  toolbox.state={};
  workspaceVisible=true;
  toolbox.settings.visible=false;
  toolbox.dirty=true;
  images.clear();
  releasedGeneration=releasedPage=0;
  latestPage={};openedPage=nullptr;pageTexture.Reset();pageMutex.Reset();
  chromeTexture.Reset();chromeGeneration=0;
  imageAcquired=false;
  nextPaint=0;
  submittedKeyboard=submittedBrowser=submittedSettings=false;keyboardAnchored=false;
 }
 void initialize(XrSession s,const XrSessionCreateInfo* info){
  if(session)return;session=s;toolbox.settings.load();
  toolbox.settings.movementReady=OnFootMovement::initialize();
  if(!OnFootMovement::configure(toolbox.settings.onFootMovement))toolbox.settings.onFootMovement=false;
  if(!OnFootMovement::configureInteraction(toolbox.settings.triggerInteract))toolbox.settings.triggerInteract=false;
  panelLog(toolbox.settings.movementReady?"analog movement bridge ready":"analog movement bridge unavailable");
  if(toolbox.settings.directGame&&!NativeUI::enable(true)){toolbox.settings.directGame=false;panelLog("native UI initialization refused");}
  XrSystemProperties properties{XR_TYPE_SYSTEM_PROPERTIES};
  auto getProperties=proc<PFN_xrGetSystemProperties>("xrGetSystemProperties");
  if(getProperties&&XR_SUCCEEDED(getProperties(instance,info->systemId,&properties)))
   maxLayers=properties.graphicsProperties.maxLayerCount;auto chain=(const XrBaseInStructure*)info->next;
  while(chain && chain->type!=XR_TYPE_GRAPHICS_BINDING_D3D11_KHR)chain=chain->next;
  if(!chain){panelLog("D3D11 graphics binding not found");return;}device=((const XrGraphicsBindingD3D11KHR*)chain)->device;
  XrReferenceSpaceCreateInfo ref{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};ref.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_VIEW;ref.poseInReferenceSpace.orientation.w=1;
  auto spaceResult=proc<PFN_xrCreateReferenceSpace>("xrCreateReferenceSpace")(s,&ref,&space);panelLog("xrCreateReferenceSpace",spaceResult);if(XR_FAILED(spaceResult))return;
  ref.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_LOCAL;auto worldResult=proc<PFN_xrCreateReferenceSpace>("xrCreateReferenceSpace")(s,&ref,&world);panelLog("xrCreateReferenceSpace LOCAL",worldResult);if(XR_FAILED(worldResult))return;
  XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};ci.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;ci.format=DXGI_FORMAT_R8G8B8A8_UNORM;ci.sampleCount=1;ci.width=ToolboxRaster::AtlasWidth;ci.height=ToolboxRaster::AtlasHeight;ci.faceCount=1;ci.arraySize=1;ci.mipCount=1;
  uint32_t formatsCount=0;auto formatsFn=proc<PFN_xrEnumerateSwapchainFormats>("xrEnumerateSwapchainFormats");
  auto formatResult=formatsFn(s,0,&formatsCount,nullptr);panelLog("xrEnumerateSwapchainFormats count",formatResult);if(XR_FAILED(formatResult))return;
  std::vector<int64_t> formats(formatsCount);if(XR_FAILED(formatsFn(s,formatsCount,&formatsCount,formats.data())))return;
  bool supported=false;for(auto preferred:{int64_t(DXGI_FORMAT_R8G8B8A8_UNORM),int64_t(DXGI_FORMAT_B8G8R8A8_UNORM)})for(auto f:formats)if(f==preferred&&!supported){ci.format=preferred;supported=true;}if(!supported){panelLog("no supported RGBA/BGRA format");return;}
  imageFormat=(DXGI_FORMAT)ci.format;panelLog("selected swapchain format",static_cast<long>(ci.format));
  toolbox.browser.configureGraphics(device,imageFormat);
  auto swapResult=proc<PFN_xrCreateSwapchain>("xrCreateSwapchain")(s,&ci,&swapchain);panelLog("xrCreateSwapchain",swapResult);
 }
 XrCompositionLayerQuad settingsQuad(const XrCompositionLayerQuad& toolboxQuad,XrTime time) {
  using namespace DirectX;
  auto result=toolboxQuad;
  result.subImage.imageRect={{0,ToolboxRaster::SettingsY},{1200,900}};
  result.size={.98f*1200.f/970.f,.98f*900.f/970.f};
  if(!settingsAnchored){
   settingsAnchor=toolboxQuad.pose;
   const auto q=settingsAnchor.orientation;
   const auto p=settingsAnchor.position;
   const float offset=(result.size.height+toolboxQuad.size.height)*.5f+.08f;
   auto position=XMVectorAdd(XMVectorSet(p.x,p.y,p.z,0),
       XMVector3Rotate(XMVectorSet(0,offset,0,0),XMVectorSet(q.x,q.y,q.z,q.w)));
   settingsAnchor.position={XMVectorGetX(position),XMVectorGetY(position),XMVectorGetZ(position)};
   if(loadToolboxPosition(settingsAnchor.position,true))panelLog("settings position restored");
   if(get && space && world){
    XrSpaceLocation head{XR_TYPE_SPACE_LOCATION};
    auto locate=proc<PFN_xrLocateSpace>("xrLocateSpace");
    if(locate && XR_SUCCEEDED(locate(space,world,time,&head)) &&
       (head.locationFlags&XR_SPACE_LOCATION_POSITION_VALID_BIT))faceToolbox(settingsAnchor,head.pose.position);
   }
   settingsAnchored=true;
  }
  result.pose=settingsAnchor;
  return result;
 }
 XrCompositionLayerQuad browserQuad(const XrCompositionLayerQuad& source,XrTime time) {
  using namespace DirectX;
  auto result=source;
  result.subImage.imageRect={{0,ToolboxRaster::BrowserY},{submittedWidth?submittedWidth:toolbox.browserPixelWidth(),submittedHeight?submittedHeight:toolbox.browserPixelHeight()}};
  result.size={toolbox.browserWidth,toolbox.browserHeight};
  if(!browserAnchored){
   browserAnchor=source.pose;
   const auto q=source.pose.orientation;
   const auto p=source.pose.position;
   const float side=toolbox.settings.visible?1.15f:0.f;
   auto position=XMVectorAdd(XMVectorSet(p.x,p.y,p.z,0),
       XMVector3Rotate(XMVectorSet(side,(result.size.height+source.size.height)*.5f+.08f,0,0),XMVectorSet(q.x,q.y,q.z,q.w)));
   browserAnchor.position={XMVectorGetX(position),XMVectorGetY(position),XMVectorGetZ(position)};
   if(loadToolboxPosition(browserAnchor.position,2))panelLog("browser position restored");
   if(get&&space&&world){
    XrSpaceLocation head{XR_TYPE_SPACE_LOCATION};
    auto locate=proc<PFN_xrLocateSpace>("xrLocateSpace");
    if(locate&&XR_SUCCEEDED(locate(space,world,time,&head))&&
       (head.locationFlags&XR_SPACE_LOCATION_POSITION_VALID_BIT))faceToolbox(browserAnchor,head.pose.position);
   }
   browserAnchored=true;
  }
  result.pose=browserAnchor;
  result.layerFlags=XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
  return result;
 }
 XrPosef keyboardAnchor{};
 bool keyboardAnchored=false;
 XrCompositionLayerQuad keyboardQuad(const XrCompositionLayerQuad& source,XrTime time){
  using namespace DirectX;
  auto result=source;
  result.subImage.imageRect={{0,ToolboxRaster::KeyboardY},{SharedKeyboard::Width,SharedKeyboard::Height}};
  result.size={.9f,.9f*SharedKeyboard::Height/SharedKeyboard::Width};
  if(!keyboardAnchored){
   keyboardAnchor=source.pose;
   const auto q=source.pose.orientation;
   const auto p=source.pose.position;
   const auto offset=XMVector3Rotate(XMVectorSet(0,-(result.size.height+source.size.height)*.5f-.07f,0,0),
                                     XMVectorSet(q.x,q.y,q.z,q.w));
   keyboardAnchor.position={p.x+XMVectorGetX(offset),p.y+XMVectorGetY(offset),p.z+XMVectorGetZ(offset)};
   loadToolboxPosition(keyboardAnchor.position,3);
   XrSpaceLocation head{XR_TYPE_SPACE_LOCATION};
   auto locate=proc<PFN_xrLocateSpace>("xrLocateSpace");
   if(locate&&XR_SUCCEEDED(locate(space,world,time,&head))&&(head.locationFlags&XR_SPACE_LOCATION_POSITION_VALID_BIT))
    faceToolbox(keyboardAnchor,head.pose.position);
   keyboardAnchored=true;
  }
  result.pose=keyboardAnchor;
  result.layerFlags=XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
  return result;
 }
 static constexpr int BrowserFacets=5;
 int browserSurfaces(const XrCompositionLayerQuad& flat,
     std::array<XrCompositionLayerQuad,BrowserFacets>& surfaces,uint32_t occupied,float curve=-1) const {
  using namespace DirectX;
  surfaces[0]=flat;
  if(curve<0)curve=toolbox.browserCurve;
  if(curve<=0.f||maxLayers<occupied+BrowserFacets)return 1;
  const auto p=flat.pose.position;
  const auto q=flat.pose.orientation;
  const auto basis=XMVectorSet(q.x,q.y,q.z,q.w);
  const auto center=XMVectorSet(p.x,p.y,p.z,0);
  const float angle=XMConvertToRadians(curve);
  const float step=angle/BrowserFacets;
  const float radius=flat.size.width/angle;
  const int totalColumns=flat.subImage.imageRect.extent.width;
  for(int i=0;i<BrowserFacets;++i){
   auto& facet=surfaces[i];
   facet=flat;
   const float a=(i+.5f)*step-angle*.5f;
   const auto local=XMVectorSet(radius*std::cos(step*.5f)*std::sin(a),0,radius*std::cos(step*.5f)*(1.f-std::cos(a)),0);
   const auto position=XMVectorAdd(center,XMVector3Rotate(local,basis));
   const auto rotation=XMQuaternionMultiply(XMQuaternionRotationAxis(XMVectorSet(0,1,0,0),-a),basis);
   XMFLOAT4 orientation;
   XMStoreFloat4(&orientation,rotation);
   facet.pose.position={XMVectorGetX(position),XMVectorGetY(position),XMVectorGetZ(position)};
   facet.pose.orientation={orientation.x,orientation.y,orientation.z,orientation.w};
   facet.size.width=2.f*radius*std::sin(step*.5f);
   facet.subImage.imageRect.offset.x=i*totalColumns/BrowserFacets;
   facet.subImage.imageRect.extent.width=(i+1)*totalColumns/BrowserFacets-facet.subImage.imageRect.offset.x;
  }
  return BrowserFacets;
 }
 bool render(XrCompositionLayerQuad& quad,XrTime time){
  if(!swapchain||!space||!world)return false;
  if(!anchored){
   XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
   constexpr auto tracked=XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT|
    XR_SPACE_LOCATION_POSITION_TRACKED_BIT|XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
   auto located=proc<PFN_xrLocateSpace>("xrLocateSpace")(space,world,time,&location);
   if(XR_FAILED(located)||(location.locationFlags&tracked)!=tracked)return false;
   using namespace DirectX;
   const auto q=location.pose.orientation;
   const auto eye=location.pose.position;
   auto forward=XMVector3Rotate(XMVectorSet(0,0,-1,0),XMVectorSet(q.x,q.y,q.z,q.w));
   auto position=XMVectorMultiplyAdd(forward,XMVectorReplicate(1.5f),XMVectorSet(eye.x,eye.y,eye.z,0));
   anchor=location.pose;
   anchor.position={XMVectorGetX(position),XMVectorGetY(position),XMVectorGetZ(position)};
   if(loadToolboxPosition(anchor.position))panelLog("toolbox position restored");
   faceToolbox(anchor,eye);
   anchored=true;
   panelLog("panel anchored facing tracked headset");
  }
  LARGE_INTEGER start{},end{};
  QueryPerformanceCounter(&start);
  if(!clockFrequency.QuadPart)QueryPerformanceFrequency(&clockFrequency);
  if(!releasedGeneration||!toolbox.state.browser||time>=nextPaint){
   if(toolbox.paint(imageFormat==DXGI_FORMAT_R8G8B8A8_UNORM,workspaceVisible))
    nextPaint=time+40000000;
  }
  const bool gpuAvailable=pollPage();
  const bool useGpu=gpuAvailable&&toolbox.paintedBrowser&&!toolbox.paintedBrowserSettings;
  if(toolbox.paintGeneration!=releasedGeneration||(useGpu&&releasedPage!=latestPage.serial))updateTexture(useGpu);
  QueryPerformanceCounter(&end);
  ++frameCount;
  if(clockFrequency.QuadPart)
   maxFrameWorkUs=std::max(maxFrameWorkUs,static_cast<long>((end.QuadPart-start.QuadPart)*1000000/clockFrequency.QuadPart));
  const auto now=GetTickCount64();
  if(!nextTimingLog)nextTimingLog=now+10000;
  if(now>=nextTimingLog){
   char summary[160]{};
   sprintf_s(summary,"overlay timing frames=%u uploads=%u gpu_pages=%u deferred=%u max_work_us=%ld",
      frameCount,uploadCount,gpuUploads,timedOutImages,maxFrameWorkUs);
   panelLog(summary);
   if(toolbox.settings.directGame){
    uint64_t values[5]{};
    NativeUI::counters(values);
    sprintf_s(summary,"native UI ready=%llu meshes=%llu hits=%llu hover=%llu clicks=%llu",
       values[4],values[0],values[1],values[2],values[3]);
    panelLog(summary);
   }
   frameCount=uploadCount=gpuUploads=timedOutImages=0;
   maxFrameWorkUs=0;
   nextTimingLog=now+10000;
  }
  if(!releasedGeneration)return false;
  quad={XR_TYPE_COMPOSITION_LAYER_QUAD};
  quad.space=world;
  quad.eyeVisibility=XR_EYE_VISIBILITY_BOTH;
  quad.subImage.swapchain=swapchain;
  quad.subImage.imageRect.extent={submittedToolboxWidth,210};
  quad.pose=anchor;
  quad.size={.98f*submittedToolboxWidth/970.f,.98f*210.f/970.f};
  return true;
 }
};

