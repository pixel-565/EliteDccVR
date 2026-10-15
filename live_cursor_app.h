// Diagnostic only: this marker never changes the mouse target or sends input.
void syncCursorDiagnostics()
{
    const auto &c = liveUi.cursor;
    bool valid = cursorDiagnostics && liveUi.enabled && liveSurfaceReady && c.stamp &&
                 GetTickCount64() - c.stamp <= 150 && c.pid == liveUi.snapshot.pid &&
                 !memcmp(c.modelTransform, liveUi.snapshot.modelTransform, sizeof(c.modelTransform));
    V position{}, cursorNormal{};
    bool found = false;
    if (valid)
        for (unsigned i = 0; i < LiveUI::Indices; i += 3)
        {
            unsigned a = liveMeshIndices[i], b = liveMeshIndices[i + 1], d = liveMeshIndices[i + 2];
            float ax = liveMeshUv[a][0], ay = liveMeshUv[a][1], bx = liveMeshUv[b][0] - ax,
                  by = liveMeshUv[b][1] - ay, dx = liveMeshUv[d][0] - ax, dy = liveMeshUv[d][1] - ay,
                  px = c.u - ax, py = c.v - ay, det = bx * dy - by * dx;
            if (std::abs(det) < 1e-10f)
                continue;
            float wb = (px * dy - py * dx) / det, wd = (bx * py - by * px) / det, wa = 1 - wb - wd;
            if (wa < -.001f || wb < -.001f || wd < -.001f)
                continue;
            position = liveAnchor.points[a] * wa + liveAnchor.points[b] * wb + liveAnchor.points[d] * wd;
            cursorNormal = unit(cross(liveAnchor.points[b] - liveAnchor.points[a],
                                      liveAnchor.points[d] - liveAnchor.points[a]));
            if (dot(cursorNormal, liveAnchor.viewer - position) < 0)
                cursorNormal = cursorNormal * -1;
            found = true;
            break;
        }
    valid = valid && found;
    std::wstring status = cursorDiagnostics ? L"Cursor not found / stale" : L"Cursor diagnostics Off";
    if (valid)
    {
        if (!cursorOverlay)
        {
            if (vr::VROverlay()->CreateOverlay("local.vrkeyboard.cursor-diagnostic", "Game cursor diagnostic",
                                               &cursorOverlay) != vr::VROverlayError_None)
                cursorOverlay = 0;
            if (cursorOverlay)
            {
                uint32_t pixels[32 * 32]{};
                for (int y = 0; y < 32; ++y)
                    for (int x = 0; x < 32; ++x)
                    {
                        int r = (x - 16) * (x - 16) + (y - 16) * (y - 16);
                        if (r >= 81 && r <= 196)
                            pixels[y * 32 + x] = 0xffff40ff;
                    }
                check(vr::VROverlay()->SetOverlayRaw(cursorOverlay, pixels, 32, 32, 4));
                check(vr::VROverlay()->SetOverlayInputMethod(cursorOverlay, vr::VROverlayInputMethod_None));
                check(vr::VROverlay()->SetOverlayWidthInMeters(cursorOverlay, .016f));
            }
        }
        if (cursorOverlay)
        {
            V rightAxis = unit(cross(V{0, 1, 0}, cursorNormal));
            if (dot(rightAxis, rightAxis) < .1f)
                rightAxis = {1, 0, 0};
            auto m = matrix(rightAxis, unit(cross(cursorNormal, rightAxis)), cursorNormal,
                            position + cursorNormal * .003f);
            check(vr::VROverlay()->SetOverlayTransformAbsolute(cursorOverlay, vr::TrackingUniverseStanding,
                                                               &m));
            check(vr::VROverlay()->ShowOverlay(cursorOverlay));
        }
        status = L"Cursor found: " + std::to_wstring(int(std::lround(c.u * 1680))) + L", " +
                 std::to_wstring(int(std::lround(c.v * 1050)));
        if (liveMousePointing)
            status += L"\nDelta X: " + std::to_wstring(int(std::lround((c.u - liveMouseHit.u) * 1680))) +
                      L" px   Delta Y: " + std::to_wstring(int(std::lround((c.v - liveMouseHit.v) * 1050))) +
                      L" px";
        else
            status += L"\nPoint the ray at the game UI to compare";
        if (!cursorOverlay)
            status += L" (marker unavailable)";
    }
    else if (cursorOverlay)
        vr::VROverlay()->HideOverlay(cursorOverlay);
    if (GetTickCount64() - cursorStatusTime >= 100 && status != cursorDiagnosticStatus)
    {
        cursorDiagnosticStatus = status;
        cursorStatusTime = GetTickCount64();
        giDirty = true;
    }
}
