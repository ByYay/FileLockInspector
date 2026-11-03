#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <stdexcept>
#include <utility>
#include <cstdint>
#include <algorithm>

namespace fli {
class Handle {
    HANDLE value_ = nullptr;
public:
    Handle() = default;
    explicit Handle(HANDLE h) : value_(h) {}
    ~Handle() { reset(); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& h) noexcept : value_(h.release()) {}
    Handle& operator=(Handle&& h) noexcept { if(this != &h) reset(h.release()); return *this; }
    explicit operator bool() const { return value_ && value_ != INVALID_HANDLE_VALUE; }
    HANDLE get() const { return value_; }
    HANDLE release() { return std::exchange(value_, nullptr); }
    void reset(HANDLE h = nullptr) { if(*this) CloseHandle(value_); value_ = h; }
};
inline uint64_t stamp(FILETIME t) { return (uint64_t(t.dwHighDateTime) << 32) | t.dwLowDateTime; }
inline uint64_t creation(HANDLE p) {
    FILETIME c{}, e{}, k{}, u{};
    return GetProcessTimes(p, &c, &e, &k, &u) ? stamp(c) : 0;
}
inline std::wstring errorText(DWORD code) {
    wchar_t* p = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, reinterpret_cast<wchar_t*>(&p), 0, nullptr);
    std::wstring result = p ? p : L"Unknown Windows error";
    if(p) LocalFree(p);
    return result + L" (" + std::to_wstring(code) + L")";
}
// Windows CRT/CommandLineToArgvW quoting, including trailing backslashes.
inline std::wstring quote(const std::wstring& s) {
    std::wstring out = L"\""; size_t slashes = 0;
    for(wchar_t c : s) {
        if(c == L'\\') { ++slashes; continue; }
        out.append(slashes * (c == L'\"' ? 2 : 1), L'\\');
        if(c == L'\"') out += L'\\';
        out += c; slashes = 0;
    }
    out.append(slashes * 2, L'\\'); return out + L'\"';
}
inline std::wstring modulePath(HMODULE module = nullptr) {
    std::wstring p(32768, L'\0');
    DWORD n = GetModuleFileNameW(module, p.data(), static_cast<DWORD>(p.size()));
    if(!n || n >= p.size()) throw std::runtime_error("Module path unavailable");
    p.resize(n); return p;
}
inline std::wstring sibling(const wchar_t* name) {
    auto p = modulePath(); return p.substr(0, p.find_last_of(L'\\') + 1) + name;
}
inline std::wstring baseName(const std::wstring& p) { return p.substr(p.find_last_of(L"\\/") + 1); }
inline std::wstring fullPath(const std::wstring& input) {
    if(input.empty()) throw std::runtime_error("Empty path");
    std::wstring p(32768, L'\0');
    DWORD n = GetFullPathNameW(input.c_str(), static_cast<DWORD>(p.size()), p.data(), nullptr);
    if(!n || n >= p.size()) throw std::runtime_error("Invalid or oversized path");
    p.resize(n);
    // Accept filesystem paths, never device namespaces or alternate data streams.
    if(p.rfind(L"\\\\.\\", 0) == 0 || p.rfind(L"\\\\?\\GLOBALROOT", 0) == 0)
        throw std::runtime_error("Device paths are unsupported");
    if(p.rfind(L"\\\\?\\UNC\\", 0) == 0) p = L"\\\\" + p.substr(8);
    else if(p.rfind(L"\\\\?\\", 0) == 0) p = p.substr(4);
    bool drive = p.size() > 2 && p[1] == L':' && p[2] == L'\\';
    if(!drive && p.rfind(L"\\\\", 0) != 0) throw std::runtime_error("Filesystem path required");
    if(p.find(L':', drive ? 2 : 0) != std::wstring::npos) throw std::runtime_error("Alternate streams unsupported");
    return p;
}
inline std::wstring extended(const std::wstring& p) {
    if(p.rfind(L"\\\\?\\", 0) == 0) return p;
    return p.rfind(L"\\\\", 0) == 0 ? L"\\\\?\\UNC\\" + p.substr(2) : L"\\\\?\\" + p;
}
}
