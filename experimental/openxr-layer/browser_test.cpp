#include "panel.h"
#include "controller.h"
#include <cassert>
static int acquires=0,waits=0;
static XrResult XRAPI_CALL acquire(XrSwapchain,const XrSwapchainImageAcquireInfo*,uint32_t* index){
 ++acquires;*index=0;return XR_SUCCESS;
}
static XrResult XRAPI_CALL waitImage(XrSwapchain,const XrSwapchainImageWaitInfo* info){
 assert(info->timeout==0);++waits;return XR_TIMEOUT_EXPIRED;
}
static XrResult XRAPI_CALL fakeProc(XrInstance,const char* name,PFN_xrVoidFunction* proc){
 if(std::strcmp(name,"xrAcquireSwapchainImage")==0)*proc=reinterpret_cast<PFN_xrVoidFunction>(acquire);
 else if(std::strcmp(name,"xrWaitSwapchainImage")==0)*proc=reinterpret_cast<PFN_xrVoidFunction>(waitImage);
 else return XR_ERROR_FUNCTION_UNSUPPORTED;
 return XR_SUCCESS;
}
static void testSwapchainReuse(){
 TestPanel panel;
 panel.get=fakeProc;
 panel.images.resize(1);
 panel.toolbox.paintGeneration=panel.releasedGeneration=1;
 assert(panel.updateTexture() && acquires==0 && waits==0);
 panel.toolbox.paintGeneration=2;
 assert(!panel.updateTexture());
 assert(panel.releasedGeneration==1 && acquires==1 && waits==1 && panel.imageAcquired);
 assert(!panel.updateTexture());
 assert(panel.releasedGeneration==1 && acquires==1 && waits==2);
}
int main(){
 testSwapchainReuse();
 ToolboxRaster toolbox;
 struct StopBrowser{OpenXRBrowser& browser;~StopBrowser(){browser.stop();}} stop{toolbox.browser};
 wchar_t preferences[MAX_PATH]{};
 if(ToolboxRaster::browserPreferencesPath(preferences))DeleteFileW(preferences);
 toolbox.select(301);
 const auto deadline=GetTickCount64()+20000;
 while(GetTickCount64()<deadline){
  if(!toolbox.paint(true)){toolbox.select(800);return 2;}
  if(!toolbox.browserFrame.empty()){
   auto first=toolbox.browserFrame.front();
   bool varied=false;
   for(auto pixel:toolbox.browserFrame)if(pixel!=first){varied=true;break;}
   if(varied){
    assert(toolbox.browserHit({10,-89})==801);
    assert(toolbox.browserHit({40,-25})==815);
    assert(toolbox.browserHit({620,-25})==816);
    assert(toolbox.browserHit({625,toolbox.pageHeight()+15})==813);
    assert(toolbox.browserHit({16,toolbox.pageHeight()+15})==812);
    toolbox.select(805);
    assert(toolbox.browserSettings && toolbox.paint(true));
    toolbox.setBrowserOpacity(400);
    toolbox.select(811);
    toolbox.select(807);
    assert(toolbox.browserOpacity==.65f && toolbox.browserCurve==5.f && toolbox.browserZoom==1.1f);
    assert(toolbox.paint(true));
    auto pixel=toolbox.pixels[ToolboxRaster::BrowserY*ToolboxRaster::AtlasWidth];
    assert((pixel>>24)==166);
    assert((pixel&255u)==(16u*166u+127u)/255u);
    SharedBrowser::Resize<BrowserResizePoint> resize;
    assert(resize.begin(1,{0,0,-1.5f},{1,0,0},{0,1,0},{0,0,1},.9f,.7f,
                        {0,0,0},{.3f,-.23333f,-1}));
    BrowserResizePoint center{};
    assert(resize.update({0,0,0},{.5f,-.4f,-1},center,toolbox.browserWidth,toolbox.browserHeight));
    assert(toolbox.browserWidth>1.1f && toolbox.browserHeight>.85f);
    toolbox.saveBrowserPreferences();
    assert(toolbox.browserPixelWidth()>640);
    assert(std::abs(toolbox.browserPixelWidth()/toolbox.browserWidth-
                    toolbox.browserPixelHeight()/toolbox.browserHeight)<3.f);
    assert(toolbox.browserHit({10,-89})==801);
    assert(toolbox.browserHit({toolbox.browserPixelWidth()-15,toolbox.pageHeight()+15})==813);
    toolbox.select(805);
    const auto testUrl=std::wstring(Browser::HomeUrl)+L"?address-test=1";
    toolbox.address=testUrl;
    toolbox.select(816);
    const auto resizedDeadline=GetTickCount64()+12000;
    while(GetTickCount64()<resizedDeadline){
     assert(toolbox.paint(true));
     if(toolbox.frameWidth==toolbox.browserPixelWidth()&&toolbox.frameHeight==toolbox.pageHeight()&&
        toolbox.sourceAddress==testUrl)break;
     Sleep(40);
    }
    assert(toolbox.frameWidth==toolbox.browserPixelWidth()&&toolbox.frameHeight==toolbox.pageHeight());
    assert(toolbox.sourceAddress==testUrl);
    ToolboxRaster restored;
    restored.loadBrowserPreferences();
    assert(restored.browserOpacity==.65f && restored.browserCurve==5.f && restored.browserZoom==1.1f);
    assert(std::abs(restored.browserWidth-toolbox.browserWidth)<.001f);
    TestPanel panel;
    panel.maxLayers=16;
    panel.toolbox.browserCurve=60;
    XrCompositionLayerQuad flat{XR_TYPE_COMPOSITION_LAYER_QUAD};
    flat.size={.9f,.6f};
    flat.pose.orientation.w=1;
    flat.pose.position.z=-1.5f;
    flat.subImage.imageRect.extent={OpenXRBrowser::Width,444};
    std::array<XrCompositionLayerQuad,TestPanel::BrowserFacets> facets{};
    assert(panel.browserSurfaces(flat,facets,4)==5);
    assert(facets[0].pose.position.x<0 && facets[4].pose.position.x>0);
    assert(facets[2].pose.position.z==flat.pose.position.z);
    assert(facets[4].subImage.imageRect.offset.x+facets[4].subImage.imageRect.extent.width==OpenXRBrowser::Width);
    assert(panel.browserSurfaces(flat,facets,13)==1);
    saveToolboxPosition({.6f,.5f,-1.4f},2);
    const auto restoredPanel=panel.browserQuad(flat,1);
    assert(std::abs(restoredPanel.pose.position.x-.6f)<.001f);
    flat.pose.position.x=1;
    assert(panel.browserQuad(flat,2).pose.position.x==restoredPanel.pose.position.x);
    toolbox.select(800);
    assert(!toolbox.state.browser);
    toolbox.select(301);
    assert(toolbox.state.browser && toolbox.browser.source()==testUrl);
    toolbox.select(805);
    assert(toolbox.browserSettings);
    toolbox.select(804);
    assert(!toolbox.browserSettings && !toolbox.addressFocused);
    const auto firstTab=toolbox.tabRect(0);
    const auto tabY=ToolboxRaster::TabRowTop+5-OpenXRBrowser::Toolbar;
    assert(toolbox.browserHit({static_cast<LONG>(firstTab.right+10),tabY})==ToolboxRaster::AddTab);
    toolbox.select(ToolboxRaster::AddTab);
    assert(toolbox.browser.count()==2 && toolbox.browser.selected()==1);
    const auto tabDeadline=GetTickCount64()+15000;
    while(GetTickCount64()<tabDeadline){
     assert(toolbox.paint(true));
     if(toolbox.browser.source()==Browser::HomeUrl&&!toolbox.browserFrame.empty())break;
     Sleep(40);
    }
    assert(toolbox.browser.source()==Browser::HomeUrl && !toolbox.browserFrame.empty());
    toolbox.address=std::wstring(Browser::HomeUrl)+L"?second-tab=1";
    const auto secondUrl=toolbox.address;
    toolbox.select(816);
    const auto secondDeadline=GetTickCount64()+8000;
    while(GetTickCount64()<secondDeadline&&toolbox.browser.source()!=secondUrl)Sleep(40);
    assert(toolbox.browser.source()==secondUrl);
    toolbox.select(ToolboxRaster::SelectTab);
    toolbox.select(ToolboxRaster::SelectTab+1);
    assert(toolbox.address==secondUrl);
    toolbox.select(ToolboxRaster::CloseTab+1);
    assert(toolbox.browser.count()==1 && toolbox.browser.selected()==0);
    const auto homeDeadline=GetTickCount64()+8000;
    while(GetTickCount64()<homeDeadline&&toolbox.browser.source()!=Browser::HomeUrl)Sleep(40);
    assert(toolbox.browser.source()==Browser::HomeUrl);
    using namespace SharedKeyboard;
    toolbox.select(815);
    assert(toolbox.state.keyboard && toolbox.addressSelected);
    toolbox.select(First+Shift);
    toolbox.select(First+10);
    toolbox.select(First+41);
    assert(toolbox.address==L"Q." && !toolbox.keyboardShift);
    toolbox.select(First+Backspace);
    assert(toolbox.address==L"Q");
    toolbox.select(Settings);
    assert(toolbox.keyboardSettings);
    assert(SharedKeyboard::hit(900,160,true)==More);
    assert(toolbox.paint(true)&&toolbox.paintedKeyboard);
    toolbox.keyboardOpacity=1.f;
    auto keyboardPixel=[&](int x,int y){return toolbox.pixels[(ToolboxRaster::KeyboardY+y)*ToolboxRaster::AtlasWidth+x];};
    const bool previousTheme=toolbox.settings.amber;
    for(bool amber:{false,true}){
     toolbox.settings.amber=amber;
     for(bool rgba:{false,true}){
      toolbox.dirty=true;
      assert(toolbox.paint(rgba));
      const auto expected=[&](uint32_t rgb){
       return 0xff000000u|(rgba?((rgb&0xff)<<16)|(rgb&0xff00)|((rgb>>16)&0xff):rgb);
      };
      assert(keyboardPixel(500,20)==expected(amber?0x1c1711:0x182738));
      assert(keyboardPixel(500,63)==expected(amber?0x100e0c:0x101b2b));
      assert(keyboardPixel(50,140)==expected(amber?0x302215:0x22374e));
     }
    }
    toolbox.settings.amber=previousTheme;
    toolbox.select(Hide);
    assert(!toolbox.state.keyboard);
    const auto fixture=fs::path(L"home")/(L"select-test-"+std::to_wstring(GetCurrentProcessId())+L".html");
    FILE* fixtureFile=nullptr;
    assert(_wfopen_s(&fixtureFile,fixture.c_str(),L"wb")==0);
    fputs(R"HTML(<!doctype html><style>
    select { position:fixed; left:20px; top:20px; width:160px; height:40px; }
    select::picker(select) { top:60px; left:20px; margin:0; padding:0; border:0; }
    option { height:40px; }
    </style><select onchange="location.href='index.html?mineral='+this.value">
    <option>Gold</option><option>Silver</option></select><button style="position:fixed;left:240px;top:20px;width:120px;height:40px" onclick="document.querySelector('dialog').showModal()">Add</button><dialog><input oninput="if(this.value==='Q.')location.href='index.html?typed=1'"></dialog>)HTML",fixtureFile);
    fclose(fixtureFile);
    struct RemoveFixture{fs::path path;~RemoveFixture(){DeleteFileW(path.c_str());}} cleanup{fixture};
    toolbox.browser.post({OpenXRBrowser::Zoom,{},0,1.0});
    const auto fixtureUrl=L"https://vrkeyboard.example/"+fixture.filename().wstring();
    toolbox.browser.post({OpenXRBrowser::Navigate,{},0,1,fixtureUrl});
    const auto selectDeadline=GetTickCount64()+8000;
    while(GetTickCount64()<selectDeadline&&toolbox.browser.source()!=fixtureUrl)Sleep(40);
    assert(toolbox.browser.source()==fixtureUrl);
    Sleep(500);
    toolbox.browser.post({OpenXRBrowser::Click,{70,40}});
    Sleep(500);
    toolbox.browser.post({OpenXRBrowser::Click,{70,115}});
    const auto choiceUrl=std::wstring(Browser::HomeUrl)+L"?mineral=Silver";
    const auto choiceDeadline=GetTickCount64()+5000;
    while(GetTickCount64()<choiceDeadline&&toolbox.browser.source()!=choiceUrl)Sleep(40);
    assert(toolbox.browser.source()==choiceUrl);
    toolbox.browser.post({OpenXRBrowser::Navigate,{},0,1,fixtureUrl});
    const auto dialogDeadline=GetTickCount64()+8000;
    while(GetTickCount64()<dialogDeadline&&toolbox.browser.source()!=fixtureUrl)Sleep(40);
    assert(toolbox.browser.source()==fixtureUrl);
    Sleep(500);
    toolbox.browser.post({OpenXRBrowser::Click,{285,40}});
    while(GetTickCount64()<dialogDeadline&&!toolbox.state.keyboard){
     assert(toolbox.paint(true));Sleep(40);
    }
    assert(toolbox.state.keyboard && toolbox.inputTarget==ToolboxRaster::InputTarget::Page);
    toolbox.select(First+Shift);
    toolbox.select(First+10);
    toolbox.select(First+41);
    const auto typedUrl=std::wstring(Browser::HomeUrl)+L"?typed=1";
    while(GetTickCount64()<dialogDeadline&&toolbox.browser.source()!=typedUrl)Sleep(40);
    assert(toolbox.browser.source()==typedUrl);
    const auto selection=fs::path(L"home")/(L"selection-test-"+std::to_wstring(GetCurrentProcessId())+L".html");
    assert(_wfopen_s(&fixtureFile,selection.c_str(),L"wb")==0);
    fputs(R"HTML(<!doctype html><style>body{font:20px monospace}</style>
    <p onmouseup="if(getSelection().toString().includes('selection'))location.href='index.html?selection=1'">Drag to verify browser selection works across this line.</p>)HTML",fixtureFile);
    fclose(fixtureFile);
    RemoveFixture removeSelection{selection};
    const auto selectionUrl=L"https://vrkeyboard.example/"+selection.filename().wstring();
    toolbox.browser.post({OpenXRBrowser::Navigate,{},0,1,selectionUrl});
    const auto dragDeadline=GetTickCount64()+8000;
    while(GetTickCount64()<dragDeadline&&toolbox.browser.source()!=selectionUrl)Sleep(40);
    assert(toolbox.browser.source()==selectionUrl);
    Sleep(500);
    toolbox.browser.post({OpenXRBrowser::Down,{12,31}});
    toolbox.browser.post({OpenXRBrowser::Move,{425,31}});
    toolbox.browser.post({OpenXRBrowser::Up,{425,31}});
    const auto selectedUrl=std::wstring(Browser::HomeUrl)+L"?selection=1";
    while(GetTickCount64()<dragDeadline&&toolbox.browser.source()!=selectedUrl)Sleep(40);
    assert(toolbox.browser.source()==selectedUrl);
    toolbox.select(800);
    puts("PASS keyboard address input, web dialog focus and input, browser tabs, resize and swapchain behavior");
    return 0;
   }
  }
  Sleep(40);
 }
 toolbox.select(800);
 puts("Browser did not produce a page frame");
 return 1;
}
