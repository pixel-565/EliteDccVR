#include "../main.cpp"
#include <iostream>
void require(bool v,const char *s){if(!v)throw std::runtime_error(s);}
int main(){try{
 int samples=0;V center{.2f,1,-2},r=unit(V{1,0,.2f}),n=unit(V{-.2f,0,1}),up{0,1,0};
 for(float curve:{0.f,5.f,30.f,60.f,120.f})for(float w:{.9f,1.4f,2.5f}){
 float h=1.4f;V origin=center+n*2.f;
 for(int row=0;row<=16;++row)for(int col=0;col<=24;++col){float u=col/24.f,v=row/16.f;V p=CurvedPanel::point(center,r,up,n,w,h,curve,u,v);vr::VROverlayIntersectionResults_t hit{};require(CurvedPanel::hit(origin,unit(p-origin),center,r,up,n,w,h,curve,hit),"Visible surface point missed");require(std::abs(hit.vUVs.v[0]-u)<.00003f&&std::abs(hit.vUVs.v[1]-v)<.00003f,"Curved UV mismatch");V actual{hit.vPoint.v[0],hit.vPoint.v[1],hit.vPoint.v[2]};require(dot(actual-p,actual-p)<1.e-8f,"Wrong surface location");++samples;}
 for(auto uv:std::vector<std::pair<float,float>>{{-.01f,.5f},{1.01f,.5f},{.5f,-.01f},{.5f,1.01f}}){V p=CurvedPanel::point(center,r,up,n,w,h,curve,uv.first,uv.second);vr::VROverlayIntersectionResults_t hit{};require(!CurvedPanel::hit(origin,unit(p-origin),center,r,up,n,w,h,curve,hit),"Invisible margin accepted");}
 for(int side:{-1,1}){BrowserResize resize;V c=center;float rw=w,rh=h;V corner=CurvedPanel::point(center,r,up,n,w,h,curve,side<0?0.f:1.f,0);V fixed=CurvedPanel::point(center,r,up,n,w,h,curve,side<0?1.f:0.f,1);require(resize.begin(side,c,r,up,n,rw,rh,origin,unit(corner-origin),curve),"Resize start failed");require(resize.update(origin,unit(corner-origin),c,rw,rh),"Resize no-op failed");require(dot(c-center,c-center)<1.e-8f&&std::abs(rw-w)<.0001f,"Resize jumps on grip");V dragged=corner+r*(side*.12f)-up*.08f;require(resize.update(origin,unit(dragged-origin),c,rw,rh),"Resize update failed");V newFixed=CurvedPanel::point(c,r,up,n,rw,rh,curve,side<0?1.f:0.f,1);require(dot(newFixed-fixed,newFixed-fixed)<1.e-8f,"Opposite curved corner moved");}
 }

 App test;
 vr::InputAnalogActionData_t wheelStick{};wheelStick.bActive=true;wheelStick.y=1;
 auto wheelBlocked=[&](bool allowed){test.gameMouseWheel.update(1,1000);test.updateLiveScroll(wheelStick,allowed);require(test.gameMouseWheel.direction==0,"Blocked game wheel did not reset");};
 wheelBlocked(false);
 wheelBlocked(true);
 test.liveMouseMoveReady=true;wheelBlocked(true);
 test.liveMouseEnabled=true;wheelBlocked(true);
 test.liveUi.enabled=true;wheelBlocked(true);
 test.liveSurfaceReady=true;wheelBlocked(true);
 test.liveMouseEnabled=false;test.liveUi.enabled=false;test.liveSurfaceReady=false;test.liveMouseMoveReady=false;
 require(keyboardCharacterFor(0,false)==L'1' && keyboardCharacterFor(0,true)==L'!' && keyboardCharacterFor(9,true)==L')',"Shift number mapping wrong");
 require(keyboardCharacterFor(10,false)==L'q' && keyboardCharacterFor(10,true)==L'Q' && keyboardCharacterFor(36,true)==L'_',"Shift letter/punctuation mapping wrong");
 require(test.shiftKeyIndex()==49,"Elite Shift target wrong");
 test.activate(49);require(test.keyboardShift,"Shift activation failed");
 test.activate(49);require(!test.keyboardShift,"Shift toggle failed");
 for(float curve:{0.f,5.f,30.f,60.f,120.f}) {
  test.keyboardCurveDegrees=curve;
  V c=test.anchor-test.normal*test.distance+test.right*test.offsetX+test.up*test.offsetY;
  V o=c+test.normal*2.f;
  for(float u:{0.f,.25f,.5f,.75f,1.f})for(float v:{0.f,.5f,1.f}) {
   V p=CurvedPanel::point(c,test.right,test.up,test.normal,test.width,test.width*H/W,curve,u,v);
   vr::VROverlayIntersectionResults_t hit{};
   require(test.intersectKeyboard(o,unit(p-o),hit),"Curved keyboard point missed");
   require(std::abs(hit.vUVs.v[0]-u)<.00003f && std::abs(hit.vUVs.v[1]-v)<.00003f,"Keyboard UV mismatch");
  }
 }
 test.keyboardCurveDegrees=0;
 require(ToolH == 210 && ToolW == 970, "Unexpected toolbox dimensions");
 for (int i=0;i<5;++i) {
  auto rect=toolButtons[i].r;
  require(rect.bottom-rect.top==rect.right-rect.left && rect.top==20 && ToolH-rect.bottom==20,"Toolbox buttons are not square/evenly padded");
  require(test.hitToolControl((rect.left+rect.right)*.5f,105)==300+i,"Toolbox target mismatch");
 }
 require(test.hitToolControl(105,5)==-1 && test.hitToolControl(105,205)==-1,"Invisible toolbox margins are interactive");
 test.workspace.open=true;test.workspace.keyboard=true;test.workspace.browser=true;
 test.activate(304);
 require(test.toolboxCollapsed && test.workspace.open && test.workspace.keyboard && test.workspace.browser,"Collapse changed panel visibility");
 require(test.hitToolControl(105,105)==305 && test.hitToolControl(300,105)==-1,"Collapsed hit bounds incorrect");
 test.toolCenter={0,0,-2};test.toolRight={1,0,0};test.toolUp={0,1,0};test.toolNormal={0,0,1};
 vr::VROverlayIntersectionResults_t toolHit{};
 require(test.intersectToolbox({0,0,0},{0,0,-1},toolHit),"Collapsed toolbox center missed");
 require(!test.intersectToolbox({.2f,0,0},{0,0,-1},toolHit),"Collapsed toolbox has invisible horizontal area");
 require(!test.intersectToolbox({0,.2f,0},{0,0,-1},toolHit),"Toolbox has invisible vertical area");
 test.activate(305);require(!test.toolboxCollapsed,"Show failed to expand toolbox");
 require(test.intersectToolbox({.2f,0,0},{0,0,-1},toolHit),"Expanded toolbox bounds incorrect");
 test.giSelectedTab=2;
 require(test.hitGameInterface(200,740)==642 && test.hitGameInterface(900,740)==643,"Environment idle controls missing");
 auto idleSettingsFolder=std::filesystem::temp_directory_path()/("vrkeyboard-idle-settings-"+std::to_string(GetCurrentProcessId()));
 std::filesystem::create_directories(idleSettingsFolder);test.root=idleSettingsFolder;
 test.giSelectedTab=0;
 require(test.hitGameInterface(950,560)==611 && test.hitGameInterface(1100,560)==612,"X sensitivity targets missing");
 test.liveMouseYPercent=53;test.liveMouseXPercent=100;
 test.activateGameInterface(611);
 require(test.liveMouseXPercent==99 && test.liveMouseYPercent==53,"X correction affects Y");
 {std::ifstream in(idleSettingsFolder/"mouse-y-scale.txt");int y=0,x=0;in>>y>>x;require(y==53 && x==99,"Axis sensitivities not persisted");}
 test.liveMouseXPercent=25;test.activateGameInterface(611);require(test.liveMouseXPercent==25,"X minimum exceeded");
 test.liveMouseXPercent=125;test.activateGameInterface(612);require(test.liveMouseXPercent==125,"X maximum exceeded");
 require(std::abs(LiveMouse::vertical(.5f,.5f)-.5f)<1.e-6f && std::abs(LiveMouse::vertical(.8f,.5f)-.65f)<1.e-6f,"Centered X correction wrong");
 require(test.hitGameInterface(950,625)==613 && test.hitGameInterface(1100,690)==616,"Drag controls missing");
 test.activateGameInterface(614);test.activateGameInterface(615);
 require(test.leftDragDivisor==9 && test.rightDragDivisor==7,"Independent drag divisors failed");
 {std::ifstream in(idleSettingsFolder/"mouse-drag-settings.txt");int l=0,r=0;in>>l>>r;require(l==9 && r==7,"Drag persistence failed");}
 test.leftDragDivisor=1;test.activateGameInterface(613);require(test.leftDragDivisor==1,"Drag minimum failed");
 test.rightDragDivisor=50;test.activateGameInterface(616);require(test.rightDragDivisor==50,"Drag maximum failed");
 require(!test.hideGameCursor && test.hitGameInterface(500,755)==617,"Cursor hide control default or bounds wrong");
 test.activateGameInterface(617);require(test.hideGameCursor,"Cursor hide On failed");
 {std::ifstream in(idleSettingsFolder/"mouse-settings.txt");MouseSettings persisted;persisted.read(in);require(persisted.hideCursor,"Cursor hide setting not persisted");}
 require(test.liveUi.channel.open(GetCurrentProcessId(),true),"Cursor visibility IPC test setup failed");
 test.liveUi.channel.state->hideGameCursor=1;test.liveUi.channel.state->cursorControlStamp=GetTickCount64();
 test.activateGameInterface(617);require(!test.hideGameCursor,"Cursor hide Off failed");
 require(test.liveUi.channel.state->hideGameCursor==0 && test.liveUi.channel.state->cursorControlStamp==0,"Off did not clear rendering command immediately");
 std::filesystem::remove(idleSettingsFolder/"mouse-drag-settings.txt");
 std::filesystem::remove(idleSettingsFolder/"mouse-y-scale.txt");
 std::filesystem::remove(idleSettingsFolder/"mouse-settings.txt");
 test.giSelectedTab=2;
 test.rayIdleSeconds=5;test.activateGameInterface(643);require(test.rayIdleSeconds==6,"Idle timeout increment failed");
 {std::ifstream in(idleSettingsFolder/"settings.txt");std::vector<float> values;float value=0;while(in>>value)values.push_back(value);require(values.size()>1 && values[values.size()-2]==6 && values.back()==0,"Idle timeout/keyboard curve not persisted");}
 test.rayIdleSeconds=1;test.activateGameInterface(642);require(test.rayIdleSeconds==1,"Idle minimum exceeded");
 test.rayIdleSeconds=120;test.activateGameInterface(643);require(test.rayIdleSeconds==120,"Idle maximum exceeded");
 std::filesystem::remove(idleSettingsFolder/"settings.txt");std::filesystem::remove(idleSettingsFolder);
 test.giSelectedTab=0;require(test.hitGameInterface(200,740)!=642,"Idle controls leak into other tabs");

 for(float w:{.9f,1.1f,2.5f}) for(float h:{.7f,1.2f,2.f}) {
  test.navSurface.width=w;test.navSurface.height=h;
  float sx=test.navScaleX(),sy=test.navScaleY();
  auto rect=test.navRect(701);
  float x=(rect.left+rect.right)*.5f;
  float y=(rect.top+rect.bottom)*.5f;
  require(test.navHitControl(x,y)==701,"Resized tab input mismatch");
  require(test.navHitControl(30,775)==720 && test.navHitControl(1170,775)==721,"Resized corner target lost");
  require(test.navHitControl(30,35)==702 && test.navHitControl(1165,35)==700,"Header icon target lost");
  auto icon=test.navIconTransform(test.navRect(702));
  require(std::abs(w/App::NavW*icon._11-h/App::NavH*icon._22)<1.e-6f,"Icon proportions changed");
  test.navTab=1;
  require(test.navHitControl(App::NavW*(1-sx)*.5f+700*sx,App::NavH*(1-sy)*.5f+525*sy)==713,"Resized opacity slider target lost");
  test.navTab=0;
  require(std::abs(w/1200*sx-h/800*sy)<1.e-6f,"Content proportions changed");
 }
 test.navSurface.width=1.1f;test.navSurface.height=1.1f*800/1200;
 require(test.navHitControl(580,130)==703,"Exobio tab missing");
 test.activateNav(703);
 require(test.navTab==2,"Exobio tab activation failed");
 require(test.navHitControl(200,350)==-1,"Settings controls leak into Exobio");
 require(test.navHitControl(1100,700)==736,"Exobio paging unavailable");
 test.activateNav(736);require(test.navExobioPage==1,"Exobio page selection failed");
 test.activateNav(735);require(test.navExobioPage==0,"Exobio previous page failed");
 test.navPlanetDetail=true;
 require(test.navHitControl(1070,40)==-1,"Hidden Hide button remains interactive");
 require(test.navHitControl(30,30)==730 && test.navHitControl(200,130)==731,"Detail back/info missing");
 require(test.navHitControl(500,130)==-1 && test.navHitControl(900,130)==-1,"Unknown signal tabs visible");
 test.navPlanet.bioCount=3;
 require(test.navHitControl(500,130)==732 && test.navHitControl(900,130)==-1,"Bio signal tab gating wrong");
 test.navPlanet.geoCount=2;
 require(test.navHitControl(900,130)==733,"Geo signal tab missing");
 std::cout<<"PASS curved panel bounds and both resize corners: "<<samples<<" surface samples; resized tab input and proportions\n";
 return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}

