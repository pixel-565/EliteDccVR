#include "panel.h"
#include <cassert>
int main(){
 ComPtr<ID3D11Device> device;
 ComPtr<ID3D11DeviceContext> context;
 if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,
    nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)))return 1;
 OpenXRBrowser browser;
 struct Close {OpenXRBrowser& b;~Close(){b.stop();}} close{browser};
 browser.configureGraphics(device.Get(),DXGI_FORMAT_R8G8B8A8_UNORM);
 browser.setOpacity(.6f);
 browser.start();
 BrowserGpuFrame frame;
 const auto deadline=GetTickCount64()+15000;
 while(GetTickCount64()<deadline && !frame.handle){
  browser.latestGpu(frame);
  Sleep(30);
 }
 if(!frame.handle){puts("No GPU frame");return 2;}
 const auto initial=frame.serial;
 const auto rendered=GetTickCount64()+10000;
 while(GetTickCount64()<rendered){
  browser.latestGpu(frame);
  if(frame.handle&&frame.serial>initial&&browser.source()==Browser::HomeUrl)break;
  Sleep(30);
 }
 ComPtr<ID3D11Texture2D> received,readback;
 assert(SUCCEEDED(device->OpenSharedResource(frame.handle,IID_PPV_ARGS(&received))));
 ComPtr<IDXGIKeyedMutex> gate;
 assert(SUCCEEDED(received.As(&gate)));
 D3D11_TEXTURE2D_DESC desc{};
 received->GetDesc(&desc);
 desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=desc.MiscFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
 assert(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&readback)));
 assert(gate->AcquireSync(0,1000)==S_OK);
 context->CopyResource(readback.Get(),received.Get());
 context->Flush();
 gate->ReleaseSync(0);
 D3D11_MAPPED_SUBRESOURCE data{};
 assert(SUCCEEDED(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&data)));
 auto row=static_cast<const uint32_t*>(data.pData);
 assert((row[0]>>24)==153);
 bool varied=false;
 for(UINT y=0;y<desc.Height&&!varied;++y){
  auto current=reinterpret_cast<const uint32_t*>(static_cast<const unsigned char*>(data.pData)+y*data.RowPitch);
  for(UINT x=0;x<desc.Width;++x)if(current[x]!=row[0]){varied=true;break;}
 }
 context->Unmap(readback.Get(),0);
 assert(varied);
 const auto firstSerial=frame.serial;
 assert(browser.add(1));
 const auto switchDeadline=GetTickCount64()+15000;
 do {
  browser.latestGpu(frame);
  if(frame.handle&&frame.serial>firstSerial)break;
  Sleep(30);
 }while(GetTickCount64()<switchDeadline);
 assert(frame.handle&&frame.serial>firstSerial);
 assert(browser.activate(0));
 assert(browser.latestGpu(frame) && frame.handle);
 puts("PASS WebView2 GPU capture, cross-device texture copy, and premultiplied alpha");
}
