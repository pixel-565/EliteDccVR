std::vector<vr::VROverlayHandle_t> liveEdges;
#include "live_ui_edges.h"
uint64_t liveSerial = 0;
bool liveShown = false;
LivePoseHistory livePoseHistory;
LiveFrameAnchor<LiveUI::Vertices> liveAnchor;
float liveMeshUv[LiveUI::Vertices][2]{};
uint16_t liveMeshIndices[LiveUI::Indices]{};
uint64_t liveMeshRevision = 0;
bool liveHeadStill = false;
bool recordLiveHeadPose()
{
    vr::TrackedDevicePose_t pose{};
    const auto before = LiveUI::sampleTime();
    sys->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding, 0, &pose, 1);
    const auto after = LiveUI::sampleTime();
    if (!pose.bPoseIsValid || !pose.bDeviceIsConnected)
    {
        livePoseHistory.clear();
        liveHeadStill = false;
        return false;
    }
    V velocity{pose.vVelocity.v[0], pose.vVelocity.v[1], pose.vVelocity.v[2]};
    V angular{pose.vAngularVelocity.v[0], pose.vAngularVelocity.v[1], pose.vAngularVelocity.v[2]};
    liveHeadStill = std::isfinite(dot(velocity, velocity)) && std::isfinite(dot(angular, angular)) &&
                    dot(velocity, velocity) < .025f * .025f && dot(angular, angular) < .06f * .06f;
    livePoseHistory.add(before + (after - before) / 2, pose.mDeviceToAbsoluteTracking);
    return true;
}
bool liveHeadVertices(V *vertices, const LiveUI::Snapshot &s)
{
    V f{s.projection[3][0], s.projection[3][1], s.projection[3][2]};
    V r{s.projection[0][0], s.projection[0][1], s.projection[0][2]};
    V u{s.projection[1][0], s.projection[1][1], s.projection[1][2]};
    if (std::abs(dot(f, f) - 1.f) > .02f)
        return false;
    float b = dot(r, f), d = dot(u, f);
    r = r - f * b;
    u = u - f * d;
    float a = std::sqrt(dot(r, r)), c = std::sqrt(dot(u, u));
    if (a < .1f || c < .1f || std::abs(dot(r, u) / (a * c)) > .01f)
        return false;
    int eye = 0;
    float best = 1e6f;
    for (int i = 0; i < 2; ++i)
    {
        auto p = sys->GetProjectionMatrix(static_cast<vr::EVREye>(i), .1f, 100.f);
        float error = std::abs(a - p.m[0][0]) + std::abs(c - p.m[1][1]) + std::abs(b + p.m[0][2]) +
                      std::abs(d + p.m[1][2]);
        if (error < best)
        {
            best = error;
            eye = i;
        }
    }
    if (!std::isfinite(best) || best > .3f)
        return false;
    auto m = sys->GetEyeToHeadTransform(static_cast<vr::EVREye>(eye));
    for (unsigned i = 0; i < LiveUI::Vertices; ++i)
    {
        float w = s.clip[i][3];
        V p{(s.clip[i][0] - b * w) / a, (s.clip[i][1] - d * w) / c, -w};
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) || dot(p, p) > 900.f)
            return false;
        vertices[i] = {m.m[0][0] * p.x + m.m[0][1] * p.y + m.m[0][2] * p.z + m.m[0][3],
                       m.m[1][0] * p.x + m.m[1][1] * p.y + m.m[1][2] * p.z + m.m[1][3],
                       m.m[2][0] * p.x + m.m[2][1] * p.y + m.m[2][2] * p.z + m.m[2][3]};
    }
    return true;
}
void syncLiveFrame(bool dashboard)
{
    auto previous = liveUi.status;
    liveUi.poll();
    if (!liveUi.enabled && liveAnchor.valid)
        liveAnchor.reset();
    bool tracking = recordLiveHeadPose();
    bool show = (liveFrameRequested || liveMouseEnabled || cursorDiagnostics) && liveUi.enabled && workspace.open && !dashboard &&
                liveUi.snapshot.stamp;
    V vertices[LiveUI::Vertices];
    if (show && !liveHeadVertices(vertices, liveUi.snapshot))
    {
        show = false;
        liveUi.status = L"UI detected; eye projection mismatch. Frame hidden.";
    }
    vr::HmdMatrix34_t samplePose{};
    if (show && (!tracking || !livePoseHistory.at(liveUi.snapshot.sampleStamp, samplePose)))
    {
        show = false;
        liveUi.status = L"Waiting for synchronized headset tracking. Frame hidden.";
    }
    if (show)
        for (auto &point : vertices)
            point = LivePoseHistory::transform(samplePose, point);
    if (show)
    {
        LiveFrameAnchor<LiveUI::Vertices>::Points incoming;
        std::copy(std::begin(vertices), std::end(vertices), incoming.begin());
        LiveFrameAnchor<LiveUI::Vertices>::Model key;
        std::copy(std::begin(liveUi.snapshot.modelTransform), std::end(liveUi.snapshot.modelTransform),
                  key.begin());
        V viewer{samplePose.m[0][3], samplePose.m[1][3], samplePose.m[2][3]};
        liveAnchor.observe(incoming, viewer, key, liveUi.snapshot.pid, liveUi.snapshot.serial,
                           liveUi.snapshot.sampleStamp, liveHeadStill);
        if (!liveAnchor.valid)
        {
            liveUi.status = L"Hold your head still briefly to anchor the frame.";
            show = false;
        }
        else
        {
            if (liveMeshRevision != liveAnchor.revision)
            {
                memcpy(liveMeshUv, liveUi.snapshot.uv, sizeof(liveMeshUv));
                memcpy(liveMeshIndices, liveUi.snapshot.indices, sizeof(liveMeshIndices));
                liveMeshRevision = liveAnchor.revision;
            }
            std::copy(liveAnchor.points.begin(), liveAnchor.points.end(), std::begin(vertices));
            liveUi.status = liveAnchor.pending ? L"Confirming a new UI position. Frame remains anchored."
                                               : L"Frame anchored in SteamVR space.";
        }
    }
    else
        liveAnchor.pause();
    liveSurfaceReady =
        show && liveAnchor.valid && !liveAnchor.pending && liveMeshRevision == liveAnchor.revision;
    show = show && liveFrameRequested;
    if (previous != liveUi.status)
        giDirty = true;
    if (!show)
    {
        if (liveShown)
            for (auto h : liveEdges)
                vr::VROverlay()->HideOverlay(h);
        liveShown = false;
        return;
    }
    if (liveShown && liveSerial == liveAnchor.revision)
        return;
    size_t edge = 0;
    for (const auto &pair : LiveWireEdges)
    {
        unsigned x = pair[0], y = pair[1];
        V center = (vertices[x] + vertices[y]) * .5f, delta = vertices[y] - vertices[x];
        float length = std::sqrt(dot(delta, delta));
        if (length < .0001f)
            continue;
        if (edge == liveEdges.size())
        {
            vr::VROverlayHandle_t h{};
            auto key = "local.vrkeyboard.liveui." + std::to_string(edge);
            auto error = vr::VROverlay()->CreateOverlay(key.c_str(), "Live UI frame", &h);
            if (error != vr::VROverlayError_None)
            {
                for (auto existing : liveEdges)
                    vr::VROverlay()->HideOverlay(existing);
                liveUi.status = L"SteamVR cannot allocate the frame overlays";
                giDirty = true;
                liveShown = false;
                return;
            }
            liveEdges.push_back(h);
            uint32_t color = 0xffffff00;
            check(vr::VROverlay()->SetOverlayRaw(h, &color, 1, 1, 4));
            check(vr::VROverlay()->SetOverlayInputMethod(h, vr::VROverlayInputMethod_None));
        }
        auto h = liveEdges[edge++];
        V viewer = liveAnchor.viewer;
        V xAxis = delta * (1.f / length), yAxis = unit(cross(viewer - center, xAxis)),
          zAxis = unit(cross(xAxis, yAxis));
        auto m = matrix(xAxis, yAxis, zAxis, center);
        check(vr::VROverlay()->SetOverlayTransformAbsolute(h, vr::TrackingUniverseStanding, &m));
        check(vr::VROverlay()->SetOverlayWidthInMeters(h, length));
        check(vr::VROverlay()->SetOverlayTexelAspect(h, length / .0015f));
        check(vr::VROverlay()->ShowOverlay(h));
    }
    for (size_t i = edge; i < liveEdges.size(); ++i)
        vr::VROverlay()->HideOverlay(liveEdges[i]);
    liveSerial = liveAnchor.revision;
    liveShown = true;
}
