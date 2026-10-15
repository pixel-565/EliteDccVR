#include "../mouse_settings.h"
#include <iostream>
int main()
{
    for (unsigned bits = 0; bits < 32; ++bits)
    {
        MouseSettings original{bool(bits & 1), bool(bits & 2), bool(bits & 4), bool(bits & 8),
                               bool(bits & 16)};
        std::stringstream serialized;
        original.write(serialized);
        MouseSettings restored;
        restored.read(serialized);
        if (original.integration != restored.integration || original.frame != restored.frame ||
            original.mouse != restored.mouse || original.diagnostics != restored.diagnostics ||
            original.align != restored.align)
            return 1;
    }
    MouseSettings defaults;
    std::istringstream invalid(
        "Integration 2\nMouse 1 garbage\nCursorDiagnostics no\nUnknown 1\nAutoAlignCursor 1\n");
    defaults.read(invalid);
    if (defaults.integration || defaults.mouse || defaults.diagnostics || !defaults.align)
        return 1;
    std::cout << "PASS all 32 settings combinations round-trip and malformed settings rejection\n";
}
