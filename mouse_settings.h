#pragma once
#include <istream>
#include <ostream>
#include <sstream>
#include <string>
struct MouseSettings
{
    bool integration = false, frame = false, mouse = false, diagnostics = false, align = false;
    void read(std::istream &input)
    {
        std::string line;
        while (std::getline(input, line))
        {
            std::istringstream row(line);
            std::string key, extra;
            int value;
            if (!(row >> key >> value) || (value != 0 && value != 1) || (row >> extra))
                continue;
            if (key == "Integration")
                integration = value != 0;
            else if (key == "ShowFrame")
                frame = value != 0;
            else if (key == "Mouse")
                mouse = value != 0;
            else if (key == "CursorDiagnostics")
                diagnostics = value != 0;
            else if (key == "AutoAlignCursor")
                align = value != 0;
        }
    }
    void write(std::ostream &output) const
    {
        output << "Integration " << integration << '\n'
               << "ShowFrame " << frame << '\n'
               << "Mouse " << mouse << '\n'
               << "CursorDiagnostics " << diagnostics << '\n'
               << "AutoAlignCursor " << align << '\n';
    }
};
