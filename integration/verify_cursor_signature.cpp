#include "cursor_signature.h"
#include <fstream>
#include <vector>
#include <iostream>
#include <iterator>
int main()
{
    std::ifstream input("integration/fixtures/cursor-vs.dxbc", std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(input)), {});
    if (bytes.size()!=2992 || !CursorSignature::shader(bytes.data(),bytes.size())) return 1;
    if (CursorSignature::shader(nullptr,bytes.size()) || CursorSignature::shader(bytes.data(),bytes.size()-1)) return 2;
    for (size_t i : {size_t(0),size_t(128),bytes.size()-1}) {
        bytes[i]^=1;
        if (CursorSignature::shader(bytes.data(),bytes.size())) return 3;
        bytes[i]^=1;
    }
    std::cout << "PASS exact cursor shader fingerprint and rejection of modified/unrelated code\n";
    return 0;
}
