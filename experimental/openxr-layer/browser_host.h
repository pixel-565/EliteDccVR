#pragma once
#include <filesystem>
#include <mutex>

#include <atomic>
#include <deque>
#include <wrl/client.h>
#include "../shared-vr-ui/theme.h"
using Microsoft::WRL::ComPtr;
namespace fs=std::filesystem;
#include "../shared-vr-ui/browser/browser.h"
#include "browser_gpu_transfer.h"

class OpenXRBrowserTab {
public:
 static constexpr int Width=640, Height=400, Tabs=26, Toolbar=100, Footer=30, PanelHeight=Height+Toolbar+Footer;
 enum Command {Move,Click,Down,Up,Copy,Scroll,Back,Forward,Reload,Home,Zoom,Navigate,Insert,Type,Key};
 struct Input {Command kind;POINT point{};int wheel=0;double scale=1;std::wstring url;};

 void configureGraphics(ID3D11Device* device,DXGI_FORMAT output){
  ComPtr<IDXGIDevice> dxgi;
  if(device&&SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&dxgi))))dxgi->GetAdapter(&adapter);
  outputFormat=output;
 }
 void setOpacity(float value){opacity=value;}
 bool latestGpu(BrowserGpuFrame& frame){
  std::unique_lock lock(mutex,std::try_to_lock);
  if(!lock.owns_lock()||frame.serial==gpuFrame.serial)return false;
  frame=gpuFrame;
  return true;
 }
 void post(Input event){
  std::lock_guard lock(mutex);
  if(event.kind==Move && !queue.empty() && queue.back().kind==Move)queue.back()=event;
  else if(queue.size()<128)queue.push_back(event);
 }
 void resize(int width,int height){
  std::lock_guard lock(mutex);
  requestedWidth=width;requestedHeight=height;
 }
 std::wstring source(){
  std::lock_guard lock(mutex);
  return pageSource;
 }
 bool copyFrame(std::vector<uint32_t>& target,uint64_t& generation,int& width,int& height){
  std::unique_lock lock(mutex,std::try_to_lock);
  if(!lock.owns_lock())return false;
  if(generation==serial)return false;
  target=pixels;
  generation=serial;
  width=frameWidth;height=frameHeight;
  return true;
 }
 void setVisible(bool visible){presented=visible;}
 void start(double zoom=1){
  if(worker)return;
  initialZoom=zoom;
  stopRequested=false;
  worker=CreateThread(nullptr,0,[](void* self)->DWORD{
   static_cast<OpenXRBrowserTab*>(self)->run();
   return 0;
  },this,0,nullptr);
  if(!worker)panelLog("browser worker startup",GetLastError());
 }
 bool textFocused()const{return focused.load();}
 uint64_t textFocusEvent()const{return focusEvent.load();}
 void requestStop(){stopRequested=true;}
 bool stopped() const{return !worker||WaitForSingleObject(worker,0)==WAIT_OBJECT_0;}
 std::wstring title(){std::lock_guard lock(mutex);return pageTitle;}
 void stop(){
  stopRequested=true;
  if(worker){WaitForSingleObject(worker,INFINITE);CloseHandle(worker);worker=nullptr;}
  std::lock_guard lock(mutex);
  queue.clear();
  pixels.clear();
  pageSource.clear();
  gpuFrame={};gpuFrame.serial=++gpuSerial;
  ++serial;
 }
private:
 HANDLE worker{};
 double initialZoom=1;
 std::atomic<bool> stopRequested=false,presented=true;
 std::mutex mutex;
 std::deque<Input> queue;
 std::vector<uint32_t> pixels;
 uint64_t serial=0;
 int requestedWidth=Width,requestedHeight=Height,frameWidth=0,frameHeight=0;
 std::atomic<bool> focused=false;
 inline static std::atomic<uint64_t> nextFocusEvent=0;
 std::atomic<uint64_t> focusEvent=0;
 std::wstring pageSource,pageTitle=L"New tab";
 ComPtr<IDXGIAdapter> adapter;
 DXGI_FORMAT outputFormat=DXGI_FORMAT_UNKNOWN;
 std::atomic<float> opacity=1.f;
 BrowserGpuFrame gpuFrame;
 inline static std::atomic<uint64_t> gpuSerial=0;
 void run(){
  const auto initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
  if(FAILED(initialized)){panelLog("browser COM startup",initialized);return;}
  try {
   wchar_t local[MAX_PATH]{},modulePath[MAX_PATH]{};
   HMODULE module{};
   GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
       reinterpret_cast<LPCWSTR>(&panelLog),&module);
   const auto size=GetEnvironmentVariableW(L"LOCALAPPDATA",local,MAX_PATH);
   const auto moduleSize=GetModuleFileNameW(module,modulePath,MAX_PATH);
   if(!size||size>=MAX_PATH||!moduleSize||moduleSize>=MAX_PATH)throw std::runtime_error("Browser paths unavailable");
   auto profile=fs::path(local)/L"EliteDccVR/OpenXRBrowser";
   fs::create_directories(profile);
   BrowserGpuTransfer transfer;
   const bool gpuReady=adapter&&transfer.initialize(adapter.Get(),outputFormat);
   if(adapter)panelLog(gpuReady?"browser GPU transfer ready":"browser GPU transfer unavailable");
   Browser browser;
   browser.useInPageSelect=true;
   if(gpuReady)browser.graphicsDevice=transfer.device;
   bool haveGpuFrame=false;
   float lastOpacity=-1;
   auto publishGpu=[&](ID3D11Texture2D* texture){
    BrowserGpuFrame frame;
    const auto alpha=opacity.load();
    if(transfer.render(texture,browser.viewportWidth,browser.viewportHeight,alpha,frame)){
     std::lock_guard lock(mutex);
     const bool first=!gpuFrame.handle;
     frame.serial=++gpuSerial;
     gpuFrame=std::move(frame);
     lastOpacity=alpha;
     haveGpuFrame=true;
     if(first)panelLog("first WebView2 GPU frame");
    }
   };
   browser.onTexture=[&](ID3D11Texture2D* texture){
    publishGpu(texture);
   };
   browser.resize(Width,Height);
   browser.setPageZoom(initialZoom);
   browser.onDiagnostic=[](const std::string& text){panelLog(text.c_str());};
   browser.onTextFocus=[this,&browser]{
    {
     focused=true;
     focusEvent=++nextFocusEvent;
    }
   };
   browser.onTextBlur=[this]{focused=false;};
   browser.onNavigation=[this]{focused=false;};
   browser.onTitle=[this,&browser]{std::lock_guard lock(mutex);pageTitle=browser.title;};
   browser.onSource=[this,&browser]{
    LPWSTR address=nullptr;
    if(browser.web&&SUCCEEDED(browser.web->get_Source(&address))&&address){
     std::lock_guard lock(mutex);
     pageSource=address;
     CoTaskMemFree(address);
    }
   };
   browser.onFrame=[this,&browser,&haveGpuFrame](const std::vector<unsigned char>& frame){
    const int width=browser.viewportWidth,height=browser.viewportHeight;
    haveGpuFrame=false;
    if(frame.size()!=static_cast<size_t>(width)*height*4)return;
    std::lock_guard lock(mutex);
    if(gpuFrame.handle){gpuFrame={};gpuFrame.serial=++gpuSerial;}
    if(frameWidth==width&&frameHeight==height&&pixels.size()==frame.size()/4 && memcmp(pixels.data(),frame.data(),frame.size())==0)return;
    pixels.resize(frame.size()/4);
    frameWidth=width;frameHeight=height;
    memcpy(pixels.data(),frame.data(),frame.size());
    ++serial;
    if(serial==1)panelLog("first WebView2 frame");
   };
   browser.start(profile);
   browser.homeFolder=fs::path(modulePath).parent_path()/L"home";
   bool mouseHeld=false;POINT lastMouse{};
   MSG message{};
   while(!stopRequested){
    while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){
     TranslateMessage(&message);DispatchMessageW(&message);
    }
    std::deque<Input> events;
    int width,height;
    {std::lock_guard lock(mutex);events.swap(queue);width=requestedWidth;height=requestedHeight;}
    if(width!=browser.viewportWidth||height!=browser.viewportHeight)haveGpuFrame=false;
    browser.resize(width,height);
    const bool visible=presented.load();
    if(!visible&&mouseHeld){
     browser.mouse(COREWEBVIEW2_MOUSE_EVENT_KIND_LEFT_BUTTON_UP,lastMouse);
     mouseHeld=false;
    }
    browser.setPresented(visible);
    if(browser.web)for(auto& event:events){
     switch(event.kind){
     case Move:
      lastMouse=event.point;
      browser.mouse(COREWEBVIEW2_MOUSE_EVENT_KIND_MOVE,event.point,mouseHeld);
      break;
     case Down:
      if(visible&&!mouseHeld){
       lastMouse=event.point;
       browser.mouse(COREWEBVIEW2_MOUSE_EVENT_KIND_LEFT_BUTTON_DOWN,event.point,true);
       mouseHeld=true;
      }
      break;
     case Up:
      if(mouseHeld)browser.mouse(COREWEBVIEW2_MOUSE_EVENT_KIND_LEFT_BUTTON_UP,event.point);
      mouseHeld=false;
      break;
     case Copy:browser.copySelection();break;
     case Click:
      browser.mouse(COREWEBVIEW2_MOUSE_EVENT_KIND_LEFT_BUTTON_DOWN,event.point,true);
      browser.mouse(COREWEBVIEW2_MOUSE_EVENT_KIND_LEFT_BUTTON_UP,event.point);break;
     case Scroll:browser.mouse(COREWEBVIEW2_MOUSE_EVENT_KIND_WHEEL,event.point,false,event.wheel);break;
     case Back:browser.web->GoBack();break;
     case Forward:browser.web->GoForward();break;
     case Reload:browser.web->Reload();break;
     case Home:browser.home();break;
     case Zoom:browser.setPageZoom(event.scale);break;
     case Type:if(focused)browser.insert(event.url);break;
     case Key:if(focused)browser.key(event.wheel!=0);break;
     case Insert:if(focused)browser.insert(event.url);break;
     case Navigate:if(Browser::allowedUrl(event.url))browser.web->Navigate(event.url.c_str());break;
     }
    }
    browser.capture(GetTickCount64(),80);
    if(haveGpuFrame&&presented&&lastOpacity!=opacity.load())publishGpu(nullptr);
    MsgWaitForMultipleObjects(0,nullptr,FALSE,10,QS_ALLINPUT);
   }
  }catch(const std::exception& error){panelLog(error.what());}
  CoUninitialize();
 }
};

class OpenXRBrowser {
 using Tab=OpenXRBrowserTab;
 std::vector<std::unique_ptr<Tab>> tabs;
 std::vector<std::unique_ptr<Tab>> closing;
 size_t active=0;
 ID3D11Device* device=nullptr;
 DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
 int width=Tab::Width,height=Tab::Height;
 float opacity=1.f;
 bool visible=true;
 Tab& current(){return *tabs[active];}
public:
 static constexpr int Width=Tab::Width,Height=Tab::Height,Tabs=Tab::Tabs,Toolbar=Tab::Toolbar,
                      Footer=Tab::Footer,PanelHeight=Tab::PanelHeight;
 static constexpr size_t MaxTabs=8;
 using Input=Tab::Input;
 using Command=Tab::Command;
 using enum Tab::Command;
 OpenXRBrowser(){tabs.push_back(std::make_unique<Tab>());}
 void configureGraphics(ID3D11Device* value,DXGI_FORMAT output){device=value;format=output;current().configureGraphics(device,format);}
 void start(double zoom=1){current().start(zoom);}
 void stop(){
  for(auto& tab:tabs)tab->requestStop();
  for(auto& tab:tabs)tab->stop();
  for(auto& tab:closing)tab->stop();
  closing.clear();
  tabs.clear();
  tabs.push_back(std::make_unique<Tab>());
  active=0;
 }
 void post(Input input){current().post(std::move(input));}
 void setOpacity(float value){opacity=value;current().setOpacity(value);}
 void setVisible(bool value){visible=value;current().setVisible(value);}
 void resize(int w,int h){
  width=w;height=h;current().resize(w,h);
  std::erase_if(closing,[](auto& tab){if(!tab->stopped())return false;tab->stop();return true;});
 }
 std::wstring source(){return current().source();}
 bool textFocused(){return current().textFocused();}
 uint64_t textFocusEvent(){return current().textFocusEvent();}
 std::wstring title(size_t index){return tabs[index]->title();}
 size_t count()const{return tabs.size();}
 size_t selected()const{return active;}
 bool copyFrame(std::vector<uint32_t>& frame,uint64_t& generation,int& w,int& h){
  return current().copyFrame(frame,generation,w,h);
 }
 bool latestGpu(BrowserGpuFrame& frame){return current().latestGpu(frame);}
 bool activate(size_t index){
  if(index>=tabs.size()||index==active)return false;
  current().setVisible(false);
  active=index;
  current().resize(width,height);
  current().setOpacity(opacity);
  current().setVisible(visible);
  return true;
 }
 bool add(double zoom){
  if(tabs.size()>=MaxTabs)return false;
  auto tab=std::make_unique<Tab>();
  tab->configureGraphics(device,format);
  tabs.push_back(std::move(tab));
  activate(tabs.size()-1);
  start(zoom);
  return true;
 }
 bool close(size_t index){
  if(index>=tabs.size()||tabs.size()==1)return false;
  const bool wasActive=index==active;
  if(wasActive)activate(index?index-1:1);
  tabs[index]->requestStop();
  closing.push_back(std::move(tabs[index]));
  tabs.erase(tabs.begin()+index);
  if(index<active)--active;
  return wasActive;
 }
};
