#include <cstdint>
#include <fstream>
#include <vector>
#include <iostream>
#include <filesystem>
#include <stdexcept>
#include "decode.h"
#include "cursor_math.h"
std::vector<char> read(const std::filesystem::path &p)
{
    std::ifstream f(p, std::ios::binary);
    if (!f)
        throw std::runtime_error("Missing fixture");
    return {std::istreambuf_iterator<char>(f), {}};
}
int wmain(int argc, wchar_t **argv)
{
    try
    {
        if (argc != 8)
            throw std::runtime_error("Usage: VerifyCursor capture mesh_draw cursor_draw base_vertex "
                                     "vertex_min expected_x expected_y");
        std::filesystem::path folder = argv[1];
        std::string mesh = "draw-" + std::to_string(std::stoi(argv[2])),
                    cursor = "draw-" + std::to_string(std::stoi(argv[3]));
        auto raw = read(folder / (mesh + "-slot1-vertex-hypothesis.bin")),
             model = read(folder / (mesh + "-instance0-t33.bin")), cb = read(folder / (mesh + "-vs-cb1.bin")),
             ib = read(folder / (mesh + "-indices.bin")), cm = read(folder / (cursor + "-instance0-t33.bin"));
        if (raw.size() != 77 * 40 || ib.size() != 360 * 2)
            throw std::runtime_error("Unexpected fixture size");
        CursorMath::P points[77];
        float uv[77][2];
        uint16_t indices[360];
        for (int i = 0; i < 77; ++i)
        {
            UiDecode::Vector p;
            if (!UiDecode::position(raw.data() + i * 40, model.data(), cb.data(), p) ||
                !UiDecode::texcoord(raw.data() + i * 40, uv[i]))
                throw std::runtime_error("Decode failed");
            points[i] = {p.x + UiDecode::number(cb.data(), 4400), p.y + UiDecode::number(cb.data(), 4404),
                         p.z + UiDecode::number(cb.data(), 4408)};
        }
        for (int i = 0; i < 360; ++i)
        {
            uint16_t rawIndex;
            memcpy(&rawIndex, ib.data() + i * 2, 2);
            int index = rawIndex + std::stoi(argv[4]) - std::stoi(argv[5]);
            if (index < 0 || index >= 77)
                throw std::runtime_error("Bad index");
            indices[i] = static_cast<uint16_t>(index);
        }
        CursorMath::P p{UiDecode::number(cm.data(), 16), UiDecode::number(cm.data(), 20),
                        UiDecode::number(cm.data(), 24)};
        CursorMath::Hit hit;
        for (int i = 0; i < 360; i += 3)
            CursorMath::triangle(p, points, uv, indices + i, hit);
        if (hit.distance > .003f || std::abs(hit.u * 1680 - std::stof(argv[6])) > .2f ||
            std::abs(hit.v * 1050 - std::stof(argv[7])) > .2f)
            throw std::runtime_error("Cursor mapping mismatch");
        CursorMath::Hit far;
        for (int i = 0; i < 360; i += 3)
            CursorMath::triangle(p + CursorMath::P{0, 0, 10}, points, uv, indices + i, far);
        if (far.distance < .003f)
            throw std::runtime_error("Off-surface cursor accepted");
        std::cout << "PASS cursor mapping " << hit.u * 1680 << "," << hit.v * 1050 << " residual "
                  << hit.distance << "; off-surface rejection\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
