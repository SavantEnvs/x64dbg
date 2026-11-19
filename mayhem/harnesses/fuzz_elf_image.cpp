// Fuzz ElfBug::ElfImage::Parse, the ELF header / program header / section table parser the Linux
// debugger runs on every module image it maps (ElfImageCache reads the file, then calls Parse).
// The input is the image file; the reader serves bytes at file offsets with a bounds check, the same
// contract as the file reader in ElfImageCache and the test suite's ParseBytes helper.
#include <ElfBug/elf/ElfImage.h>
#include <cstddef>
#include <cstdint>
#include <cstring>

using namespace ElfBug;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    const ElfReader read = [data, size](const uint64_t offset, void* buffer, const size_t len)
    {
        if(offset > size || len > size - offset)
            return false;
        if(len)
            std::memcpy(buffer, data + offset, len);
        return true;
    };

    volatile uint64_t sink = 0;
    for(const auto parts : {ElfImage::Parts::Headers, ElfImage::Parts::HeadersAndSections})
    {
        const auto image = ElfImage::Parse(read, parts);
        if(!image)
            continue;
        sink += image->End();
        sink += image->HeaderBytes().size();
        for(const auto & segment : image->Segments())
        {
            sink += segment.End();
            const auto bias = image->LoadBias(PageFloor(segment.vaddr), PageFloor(segment.offset));
            if(bias)
                sink += *bias;
        }
        for(const auto & section : image->Sections())
            sink += section.name.size() + section.addr + section.size;
    }
    (void)sink;
    return 0;
}
