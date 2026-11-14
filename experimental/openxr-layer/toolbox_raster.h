#pragma once
#include "../shared-vr-ui/shared_toolbox.h"
#include "../shared-vr-ui/settings_panel.h"
#include "../shared-vr-ui/game-native/movement.h"
#include <vector>
#include <cstdint>
#include "browser_host.h"
#include "../shared-vr-ui/browser/preferences.h"
#include "../shared-vr-ui/browser/settings_ui.h"
#include "../shared-vr-ui/browser/chrome.h"
#include "keyboard.h"
#include "../shared-vr-ui/game-native/native_ui.h"
struct ToolboxRaster {
 ToolboxRaster(){settings.nativeControls=true;}
 SharedToolbox::State state{};
 OverlaySettings settings;
 static constexpr int AtlasWidth=1800, SettingsY=210, KeyboardY=1110, BrowserY=KeyboardY+SharedKeyboard::Height, DotPixels=16, AtlasHeight=BrowserY+1440+DotPixels+1, DotY=AtlasHeight-DotPixels-1;
 bool collapsed{};
 OpenXRBrowser browser;
 bool browserSettings=false;
 bool keyboardSettings=false,keyboardShift=false,keySnap=true,addressSelected=false;
 float keyboardCurve=0,keyboardOpacity=1;
 uint64_t handledFocusEvent=0;
 enum class InputTarget {Elite,Address,Page};
 InputTarget inputTarget=InputTarget::Elite;
 void keyboardPreferences(bool save){
  wchar_t path[MAX_PATH]{},dir[MAX_PATH]{};
  const auto size=GetEnvironmentVariableW(L"LOCALAPPDATA",dir,MAX_PATH);
  if(!size||size>=MAX_PATH||swprintf_s(path,L"%ls\\EliteDccVR-OpenXR-Keyboard.settings",dir)<0)return;
  FILE* file=nullptr;
  if(_wfopen_s(&file,path,save?L"w":L"r")!=0)return;
  if(save)fprintf(file,"%g %g %d\n",keyboardCurve,keyboardOpacity,keySnap?1:0);
  else{
   float curve=0,opacity=1;int snap=1;
   if(fscanf_s(file,"%f %f %d",&curve,&opacity,&snap)==3&&std::isfinite(curve)&&std::isfinite(opacity)){
    keyboardCurve=std::clamp(curve,0.f,120.f);keyboardOpacity=std::clamp(opacity,.3f,1.f);keySnap=snap!=0;
   }
  }
  fclose(file);
 }
 void openKeyboard(InputTarget next){
  if(!state.keyboard)keyboardPreferences(false);
  state.keyboard=true;keyboardSettings=false;inputTarget=next;dirty=true;
 }
 void setKeyboardOpacity(float x){
  const auto box=SharedKeyboard::settings[4];
  const float next=.3f+.7f*std::clamp((x-box.left)/(box.right-box.left),0.f,1.f);
  if(next==keyboardOpacity)return;
  keyboardOpacity=next;dirty=true;
 }
 void keyboardInput(int key){
  using namespace SharedKeyboard;
  if(key==Shift){keyboardShift=!keyboardShift;dirty=true;return;}
  const wchar_t ch=character(key,keyboardShift);
  if(key<37)keyboardShift=false;
  if(state.browser&&inputTarget==InputTarget::Address){
   if(key==Enter){select(816);return;}
   if(addressSelected){address.clear();addressSelected=false;}
   if(key==Backspace){if(!address.empty())address.pop_back();}
   else if(key==Paste)address+=clipboardText();
   else if(ch)address+=ch;
   if(address.size()>8192)address.resize(8192);
  }else if(state.browser&&inputTarget==InputTarget::Page){
   if(key==Enter||key==Backspace)browser.post({OpenXRBrowser::Key,{},key==Enter});
   else if(key==Paste)browser.post({OpenXRBrowser::Type,{},0,1,clipboardText()});
   else if(ch)browser.post({OpenXRBrowser::Type,{},0,1,std::wstring(1,ch)});
  }else eliteKey(key,ch);
  dirty=true;
 }
 float browserOpacity=1.f,browserCurve=0.f,browserZoom=1.f,browserWidth=.9f,browserHeight=.7f;
 int resizeCorner=0;
 int frameWidth=0,frameHeight=0;
 bool addressFocused=false;
 uint64_t addressFocusEvent=0;
 std::wstring address,sourceAddress;
 static constexpr float BrowserPixelsPerMeter=640.f/.9f;
 static constexpr float BrowserChromeScale=640.f/1200.f;
 int browserPixelWidth()const{return std::clamp(static_cast<int>(std::lround(browserWidth*BrowserPixelsPerMeter)),640,AtlasWidth);}
 int browserPixelHeight()const{return std::clamp(static_cast<int>(std::lround(browserHeight*BrowserPixelsPerMeter)),498,DotY-BrowserY);}
 int pageHeight()const{return browserPixelHeight()-OpenXRBrowser::Toolbar-OpenXRBrowser::Footer;}
 float chromeWidth()const{return browserPixelWidth()/BrowserChromeScale;}
 static std::wstring clipboardText(){
  std::wstring value;
  if(!OpenClipboard(nullptr))return value;
  if(auto handle=GetClipboardData(CF_UNICODETEXT)){
   if(auto text=static_cast<const wchar_t*>(GlobalLock(handle))){
    value.assign(text,wcsnlen_s(text,8192));
    GlobalUnlock(handle);
   }
  }
  CloseClipboard();
  return value;
 }
 static bool browserPreferencesPath(wchar_t (&path)[MAX_PATH]){
  wchar_t directory[MAX_PATH]{};
  auto size=GetEnvironmentVariableW(L"LOCALAPPDATA",directory,MAX_PATH);
  return size>0&&size<MAX_PATH&&swprintf_s(path,L"%ls\\EliteDccVR-OpenXR-Browser.settings",directory)>0;
 }
 void loadBrowserPreferences(){
  wchar_t path[MAX_PATH]{};
  FILE* file=nullptr;
  if(!browserPreferencesPath(path)||_wfopen_s(&file,path,L"r")!=0)return;
  float alpha=1,curve=0,zoom=1,width=.9f,height=.7f;
  const int count=fscanf_s(file,"%f %f %f %f %f",&alpha,&curve,&zoom,&width,&height);
  fclose(file);
  if(count<3||!std::isfinite(alpha)||!std::isfinite(curve)||!std::isfinite(zoom))return;
  browserOpacity=std::clamp(alpha,.3f,1.f);
  browserCurve=std::clamp(curve,0.f,120.f);
  browserZoom=std::clamp(zoom,.5f,2.f);
  if(count==5&&std::isfinite(width)&&std::isfinite(height)){browserWidth=std::clamp(width,.9f,2.5f);browserHeight=std::clamp(height,std::max(.7f,browserWidth*.52f),2.f);}
 }
 void saveBrowserPreferences() const {
  wchar_t path[MAX_PATH]{},temporary[MAX_PATH]{};
  FILE* file=nullptr;
  if(!browserPreferencesPath(path)||swprintf_s(temporary,L"%ls.%lu.tmp",path,GetCurrentProcessId())<0||
     _wfopen_s(&file,temporary,L"w")!=0)return;
  const bool written=fprintf(file,"%.4f %.4f %.4f %.4f %.4f\n",browserOpacity,browserCurve,browserZoom,browserWidth,browserHeight)>0;
  const bool closed=fclose(file)==0;
  if(!written||!closed||!MoveFileExW(temporary,path,MOVEFILE_REPLACE_EXISTING))DeleteFileW(temporary);
 }
 void setBrowserOpacity(float x){
  const auto rect=SharedBrowserSettings::opacityRect();
  const auto scale=OpenXRBrowser::Width/SharedBrowserSettings::Width;
  const float next=BrowserPreferences::opacity((x/scale-rect.left)/(rect.right-rect.left));
  if(next==browserOpacity)return;
  browserOpacity=next;
  browser.setOpacity(next);
  dirty=true;
  saveBrowserPreferences();
 }
 static constexpr int AddTab=900, SelectTab=910, CloseTab=930, TabRowTop=35;
 std::vector<std::wstring> tabTitles;
 D2D1_RECT_F tabRect(size_t index)const{
  const float w=std::min(180.f,(browserPixelWidth()-38.f)/browser.count());
  const float left=4.f+index*w;
  return {left,TabRowTop+2.f,left+w-3.f,TabRowTop+OpenXRBrowser::Tabs-2.f};
 }
 void refreshBrowserTab(){
  browserFrame.clear();
  frameWidth=frameHeight=0;
  browserGeneration=0;
  address=sourceAddress=browser.source();
  browser.post({OpenXRBrowser::Zoom,{},0,browserZoom});
  addressFocused=false;
  browserSettings=false;
  browser.setVisible(true);
 }
 int browserHit(POINT point) const {
  const int row=point.y+OpenXRBrowser::Toolbar;
  if(row>=TabRowTop&&row<TabRowTop+OpenXRBrowser::Tabs){
   for(size_t index=0;index<browser.count();++index){
    const auto box=tabRect(index);
    if(point.x>=box.left&&point.x<box.right)
     return (browser.count()>1&&point.x>=box.right-20?CloseTab:SelectTab)+static_cast<int>(index);
   }
   const auto last=tabRect(browser.count()-1);
   if(browser.count()<OpenXRBrowser::MaxTabs&&point.x>=last.right+3&&point.x<last.right+30)return AddTab;
   return 814;
  }
  const float chromeScale=BrowserChromeScale;
  const float x=point.x/chromeScale,y=(point.y+OpenXRBrowser::Toolbar)/chromeScale;
  if(point.y<0){
   const int controls[]={400,401,402,408,412,415,416,405};
   const int actions[]={801,802,803,804,805,806,807,800};
   for(int i=0;i<8;++i)
    if(BrowserChrome::inside(BrowserChrome::controlRect(controls[i],chromeWidth()),x,y))return actions[i];
   if(BrowserChrome::inside(BrowserChrome::controlRect(406,chromeWidth()),x,y))return 818;
   if(BrowserChrome::inside(BrowserChrome::controlRect(403,chromeWidth()),x,y))return 815;
   if(BrowserChrome::inside(BrowserChrome::controlRect(404,chromeWidth()),x,y))return 816;
   if(BrowserChrome::inside(BrowserChrome::controlRect(417,chromeWidth()),x,y))return 819;
   if(BrowserChrome::inside(BrowserChrome::controlRect(418,chromeWidth()),x,y))return 817;
   return 814;
  }
  if(point.y>=pageHeight()){
   for(int side:{-1,1})
    if(BrowserChrome::inside(BrowserChrome::resizeRect(side,browserPixelHeight()/chromeScale,chromeWidth()),x,y))
     return side<0?812:813;
   return 814;
  }
  if(!browserSettings)return -1;
  const auto scale=OpenXRBrowser::Width/SharedBrowserSettings::Width;
  switch(SharedBrowserSettings::hit(point.x/scale,point.y/scale)){
  case 480:return 808;
  case 488:return 809;
  case 489:return 810;
  case 491:return 811;
  }
  return -1;
 }
 std::vector<uint32_t> browserFrame;
 uint64_t browserGeneration=0, paintGeneration=0;
 bool paintedKeyboard=false;
 bool paintedBrowser=false,paintedSettings=false,paintedBrowserSettings=false;
 int paintedWidth=0,paintedHeight=0,paintedToolboxWidth=0;
 int hover=-1,pressed=-1;
 bool dirty=true;
 std::vector<uint32_t> pixels;
 int width() const {return collapsed?SharedToolbox::ToolH:SharedToolbox::ToolW;}
 int height() const {return 210;}
 int hit(float x,float y,bool settingsSurface=false) const {return settingsSurface?settings.hit(x,y):SharedToolbox::hit(x,y,collapsed);}
 void pasteBrowserText(std::wstring value){
   if(value.empty())return;
   if(!browserSettings && browser.textFocused() &&
      (!addressFocused||browser.textFocusEvent()!=addressFocusEvent)){
    addressFocused=false;
    browser.post({OpenXRBrowser::Insert,{},0,1,std::move(value)});
   }else{
    address=std::move(value);
    addressFocusEvent=browser.textFocusEvent();
    addressFocused=true;
   }
   dirty=true;
 }
 void select(int button) {
  if(button>=SharedKeyboard::First&&button<SharedKeyboard::Keys){keyboardInput(button-SharedKeyboard::First);return;}
  if(button>=SharedKeyboard::Keys&&button<=SharedKeyboard::Opacity){
   using namespace SharedKeyboard;
   switch(button){
   case Keys:keyboardSettings=false;break;
   case Settings:keyboardSettings=true;break;
   case Hide:state.keyboard=false;break;
   case Less:keyboardCurve=std::max(0.f,keyboardCurve-5);break;
   case Flat:keyboardCurve=0;break;
   case More:keyboardCurve=std::min(120.f,keyboardCurve+5);break;
   case Snap:keySnap=!keySnap;break;
   }
   if(button>=Less&&button<=Snap)keyboardPreferences(true);
   dirty=true;return;
  }
  if(button==AddTab){
   if(browser.add(browserZoom)){refreshBrowserTab();dirty=true;}
   return;
  }
  if(button>=SelectTab&&button<SelectTab+static_cast<int>(OpenXRBrowser::MaxTabs)){
   if(browser.activate(button-SelectTab)||browserSettings){refreshBrowserTab();dirty=true;}
   return;
  }
  if(button>=CloseTab&&button<CloseTab+static_cast<int>(OpenXRBrowser::MaxTabs)){
   if(browser.close(button-CloseTab))refreshBrowserTab();
   dirty=true;
   return;
  }
  if(button==815){addressFocusEvent=browser.textFocusEvent();addressFocused=addressSelected=true;openKeyboard(InputTarget::Address);return;}
  if(button==818){addressFocused=false;openKeyboard(InputTarget::Page);return;}
  if(button==819){
   if(!browserSettings&&!addressFocused)browser.post({OpenXRBrowser::Copy});
   return;
  }
  if(button==817){pasteBrowserText(clipboardText());return;}
  if(button==816){
   auto value=address;
   const auto begin=value.find_first_not_of(L" \t\r\n"),end=value.find_last_not_of(L" \t\r\n");
   if(begin!=std::wstring::npos){
    value=value.substr(begin,end-begin+1);
    if(value.find(L"://")==std::wstring::npos)value=L"https://"+value;
    if(Browser::allowedUrl(value))browser.post({OpenXRBrowser::Navigate,{},0,1,std::move(value)});
   }
   addressFocused=false;inputTarget=InputTarget::Page;dirty=true;return;
  }
  if(button==800){state.browser=false;browser.setVisible(false);browserSettings=false;dirty=true;return;}
  if(button==805){browserSettings=!browserSettings;browser.setVisible(!browserSettings);dirty=true;return;}
  if(button==806||button==807){
   browserZoom=BrowserPreferences::zoom(browserZoom,button==806?-1:1);
   browser.post({OpenXRBrowser::Zoom,{},0,browserZoom});
   saveBrowserPreferences();dirty=true;return;
  }
  if(button>=809&&button<=811){
   browserCurve=BrowserPreferences::curve(browserCurve,button-810);
   saveBrowserPreferences();dirty=true;return;
  }
  if(button>=801&&button<=804){
   const OpenXRBrowser::Command commands[]={OpenXRBrowser::Back,OpenXRBrowser::Forward,OpenXRBrowser::Reload,OpenXRBrowser::Home};
   if(button==804){
    browserSettings=false;
    browser.setVisible(true);
    addressFocused=false;
    address=sourceAddress;
    dirty=true;
   }
   browser.post({commands[button-801]});return;
  }
  if(button>=600){if(settings.select(button)){dirty=true;if(button==601){const bool ok=NativeUI::enable(settings.directGame);panelLog(ok?"native UI integration changed":"native UI unsupported build");if(!ok)settings.directGame=false;}if(button==618){const bool ok=OnFootMovement::configure(settings.onFootMovement);if(!ok)settings.onFootMovement=false;settings.movementReady=OnFootMovement::ready();panelLog(ok?"on-foot movement changed":"analog movement unavailable");}if(button==619){const bool ok=OnFootMovement::configureInteraction(settings.triggerInteract);if(!ok)settings.triggerInteract=false;panelLog(ok?"trigger interaction changed":"trigger interaction unavailable");}if(button>=630||button==601||button==618||button==619)settings.save();}return;}
  state.gameInterface=settings.visible;
  if(!SharedToolbox::activate(button,state,collapsed))return;
  if(button==300&&state.keyboard){state.keyboard=false;openKeyboard(state.browser?(addressFocused?InputTarget::Address:InputTarget::Page):InputTarget::Elite);}
  if(button==301){
   browser.setVisible(state.browser);
   if(state.browser){loadBrowserPreferences();browser.setOpacity(browserOpacity);browser.start(browserZoom);}
   else browserSettings=false;
  }
  if(button==302){
   settings.visible=state.gameInterface;
   if(settings.visible)settings.tab=2;
  }
  dirty=true;
  panelLog("toolbox button",button);
 }
 bool paint(bool rgba,bool autoKeyboard=true) {
  if(state.browser){
   const auto focus=browser.textFocusEvent();
   if(autoKeyboard&&!browserSettings&&focus!=handledFocusEvent&&browser.textFocused()&&(!addressFocused||focus!=addressFocusEvent)){
    handledFocusEvent=focus;
    openKeyboard(InputTarget::Page);
   }
   if(addressFocused&&browser.textFocused()&&browser.textFocusEvent()!=addressFocusEvent){
    addressFocused=false;
    address=sourceAddress;
    dirty=true;
   }
   browser.resize(browserPixelWidth(),pageHeight());
   if(tabTitles.size()!=browser.count()){tabTitles.resize(browser.count());dirty=true;}
   for(size_t index=0;index<browser.count();++index){
    auto title=browser.title(index);
    if(title!=tabTitles[index]){tabTitles[index]=std::move(title);dirty=true;}
   }
   if(browser.copyFrame(browserFrame,browserGeneration,frameWidth,frameHeight))dirty=true;
   auto current=browser.source();
   if(current!=sourceAddress){
    sourceAddress=std::move(current);
    if(!addressFocused)address=sourceAddress;
    dirty=true;
   }
  }
  if(!dirty)return !pixels.empty();
  using Microsoft::WRL::ComPtr;
  ComPtr<ID2D1Factory> factory;
  ComPtr<ID2D1DCRenderTarget> target;
  ComPtr<IDWriteFactory> text;
  ComPtr<IDWriteTextFormat> font;
  ComPtr<ID2D1SolidColorBrush> brush;
  if(FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,factory.GetAddressOf())) ||
     FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(text.GetAddressOf()))) ||
     FAILED(text->CreateTextFormat(L"Segoe UI",nullptr,DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,26,L"en-us",&font)))return false;
  font->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
  auto properties=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
      D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_IGNORE),96,96);
  if(FAILED(factory->CreateDCRenderTarget(&properties,&target)))return false;
  BITMAPINFO bitmap{};
  bitmap.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
  bitmap.bmiHeader.biWidth=AtlasWidth;
  bitmap.bmiHeader.biHeight=-AtlasHeight;
  bitmap.bmiHeader.biPlanes=1;
  bitmap.bmiHeader.biBitCount=32;
  auto dc=CreateCompatibleDC(nullptr);
  if(!dc)return false;
  void* data=nullptr;
  auto dib=CreateDIBSection(dc,&bitmap,DIB_RGB_COLORS,&data,nullptr,0);
  if(!dib){DeleteDC(dc);return false;}
  auto old=SelectObject(dc,dib);
  RECT toolboxRect{0,0,AtlasWidth,height()};
  auto result=target->BindDC(dc,&toolboxRect);
  if(SUCCEEDED(result))result=target->CreateSolidColorBrush(D2D1::ColorF(0xffffff),&brush);
  ComPtr<ID2D1RenderTarget> base=target;
  eliteTheme=settings.amber;
  if(SUCCEEDED(result)){
   auto toolboxState=state;
   toolboxState.gameInterface=settings.visible;
   result=SharedToolbox::draw(base,brush,text.Get(),font.Get(),collapsed,toolboxState,
      hover,pressed,-1,[](unsigned rgb){return ThemeColor(rgb);});
  }
  if(SUCCEEDED(result)&&settings.visible){
   RECT settingsRect{0,SettingsY,AtlasWidth,SettingsY+900};
   result=target->BindDC(dc,&settingsRect);
  }
  if(SUCCEEDED(result)&&settings.visible){
   target->BeginDraw();
   target->Clear(ThemeColor(0x101b2b));
   brush->SetColor(ThemeColor(0x182738));
   target->FillRectangle({0,0,1200,65},brush.Get());
   font->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
   auto label=[&](const std::wstring& value,D2D1_RECT_F bounds){
    brush->SetColor(ThemeColor(0xeaf1ff));
    target->DrawText(value.c_str(),static_cast<UINT32>(value.size()),font.Get(),bounds,brush.Get(),D2D1_DRAW_TEXT_OPTIONS_CLIP);
   };
   auto button=[&](int id,const wchar_t* value,bool selected=false){
    auto bounds=giRect(id);
    brush->SetColor(ThemeColor(hover==id?0x396385:selected?0x375c7c:0x22374e));
    target->FillRoundedRectangle(D2D1::RoundedRect(bounds,7,7),brush.Get());
    bounds.left+=12;bounds.top+=12;
    label(value,bounds);
   };
   label(L"Settings",{40,10,950,55});
   settings.content(button,label);
   const auto close=giRect(600);
   const float x=(close.left+close.right)*.5f,y=(close.top+close.bottom)*.5f;
   brush->SetColor(ThemeColor(0xeaf1ff));
   target->DrawLine({x-8,y-8},{x+8,y+8},brush.Get(),2.5f);
   target->DrawLine({x-8,y+8},{x+8,y-8},brush.Get(),2.5f);
   if(settings.tab==3){
    brush->SetColor(ThemeColor(0x375c7c));
    target->DrawLine({300,615},{1115,615},brush.Get(),8);
    brush->SetColor(ThemeColor(0xeaf1ff));
    target->FillEllipse(D2D1::Ellipse({300,615},12,12),brush.Get());
   }
   result=target->EndDraw();
  }
  if(SUCCEEDED(result)&&state.keyboard){
   RECT keyboardRect{0,KeyboardY,SharedKeyboard::Width,KeyboardY+SharedKeyboard::Height};
   result=target->BindDC(dc,&keyboardRect);
   if(SUCCEEDED(result)){
    using namespace SharedKeyboard;
    target->BeginDraw();
    target->Clear(ThemeColor(0x101b2b));
    brush->SetColor(ThemeColor(0x182738));
    target->FillRectangle({0,0,Width,60},brush.Get());
    brush->SetColor(ThemeColor(0x30475d));
    target->DrawLine({0,60},{Width,60},brush.Get(),2);
    font->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    font->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    const auto button=[&](int id,D2D1_RECT_F box,const std::wstring& caption,bool active=false){
     brush->SetColor(ThemeColor(hover==id?0x396385:active?0x315979:0x22374e));
     target->FillRoundedRectangle(D2D1::RoundedRect(box,7,7),brush.Get());
     brush->SetColor(ThemeColor(0xeaf1ff));
     target->DrawText(caption.c_str(),static_cast<UINT32>(caption.size()),font.Get(),box,brush.Get());
    };
    for(int i=0;i<3;++i){
     button(Keys+i,toolbar[i],L"",i==(keyboardSettings?1:0));
     BrowserChrome::drawIcon(i==0?406:i==1?412:405,toolbar[i],target.Get(),brush.Get(),1,true,
         [](unsigned rgb){return ThemeColor(rgb);});
    }
    if(keyboardSettings){
     const auto label=[&](std::wstring caption,D2D1_RECT_F box){
      brush->SetColor(ThemeColor(0xeaf1ff));
      target->DrawText(caption.c_str(),static_cast<UINT32>(caption.size()),font.Get(),box,brush.Get());
     };
     label(L"Keyboard curve: "+std::to_wstring(static_cast<int>(keyboardCurve))+L" deg",{40,66,1140,120});
     button(Less,SharedKeyboard::settings[0],L"Curve -");
     button(Flat,SharedKeyboard::settings[1],L"Flat");
     button(More,SharedKeyboard::settings[2],L"Curve +");
     button(Snap,SharedKeyboard::settings[3],keySnap?L"Key Snap: On":L"Key Snap: Off",keySnap);
     label(L"Opacity: "+std::to_wstring(static_cast<int>(std::lround(keyboardOpacity*100)))+L"%",{40,365,290,425});
     const auto slider=SharedKeyboard::settings[4];
     const float knob=slider.left+(slider.right-slider.left)*(keyboardOpacity-.3f)/.7f;
     brush->SetColor(ThemeColor(0x30475d));
     target->DrawLine({slider.left,395},{slider.right,395},brush.Get(),10);
     brush->SetColor(ThemeColor(0xffbd55));
     target->FillEllipse(D2D1::Ellipse({knob,395},14,14),brush.Get());
    }else{
     for(int i=0;i<static_cast<int>(keyRects.size());++i){
      std::wstring label;
      switch(i){
      case Space:label=L"Space";break;
      case Backspace:label=L"Backspace";break;
      case Enter:label=L"Enter";break;
      case Paste:label=L"Paste";break;
      case Shift:label=L"Shift";break;
      default:{
       auto ch=character(i,keyboardShift);
       if(!keyboardShift&&ch>=L'a'&&ch<=L'z')ch=static_cast<wchar_t>(ch-L'a'+L'A');
       label.assign(1,ch);
       break;
      }
      }
      button(First+i,keyRects[i],label,i==Shift&&keyboardShift);
     }
    }
    font->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    result=target->EndDraw();
   }
  }
  if(SUCCEEDED(result)&&state.browser){
   RECT browserRect{0,BrowserY,browserPixelWidth(),BrowserY+browserPixelHeight()};
   result=target->BindDC(dc,&browserRect);
   if(SUCCEEDED(result)){
    target->BeginDraw();
    target->Clear(ThemeColor(0x101b2b));
    const float chromeScale=BrowserChromeScale;
    font->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    font->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    ComPtr<IDWriteTextFormat> tabFont;
    if(SUCCEEDED(text->CreateTextFormat(L"Segoe UI",nullptr,DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,13,L"en-us",&tabFont))){
     tabFont->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
     tabFont->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
     ComPtr<IDWriteInlineObject> ellipsis;
     if(SUCCEEDED(text->CreateEllipsisTrimmingSign(tabFont.Get(),&ellipsis))){
      const DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER,0,0};
      tabFont->SetTrimming(&trim,ellipsis.Get());
     }
     for(size_t index=0;index<browser.count();++index){
      auto box=tabRect(index);
      brush->SetColor(ThemeColor(index==browser.selected()?0x396385:0x22374e));
      target->FillRoundedRectangle(D2D1::RoundedRect(box,4,4),brush.Get());
      brush->SetColor(ThemeColor(0xeaf1ff));
      auto titleBox=box;
      titleBox.left+=6;titleBox.right-=browser.count()>1?22:4;
      const auto& title=tabTitles[index];
      target->DrawText(title.c_str(),static_cast<UINT32>(title.size()),tabFont.Get(),titleBox,brush.Get(),D2D1_DRAW_TEXT_OPTIONS_CLIP);
      if(browser.count()>1){
       const float x=box.right-10.f,y=(box.top+box.bottom)*.5f;
       target->DrawLine({x-4,y-4},{x+4,y+4},brush.Get(),1.6f);
       target->DrawLine({x-4,y+4},{x+4,y-4},brush.Get(),1.6f);
      }
     }
     if(browser.count()<OpenXRBrowser::MaxTabs){
      const auto last=tabRect(browser.count()-1);
      const auto box=D2D1::RectF(last.right+3,last.top,last.right+30,last.bottom);
      brush->SetColor(ThemeColor(hover==AddTab?0x396385:0x22374e));
      target->FillRoundedRectangle(D2D1::RoundedRect(box,4,4),brush.Get());
      brush->SetColor(ThemeColor(0xeaf1ff));
      tabFont->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
      target->DrawText(L"+",1,tabFont.Get(),box,brush.Get());
     }
    }
    font->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    target->SetTransform(D2D1::Matrix3x2F::Scale(chromeScale,chromeScale));
    font->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    const int actions[]={801,802,803,804,818,805,806,807,800};
    int action=0;
    for(int id:BrowserChrome::Icons){
     const auto rect=BrowserChrome::controlRect(id,chromeWidth());
     const bool available=true;
     brush->SetColor(ThemeColor(available&&hover==actions[action]?0x396385:id==412&&browserSettings?0x315979:0x22374e));
     target->FillRoundedRectangle(D2D1::RoundedRect(rect,7,7),brush.Get());
     BrowserChrome::drawIcon(id,rect,target.Get(),brush.Get(),browserZoom,available,[](unsigned rgb){return ThemeColor(rgb);});
     ++action;
    }
    for(int id:{417,418}){
     auto rect=BrowserChrome::controlRect(id,chromeWidth());
     brush->SetColor(ThemeColor(hover==(id==417?819:817)?0x396385:0x22374e));
     target->FillRoundedRectangle(D2D1::RoundedRect(rect,7,7),brush.Get());
     brush->SetColor(ThemeColor(0xeaf1ff));
     rect.left+=8;rect.top+=6;
     const wchar_t* caption=id==417?L"Copy":L"Paste";
     target->DrawText(caption,static_cast<UINT32>(wcslen(caption)),font.Get(),rect,brush.Get());
    }
    for(int id:{403,404}){
     auto box=BrowserChrome::controlRect(id,chromeWidth());
     const bool focused=id==403&&addressFocused;
     brush->SetColor(ThemeColor(focused?0x396385:0x22374e));
     target->FillRoundedRectangle(D2D1::RoundedRect(box,7,7),brush.Get());
     if(focused){
      brush->SetColor(ThemeColor(0x55d4ff));
      target->DrawRoundedRectangle(D2D1::RoundedRect(box,7,7),brush.Get(),3.f);
     }
     box.left+=8;box.top+=6;
     brush->SetColor(ThemeColor(0xeaf1ff));
     const std::wstring caption=id==404?L"Go":address.empty()?L"Paste a web address":address;
     target->DrawText(caption.c_str(),static_cast<UINT32>(caption.size()),font.Get(),box,brush.Get(),D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
    BrowserChrome::drawFooter(target.Get(),brush.Get(),font.Get(),browserPixelHeight()/chromeScale,
        hover==812?460:hover==813?461:-1,resizeCorner,[](unsigned rgb){return ThemeColor(rgb);},chromeWidth());
    target->SetTransform(D2D1::Matrix3x2F::Identity());
    if(browserSettings){
     const float scale=OpenXRBrowser::Width/SharedBrowserSettings::Width;
     font->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
     target->SetTransform(D2D1::Matrix3x2F::Scale(scale,scale)*
         D2D1::Matrix3x2F::Translation(0,float(OpenXRBrowser::Toolbar)));
     const int settingsHover=hover==808?480:hover==809?488:hover==810?489:hover==811?491:-1;
     SharedBrowserSettings::draw(target.Get(),brush.Get(),font.Get(),browserOpacity,browserCurve,
                                 settingsHover,-1,-1,[](unsigned rgb){return ThemeColor(rgb);});
     target->SetTransform(D2D1::Matrix3x2F::Identity());
    }
    result=target->EndDraw();
   }
  }
  if(SUCCEEDED(result)){
   GdiFlush();
   const auto* source=static_cast<const uint32_t*>(data);
   const int rows=state.browser?BrowserY+browserPixelHeight():state.keyboard?KeyboardY+SharedKeyboard::Height:settings.visible?SettingsY+900:height();
   pixels.assign(source,source+AtlasWidth*rows);
   if(state.browser && !browserSettings && frameWidth>0 && frameHeight>0 &&
      browserFrame.size()==static_cast<size_t>(frameWidth)*frameHeight){
    const int columns=std::min(frameWidth,browserPixelWidth());
    for(int y=0;y<std::min(frameHeight,pageHeight());++y)
     std::copy_n(browserFrame.data()+y*frameWidth,columns,
       pixels.data()+(BrowserY+OpenXRBrowser::Toolbar+y)*AtlasWidth);
   }
   for(auto& pixel:pixels){
    if(rgba)pixel=(pixel&0x0000ff00u)|((pixel&0xffu)<<16)|((pixel>>16)&0xffu);
    pixel|=0xff000000u;
   }
   if(state.browser){
    const auto alpha=static_cast<uint32_t>(std::round(browserOpacity*255.f));
    for(int y=BrowserY;y<BrowserY+browserPixelHeight();++y)
     for(int x=0;x<browserPixelWidth();++x)
      {
       auto& pixel=pixels[y*AtlasWidth+x];
       const auto channel=[&](int shift){return ((((pixel>>shift)&255u)*alpha+127u)/255u)<<shift;};
       pixel=(alpha<<24)|channel(16)|channel(8)|channel(0);
      }
   }
   pixels.resize(AtlasWidth*AtlasHeight,0xff000000u);
   std::fill_n(pixels.end()-AtlasWidth,8,0xffffffffu);
   for(int y=0;y<DotPixels;++y)
    for(int x=0;x<DotPixels;++x){
     const float dx=x+.5f-DotPixels*.5f,dy=y+.5f-DotPixels*.5f;
     const auto coverage=std::clamp(DotPixels*.5f-.5f-std::sqrt(dx*dx+dy*dy),0.f,1.f);
     const auto alpha=static_cast<uint32_t>(std::lround(255.f*coverage));
     pixels[(DotY+y)*AtlasWidth+x]=alpha*0x01010101u;
    }
   dirty=false;
   if(state.keyboard){
    const auto alpha=static_cast<uint32_t>(std::lround(keyboardOpacity*255));
    for(int y=KeyboardY;y<KeyboardY+SharedKeyboard::Height;++y)
     for(int x=0;x<SharedKeyboard::Width;++x){
      auto& pixel=pixels[y*AtlasWidth+x];
      const auto channel=[&](int shift){return ((((pixel>>shift)&255u)*alpha+127u)/255u)<<shift;};
      pixel=(alpha<<24)|channel(16)|channel(8)|channel(0);
     }
   }
   paintedKeyboard=state.keyboard;
   paintedBrowser=state.browser;
   paintedSettings=settings.visible;
   paintedBrowserSettings=browserSettings;
   paintedWidth=browserPixelWidth();
   paintedHeight=browserPixelHeight();
   paintedToolboxWidth=width();
   ++paintGeneration;
  }
  SelectObject(dc,old);
  DeleteObject(dib);
  DeleteDC(dc);
  return SUCCEEDED(result);
 }
};

