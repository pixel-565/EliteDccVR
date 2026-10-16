#include "../mouse_settings.h"
#include "../cursor_visibility.h"
#include <iostream>
int main()
{
    for (unsigned bits = 0; bits < 64; ++bits)
    {
        MouseSettings original{bool(bits & 1), bool(bits & 2), bool(bits & 4), bool(bits & 8),
                               bool(bits & 16),bool(bits & 32)};
        std::stringstream serialized;
        original.write(serialized);
        MouseSettings restored;
        restored.read(serialized);
        if (original.integration != restored.integration || original.frame != restored.frame ||
            original.mouse != restored.mouse || original.diagnostics != restored.diagnostics ||
            original.align != restored.align || original.hideCursor != restored.hideCursor)
            return 1;
    }
    MouseSettings defaults;
    std::istringstream invalid(
        "Integration 2\nMouse 1 garbage\nCursorDiagnostics no\nUnknown 1\nAutoAlignCursor 1\n");
    defaults.read(invalid);
    if (defaults.integration || defaults.mouse || defaults.diagnostics || !defaults.align)
        return 1;
    if (defaults.hideCursor) return 1;
    if (!CursorVisibility::active(true,true,true,2000,1990,1990,1990,1990)) return 2;
    if (CursorVisibility::active(true,true,true,2000,1990,1879,1990,1990)) return 3;
    if (CursorVisibility::active(true,true,true,2000,1990,1990,1899,1990)) return 4;
    if (CursorVisibility::active(true,true,true,2000,1990,1990,1990,1899)) return 5;
    if (CursorVisibility::active(true,true,true,2000,499,1990,1990,1990)) return 6;
    if (CursorVisibility::active(false,true,true,2000,1990,1990,1990,1990)) return 7;
    if (CursorVisibility::active(true,false,true,2000,1990,1990,1990,1990)) return 8;
    if (CursorVisibility::active(true,true,false,2000,1990,1990,1990,1990)) return 9;
    if (CursorVisibility::active(true,true,true,2000,1990,2001,1990,1990)) return 10;
    std::cout << "PASS all 64 settings combinations round-trip and malformed settings rejection\n";
}
