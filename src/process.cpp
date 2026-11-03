#include "core.h"
#include <sddl.h>

namespace fli {
static std::wstring imagePath(HANDLE p) {
    std::wstring path(32768, L'\0'); DWORD n = static_cast<DWORD>(path.size());
    if(!QueryFullProcessImageNameW(p, 0, path.data(), &n)) return {};
    path.resize(n); return path;
}
std::wstring protectionReason(HANDLE p, DWORD pid) {
    if(pid <= 4 || pid == GetCurrentProcessId()) return L"System or own process";
    wchar_t windows[32768]{};
    UINT windowsLength = GetWindowsDirectoryW(windows, 32768);
    auto executable = imagePath(p);
    if(!windowsLength || windowsLength >= 32768 || executable.empty()) return L"System image location unavailable";
    std::wstring systemRoot = std::wstring(windows) + L"\\";
    if(_wcsnicmp(executable.c_str(), systemRoot.c_str(), systemRoot.size()) == 0)
        return L"Windows component (conservative protection policy)";
    BOOL critical = TRUE;
    if(!IsProcessCritical(p, &critical) || critical) return L"Critical status unsafe or unavailable";
    PROCESS_PROTECTION_LEVEL_INFORMATION info{};
    if(!GetProcessInformation(p, ProcessProtectionLevelInfo, &info, sizeof(info)) || info.ProtectionLevel != PROTECTION_LEVEL_NONE)
        return L"Protected process or protection status unavailable";
    DWORD session = 0, ownSession = 0;
    if(!ProcessIdToSessionId(pid, &session) || !ProcessIdToSessionId(GetCurrentProcessId(), &ownSession) || !session || session != ownSession)
        return L"System/service or another session";
    Handle token; HANDLE t = nullptr;
    if(!OpenProcessToken(p, TOKEN_QUERY, &t)) return L"Owner unavailable";
    token.reset(t);
    DWORD size = 0; GetTokenInformation(token.get(), TokenUser, nullptr, 0, &size);
    if(!size || size > 65536) return L"Owner unavailable";
    std::vector<BYTE> owner(size);
    if(!GetTokenInformation(token.get(), TokenUser, owner.data(), size, &size)) return L"Owner unavailable";
    HANDLE current = nullptr;
    if(!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &current)) return L"Own token unavailable";
    Handle ownToken(current); DWORD ownSize = 0;
    GetTokenInformation(current, TokenUser, nullptr, 0, &ownSize);
    if(!ownSize || ownSize > 65536) return L"Own token unavailable";
    std::vector<BYTE> own(ownSize);
    if(!GetTokenInformation(current, TokenUser, own.data(), ownSize, &ownSize) ||
       !EqualSid(reinterpret_cast<TOKEN_USER*>(owner.data())->User.Sid, reinterpret_cast<TOKEN_USER*>(own.data())->User.Sid))
        return L"Different user";
    return {};
}
Record processInfo(DWORD pid, uint64_t expected) {
    Record r; r.pid = pid; r.created = expected;
    Handle p(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid));
    if(!p) {
        r.error = GetLastError(); r.state = r.error == ERROR_INVALID_PARAMETER ? State::Exited : State::AccessDenied;
        copy(r.reason, errorText(r.error)); return r;
    }
    auto c = creation(p.get());
    if(!c || (expected && expected != c) || WaitForSingleObject(p.get(), 0) != WAIT_TIMEOUT) {
        r.state = State::Exited; copy(r.reason, L"Process exited or identity changed"); return r;
    }
    r.created = c; r.state = State::Running;
    auto path = imagePath(p.get()); copy(r.path, path); copy(r.name, baseName(path));
    DWORD session = 0; ProcessIdToSessionId(pid, &session);
    copy(r.type, session ? L"Application" : L"System / Service");
    HANDLE t = nullptr;
    if(OpenProcessToken(p.get(), TOKEN_QUERY, &t)) {
        Handle token(t); DWORD n = 0; GetTokenInformation(t, TokenUser, nullptr, 0, &n);
        if(n && n <= 65536) {
            std::vector<BYTE> data(n);
            if(GetTokenInformation(t, TokenUser, data.data(), n, &n)) {
                // SID avoids synchronous domain controller/name lookups in the UI and scanner.
                wchar_t* sid = nullptr;
                if(ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(data.data())->User.Sid, &sid)) {
                    copy(r.user, sid); LocalFree(sid);
                }
            }
        }
    }
    auto reason = protectionReason(p.get(), pid);
    Handle access(OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid));
    if(reason.empty() && (!access || creation(access.get()) != c)) reason = L"Termination access denied or identity changed";
    if(path.empty() && reason.empty()) reason = L"Executable path unavailable";
    r.safe = reason.empty(); copy(r.reason, reason); return r;
}
Handle verifiedProcess(const Record& r, bool force, std::wstring& error) {
    Handle p(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE | (force ? PROCESS_TERMINATE : 0), FALSE, r.pid));
    if(!p) { error = errorText(GetLastError()); return {}; }
    if(!r.created || creation(p.get()) != r.created || WaitForSingleObject(p.get(), 0) != WAIT_TIMEOUT) {
        error = L"Process exited or PID was reused."; return {};
    }
    auto path = imagePath(p.get());
    if(path.empty() || path != r.path || baseName(path) != r.name) { error = L"Process image identity changed or unavailable."; return {}; }
    error = protectionReason(p.get(), r.pid);
    if(!error.empty()) return {};
    return p;
}
struct CloseContext { HANDLE process; DWORD pid; bool posted = false; DWORD error = ERROR_NOT_FOUND; };
static BOOL CALLBACK closeWindow(HWND w, LPARAM param) {
    auto& c = *reinterpret_cast<CloseContext*>(param); DWORD pid = 0; GetWindowThreadProcessId(w, &pid);
    if(pid == c.pid && !GetWindow(w, GW_OWNER) && WaitForSingleObject(c.process, 0) == WAIT_TIMEOUT) {
        // A retained process handle prevents its PID being recycled while windows are enumerated.
        if(PostMessageW(w, WM_CLOSE, 0, 0)) { c.posted = true; c.error = 0; }
        else c.error = GetLastError();
    }
    return TRUE;
}
bool requestClose(HANDLE process, DWORD pid, DWORD& error) {
    CloseContext context{process, pid}; EnumWindows(closeWindow, reinterpret_cast<LPARAM>(&context));
    error = context.error; return context.posted;
}
bool forceTerminate(HANDLE process, DWORD& error) {
    if(!TerminateProcess(process, 1)) { error = GetLastError(); return false; }
    error = ERROR_SUCCESS; return true; // Caller asynchronously verifies signaled process handle.
}
}
