// Members of App. Cockpit controls have independent geometry and feedback.
LiveFrameAnchor<LiveUI::Vertices> shipAnchor;
uint64_t shipMeshRevision = 0;
float shipMeshUv[LiveUI::Vertices][2]{};
uint16_t shipMeshIndices[LiveUI::Indices]{};
LiveMeshHit shipHit;
bool shipReady = false, shipPointing = false;
int shipTarget = -1;
ShipControl::Navigation shipNavigation;
bool shipNavigationInput = false;
std::vector<vr::VROverlayHandle_t> shipEdges;
bool shipFrameShown = false;
uint64_t shipFrameRevision = 0;
V shipPoint(float u, float v)
{
    return shipAnchor.points[0]*((1-u)*(1-v)) + shipAnchor.points[10]*(u*(1-v)) +
           shipAnchor.points[66]*((1-u)*v) + shipAnchor.points[76]*(u*v);
}
void syncShipControls(bool dashboard)
{
    const auto &s=liveUi.ship.surface;
    bool show=liveUi.enabled && workspace.open && !dashboard && s.stamp && !liveUi.snapshot.stamp;
    V vertices[LiveUI::Vertices];vr::HmdMatrix34_t pose{};
    if(show && (!liveHeadVertices(vertices,s) || !livePoseHistory.at(s.sampleStamp,pose)))show=false;
    if(show)
    {
        LiveFrameAnchor<LiveUI::Vertices>::Points incoming;
        for(unsigned i=0;i<LiveUI::Vertices;++i)incoming[i]=LivePoseHistory::transform(pose,vertices[i]);
        LiveFrameAnchor<LiveUI::Vertices>::Model model;
        std::copy(std::begin(s.modelTransform),std::end(s.modelTransform),model.begin());
        shipAnchor.setInterval(static_cast<int>(liveAnchor.settleMs));
        shipAnchor.observe(incoming,{pose.m[0][3],pose.m[1][3],pose.m[2][3]},model,s.pid,s.serial,s.sampleStamp,liveHeadStill);
        if(shipAnchor.valid && shipMeshRevision!=shipAnchor.revision)
        {
            memcpy(shipMeshUv,s.uv,sizeof(shipMeshUv));memcpy(shipMeshIndices,s.indices,sizeof(shipMeshIndices));
            shipMeshRevision=shipAnchor.revision;
        }
    }
    else shipAnchor.pause();
    if(!liveUi.enabled)shipAnchor.reset();
    shipReady=show && shipAnchor.valid && !shipAnchor.pending && shipMeshRevision==shipAnchor.revision;
    if(!shipReady)shipNavigation.reset();
    bool frame=shipReady && liveFrameRequested;
    if(!frame)
    {
        if(shipFrameShown)for(auto h:shipEdges)vr::VROverlay()->HideOverlay(h);
        shipFrameShown=false;return;
    }
    if(shipFrameShown && shipFrameRevision==shipAnchor.revision)return;
    constexpr float lines[][4]={{.015f,.449f,.985f,.449f},{.015f,.973f,.985f,.973f},
        {.015f,.449f,.015f,.973f},{.985f,.449f,.985f,.973f},
        {.015f,.578f,.985f,.578f},{.015f,.710f,.985f,.710f},{.015f,.844f,.985f,.844f}};
    for(size_t i=0;i<std::size(lines);++i)
    {
        if(i==shipEdges.size())
        {
            vr::VROverlayHandle_t h{};auto key="local.vrkeyboard.shipframe."+std::to_string(i);
            if(vr::VROverlay()->CreateOverlay(key.c_str(),"Ship controls frame",&h)!=vr::VROverlayError_None)return;
            shipEdges.push_back(h);uint32_t color=0xffffff00;
            check(vr::VROverlay()->SetOverlayRaw(h,&color,1,1,4));
            check(vr::VROverlay()->SetOverlayInputMethod(h,vr::VROverlayInputMethod_None));
        }
        V a=shipPoint(lines[i][0],lines[i][1]),b=shipPoint(lines[i][2],lines[i][3]);
        V center=(a+b)*.5f,delta=b-a;float length=std::sqrt(dot(delta,delta));
        if(length<.0001f)continue;
        V x=delta*(1.f/length),y=unit(cross(shipAnchor.viewer-center,x)),z=unit(cross(x,y));
        auto transform=matrix(x,y,z,center);auto h=shipEdges[i];
        check(vr::VROverlay()->SetOverlayTransformAbsolute(h,vr::TrackingUniverseStanding,&transform));
        check(vr::VROverlay()->SetOverlayWidthInMeters(h,length));
        check(vr::VROverlay()->SetOverlayTexelAspect(h,length/.0015f));
        check(vr::VROverlay()->ShowOverlay(h));
    }
    shipFrameShown=true;shipFrameRevision=shipAnchor.revision;
}
void updateShipControls(bool allowed,bool triggerPressed)
{
    auto now=GetTickCount64();
    bool valid=allowed && liveMouseEnabled && liveUi.enabled && shipReady && shipPointing &&
               isEliteForeground(GetForegroundWindow());
    if (!valid && shipNavigationInput) { releaseNavigation(); shipNavigationInput=false; }
    bool busy=pasteStage>=0 || keyStage>=0 || !pendingKeys.empty() || !navigationRelease.empty();
    int action=shipNavigation.update(now,valid,shipTarget,liveUi.ship.selected,liveUi.ship.surface.stamp,triggerPressed,busy);
    if(action>=0 && valid && navigation[action].vk)
    {
        startNavigation(action);
        shipNavigationInput=!navigationRelease.empty();
    }
}
