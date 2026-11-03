#include "common.h"
#include <shobjidl.h>
#include <shlwapi.h>
#include <atomic>
#include <new>

namespace {
// {2F0DA1C3-406A-493B-93AA-6F408546812A}
constexpr CLSID CommandId{0x2f0da1c3,0x406a,0x493b,{0x93,0xaa,0x6f,0x40,0x85,0x46,0x81,0x2a}};
HMODULE module = nullptr;
std::atomic<long> objects{0}, locks{0};
class Command final : public IExplorerCommand {
    std::atomic<ULONG> refs_{1};
public:
    Command() { ++objects; }
    ~Command() { --objects; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if(!out) return E_POINTER; *out = nullptr;
        if(iid == IID_IUnknown || iid == IID_IExplorerCommand) { *out = static_cast<IExplorerCommand*>(this); AddRef(); return S_OK; }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG n = --refs_; if(!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE GetTitle(IShellItemArray*, PWSTR* title) override { return SHStrDupW(L"Who is using this file?", title); }
    HRESULT STDMETHODCALLTYPE GetIcon(IShellItemArray*, PWSTR* icon) override { if(!icon) return E_POINTER; *icon = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetToolTip(IShellItemArray*, PWSTR* text) override { return SHStrDupW(L"Inspect one file or folder on demand", text); }
    HRESULT STDMETHODCALLTYPE GetCanonicalName(GUID* id) override { if(!id) return E_POINTER; *id = CommandId; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetState(IShellItemArray* items, BOOL, EXPCMDSTATE* state) override {
        if(!state) return E_POINTER; DWORD count = 0;
        *state = items && SUCCEEDED(items->GetCount(&count)) && count == 1 ? ECS_ENABLED : ECS_HIDDEN;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Invoke(IShellItemArray* items, IBindCtx*) override {
        DWORD count = 0;
        if(!items || FAILED(items->GetCount(&count)) || count != 1) return E_INVALIDARG;
        IShellItem* item = nullptr; HRESULT hr = items->GetItemAt(0, &item);
        if(FAILED(hr)) return hr;
        PWSTR path = nullptr; hr = item->GetDisplayName(SIGDN_FILESYSPATH, &path); item->Release();
        if(FAILED(hr)) return hr;
        try {
            auto dll = fli::modulePath(module);
            auto exe = dll.substr(0, dll.find_last_of(L'\\') + 1) + L"FileLockInspector.exe";
            auto command = fli::quote(exe) + L" " + fli::quote(path);
            CoTaskMemFree(path); path = nullptr;
            if(command.size() >= 32767) return HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE);
            STARTUPINFOW si{sizeof(si)}; PROCESS_INFORMATION pi{};
            if(!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) return HRESULT_FROM_WIN32(GetLastError());
            CloseHandle(pi.hThread); CloseHandle(pi.hProcess); return S_OK;
        } catch(...) { CoTaskMemFree(path); return E_OUTOFMEMORY; }
    }
    HRESULT STDMETHODCALLTYPE GetFlags(EXPCMDFLAGS* flags) override { if(!flags) return E_POINTER; *flags = ECF_DEFAULT; return S_OK; }
    HRESULT STDMETHODCALLTYPE EnumSubCommands(IEnumExplorerCommand** out) override { if(!out) return E_POINTER; *out = nullptr; return E_NOTIMPL; }
};
class Factory final : public IClassFactory {
    std::atomic<ULONG> refs_{1};
public:
    Factory() { ++objects; }
    ~Factory() { --objects; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if(!out) return E_POINTER; *out = nullptr;
        if(iid == IID_IUnknown || iid == IID_IClassFactory) { *out = static_cast<IClassFactory*>(this); AddRef(); return S_OK; }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG n = --refs_; if(!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID iid, void** out) override {
        if(!out) return E_POINTER; *out = nullptr;
        if(outer) return CLASS_E_NOAGGREGATION;
        auto command = new(std::nothrow) Command; if(!command) return E_OUTOFMEMORY;
        HRESULT hr = command->QueryInterface(iid,out); command->Release(); return hr;
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override { if(lock) ++locks; else --locks; return S_OK; }
};
}
extern "C" HRESULT __stdcall DllGetClassObject(REFCLSID clsid, REFIID iid, void** out) {
    if(!out) return E_POINTER; *out = nullptr;
    if(clsid != CommandId) return CLASS_E_CLASSNOTAVAILABLE;
    auto factory = new(std::nothrow) Factory; if(!factory) return E_OUTOFMEMORY;
    HRESULT hr = factory->QueryInterface(iid,out); factory->Release(); return hr;
}
extern "C" HRESULT __stdcall DllCanUnloadNow() { return objects == 0 && locks == 0 ? S_OK : S_FALSE; }
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    // Static CRT requires thread notifications; do not disable them.
    if(reason == DLL_PROCESS_ATTACH) module = instance;
    return TRUE;
}
