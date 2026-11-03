#pragma once
#include "common.h"
#include <functional>

namespace fli {
enum class Method : uint32_t { RestartManager = 1, Advanced = 2 };
enum class State : uint32_t { Running, Exited, AccessDenied, Unknown };
// Fixed-size, bounded, versioned local pipe protocol. No pointers or ownership cross processes.
struct Record {
    uint32_t magic = 0x464c4931, kind = 1;
    DWORD pid = 0, error = 0;
    uint64_t created = 0;
    Method method = Method::RestartManager;
    State state = State::Unknown;
    uint32_t confirmed = 0, safe = 0, skipped = 0;
    wchar_t name[260]{}, path[32768]{}, user[256]{}, type[32]{}, reason[256]{};
};
using Sink = std::function<void(const Record&)>;
struct Identity {
    FILE_ID_INFO id{};
    bool valid = false;
    explicit Identity(HANDLE h) : valid(GetFileInformationByHandleEx(h, FileIdInfo, &id, sizeof(id)) != FALSE) {}
    bool equals(const Identity& o) const {
        return valid && o.valid && id.VolumeSerialNumber == o.id.VolumeSerialNumber &&
            memcmp(id.FileId.Identifier, o.id.FileId.Identifier, sizeof(id.FileId.Identifier)) == 0;
    }
};
// A volume GUID disambiguates serial-number collisions and explicitly excludes remote providers.
std::wstring localVolume(HANDLE file);
template<size_t N> void copy(wchar_t (&dst)[N], const std::wstring& src) {
    wcsncpy_s(dst, N, src.c_str(), _TRUNCATE);
}
Record processInfo(DWORD pid, uint64_t expected = 0);
void restartScan(const std::wstring& path, const Sink& sink);
void advancedScan(const std::wstring& path, const Sink& sink);
std::wstring protectionReason(HANDLE process, DWORD pid);
// Returns a verified handle retained across confirmation and action to prevent PID reuse.
Handle verifiedProcess(const Record& record, bool force, std::wstring& error);
bool requestClose(HANDLE process, DWORD pid, DWORD& error);
bool forceTerminate(HANDLE process, DWORD& error);
}
