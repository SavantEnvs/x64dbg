// Fuzz x64dbg's byte-pattern engine (src/dbg/patternfind.cpp), which parses the user-supplied hex
// patterns with '?' wildcards of the find/findall/findallmem commands and the memory
// search-and-replace command (patternsnr).
// Input layout: <search pattern> NUL <replace pattern> NUL <data bytes>.
#include "patternfind.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    const uint8_t* end = data + size;
    const uint8_t* nul1 = static_cast<const uint8_t*>(std::memchr(data, 0, size));
    if(!nul1)
        return -1;
    const uint8_t* nul2 = static_cast<const uint8_t*>(std::memchr(nul1 + 1, 0, end - (nul1 + 1)));
    if(!nul2)
        return -1;
    const std::string search(reinterpret_cast<const char*>(data), nul1 - data);
    const std::string replace(reinterpret_cast<const char*>(nul1 + 1), nul2 - (nul1 + 1));
    std::vector<unsigned char> buffer(nul2 + 1, end);

    std::vector<PatternByte> pattern;
    volatile size_t sink = 0;
    if(patterntransform(search, pattern))
        sink += patternfind(buffer.data(), buffer.size(), pattern);
    int patternsize = 0;
    sink += patternfind(buffer.data(), buffer.size(), search.c_str(), &patternsize);
    sink += patternsize;

    std::vector<unsigned char> copy = buffer;
    sink += patternsnr(copy.data(), copy.size(), search.c_str(), replace.c_str());
    copy = buffer;
    patternwrite(copy.data(), copy.size(), replace.c_str());
    (void)sink;
    return 0;
}
