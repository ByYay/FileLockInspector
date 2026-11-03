#include "core.h"
#include <restartmanager.h>
#include <winternl.h>
#include <set>

namespace fli {
std::wstring localVolume(HANDLE file) {
    std::wstring path(32768, L'\0');
    DWORD n = GetFinalPathNameByHandleW(file, path.data(), static_cast<DWORD>(path.size()), FILE_NAME_OPENED | VOLUME_NAME_GUID);
    if(!n || n >= path.size()) return {};
    path.resize(n);
    if(path.rfind(L"\\\\?\\Volume{", 0) != 0) return {};
    auto end = path.find(L'}');
    return end == std::wstring::npos ? std::wstring{} : path.substr(0, end + 1);
}
static void finish(const Sink& sink, Method method, DWORD error, uint32_t skipped = 0) {
    Record r; r.kind = 2; r.method = method; r.error = error; r.skipped = skipped; sink(r);
}
class RmSession {
public:
    DWORD id = 0, error; bool active = false;
    RmSession() { wchar_t key[CCH_RM_SESSION_KEY + 1]{}; error = RmStartSession(&id, 0, key); active = error == ERROR_SUCCESS; }
    ~RmSession() { if(active) RmEndSession(id); }
    RmSession(const RmSession&) = delete;
    RmSession& operator=(const RmSession&) = delete;
};
void restartScan(const std::wstring& path, const Sink& sink) {
    DWORD attrs = GetFileAttributesW(extended(path).c_str());
    if(attrs == INVALID_FILE_ATTRIBUTES) { finish(sink, Method::RestartManager, GetLastError()); return; }
    if(attrs & FILE_ATTRIBUTE_DIRECTORY) { finish(sink, Method::RestartManager, ERROR_DIRECTORY); return; }
    RmSession session;
    if(!session.active) { finish(sink, Method::RestartManager, session.error); return; }
    const wchar_t* resource = path.c_str();
    DWORD error = RmRegisterResources(session.id, 1, &resource, 0, nullptr, 0, nullptr);
    if(error) { finish(sink, Method::RestartManager, error); return; }
    std::vector<RM_PROCESS_INFO> processes;
    for(unsigned retry = 0; retry < 8; ++retry) {
        UINT needed = 0, count = static_cast<UINT>(processes.size()); DWORD reasons = 0;
        error = RmGetList(session.id, &needed, &count, processes.empty() ? nullptr : processes.data(), &reasons);
        if(error == ERROR_MORE_DATA) {
            if(needed > 65536) { error = ERROR_NOT_ENOUGH_MEMORY; break; }
            processes.resize(needed); continue;
        }
        if(!error) {
            for(UINT i = 0; i < count; ++i) {
                const auto& entry = processes[i]; auto r = processInfo(entry.Process.dwProcessId, stamp(entry.Process.ProcessStartTime));
                if(!r.name[0]) copy(r.name, entry.strAppName);
                if(entry.ApplicationType == RmService) { copy(r.type, L"Service"); r.safe = 0; copy(r.reason,L"Service termination is disabled"); }
                r.method = Method::RestartManager; r.confirmed = 0; sink(r);
            }
        }
        break;
    }
    finish(sink, Method::RestartManager, error);
}
// Native ABI is deliberately isolated here; class 64 is not a stable Win32 contract.
struct NativeHandle { void* object; ULONG_PTR pid, value; ULONG access; USHORT trace, type; ULONG attributes, reserved; };
struct NativeHeader { ULONG_PTR count, reserved; };
static_assert(sizeof(NativeHandle) == 40 && sizeof(NativeHeader) == 16, "x64 Native API layout");
void advancedScan(const std::wstring& path, const Sink& sink) {
    const auto method = Method::Advanced;
    Handle target(CreateFileW(extended(path).c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
    if(!target) { finish(sink, method, GetLastError()); return; }
    Identity identity(target.get());
    if(!identity.valid) { finish(sink, method, ERROR_NOT_SUPPORTED); return; }
    const auto volume = localVolume(target.get());
    if(volume.empty()) { finish(sink, method, ERROR_NOT_SUPPORTED); return; }
    using Query = LONG (NTAPI*)(ULONG, void*, ULONG, ULONG*);
    auto query = reinterpret_cast<Query>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQuerySystemInformation"));
    if(!query) { finish(sink, method, ERROR_NOT_SUPPORTED); return; }
    constexpr ULONG maxBytes = 64 * 1024 * 1024;
    std::vector<BYTE> buffer(1024 * 1024); LONG status = 0;
    for(;;) {
        ULONG needed = 0; status = query(64, buffer.data(), static_cast<ULONG>(buffer.size()), &needed);
        if(status != static_cast<LONG>(0xC0000004L)) break;
        size_t next = (std::max)(size_t(needed) + 65536, buffer.size() * 2);
        if(next > maxBytes) { finish(sink, method, ERROR_NOT_ENOUGH_MEMORY); return; }
        buffer.resize(next);
    }
    if(status < 0) { finish(sink, method, ERROR_NOT_SUPPORTED); return; }
    auto header = reinterpret_cast<const NativeHeader*>(buffer.data());
    if(header->count > (buffer.size() - sizeof(NativeHeader)) / sizeof(NativeHandle)) {
        finish(sink, method, ERROR_INVALID_DATA); return;
    }
    auto handles = reinterpret_cast<const NativeHandle*>(buffer.data() + sizeof(NativeHeader));
    USHORT fileType = 0;
    for(size_t i = 0; i < header->count; ++i)
        if(handles[i].pid == GetCurrentProcessId() && handles[i].value == reinterpret_cast<ULONG_PTR>(target.get())) fileType = handles[i].type;
    if(!fileType) { finish(sink, method, ERROR_NOT_SUPPORTED); return; }
    uint32_t skipped = 0; std::set<std::pair<DWORD,uint64_t>> found;
    // Query only File objects. No NtQueryObject name queries (pipes can hang indefinitely).
    for(size_t i = 0; i < header->count; ++i) {
        const auto& item = handles[i];
        if(item.type != fileType || item.pid == GetCurrentProcessId()) continue;
        if(item.pid > MAXDWORD) { ++skipped; continue; }
        DWORD pid = static_cast<DWORD>(item.pid);
        Handle process(OpenProcess(PROCESS_DUP_HANDLE | PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid));
        if(!process) { ++skipped; continue; }
        uint64_t created = creation(process.get());
        if(!created || WaitForSingleObject(process.get(), 0) != WAIT_TIMEOUT) { ++skipped; continue; }
        HANDLE dup = nullptr;
        if(!DuplicateHandle(process.get(), reinterpret_cast<HANDLE>(item.value), GetCurrentProcess(), &dup, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
            ++skipped; continue;
        }
        Handle handle(dup);
        if(GetFileType(handle.get()) != FILE_TYPE_DISK) continue;
        Identity candidate(handle.get());
        if(!candidate.valid) { ++skipped; continue; }
        if(identity.equals(candidate)) {
            auto candidateVolume = localVolume(handle.get());
            if(candidateVolume.empty()) { ++skipped; continue; }
            if(_wcsicmp(volume.c_str(), candidateVolume.c_str()) != 0 || !found.emplace(pid, created).second) continue;
            auto record = processInfo(pid, created); record.method = method; record.confirmed = 1; sink(record);
        }
    }
    // Even a completed scan is a transient snapshot; inaccessible handles remain unknown.
    finish(sink, method, ERROR_SUCCESS, skipped);
}
}
