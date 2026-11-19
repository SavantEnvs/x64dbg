// Fuzz the ElfBug /proc text parsers (src/cross/ElfBug/ElfBug/process/ProcFs.cpp). The debuggee controls
// what they read: its comm name and the paths it maps can hold spaces, newlines and parentheses.
// The input is treated as the contents of a /proc file and fed to each parser the way ElfBug's own
// callers do:
//   /proc/<pid>/maps   MemoryMap readRegions: Split(text, '\n') then ParseMapsLine on every line
//   /proc/<pid>/stat   elfbug_api readThreadStat: StatFields, then ParseNumber on the numbered fields
//   /proc/<pid>/status ProcessList: ParseNumber(FindValue(text, "PPid:")) (and /proc/stat "btime")
#include <ElfBug/process/ProcFs.h>
#include <cstddef>
#include <cstdint>
#include <string>

using namespace ElfBug;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    const std::string text(reinterpret_cast<const char*>(data), size);
    volatile uint64_t sink = 0;

    for(const std::string_view line : procfs::Split(text, '\n'))
    {
        const auto entry = procfs::ParseMapsLine(line);
        if(!entry)
            continue;
        sink += entry->start + entry->end + entry->offset + entry->file.Known();
        sink += entry->perms.size() + entry->path.size();
    }

    const auto fields = procfs::StatFields(text);
    for(size_t i = 1; i < fields.size(); i++)
    {
        sink += procfs::ParseNumber<uint64_t>(fields[i]).value_or(0);
        sink += static_cast<uint64_t>(procfs::ParseNumber<int32_t>(fields[i]).value_or(0));
    }

    for(const std::string_view key : {"PPid:", "Tgid:", "Name:", "btime"})
        sink += static_cast<uint64_t>(procfs::ParseNumber<pid_t>(procfs::FindValue(text, key)).value_or(0));
    sink += procfs::StripDeletedSuffix(procfs::Trim(text)).size();

    (void)sink;
    return 0;
}
