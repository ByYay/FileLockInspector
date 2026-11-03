#include "scan_job.h"
#include <shellapi.h>
#include <iostream>
#include <set>

using namespace fli;
namespace {
int failures = 0;
void check(bool condition, const char* name) { std::cout << (condition ? "PASS " : "FAIL ") << name << '\n'; if(!condition) ++failures; }
struct Child {
    Handle process;
    DWORD pid = 0;
    Child(const std::wstring& path, HANDLE ready, bool gui = false) {
        STARTUPINFOW si{sizeof(si)}; PROCESS_INFORMATION pi{};
        auto exe = modulePath(); auto cmd = quote(exe) + (gui ? L" --guihold " : L" --hold ") + quote(path) + L" " + std::to_wstring(reinterpret_cast<ULONG_PTR>(ready));
        if(CreateProcessW(exe.c_str(),cmd.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi)) {
            process.reset(pi.hProcess); pid = pi.dwProcessId; CloseHandle(pi.hThread);
        }
    }
    ~Child() { if(process && WaitForSingleObject(process.get(),0) == WAIT_TIMEOUT) { TerminateProcess(process.get(),0); WaitForSingleObject(process.get(),5000); } }
};
std::vector<Record> scan(const std::wstring& path, bool advanced, Metrics* metrics = nullptr) {
    ScanJob job; std::vector<Record> rows;
    check(job.start(path,advanced), "worker starts");
    while(job.active()) { job.poll([&](const Record& r) { if(r.kind == 1) rows.push_back(r); }); Sleep(10); }
    check(job.error == 0, "scan completes without error");
    if(metrics) *metrics = job.metrics;
    return rows;
}
bool contains(const std::vector<Record>& rows, DWORD pid) { for(auto& r : rows) if(r.pid == pid) return true; return false; }
LRESULT CALLBACK fixtureWindow(HWND h, UINT m, WPARAM w, LPARAM l) {
    if(m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(h,m,w,l);
}
}
int wmain(int argc, wchar_t** argv) {
    if(argc == 4 && (wcscmp(argv[1],L"--hold") == 0 || wcscmp(argv[1],L"--guihold") == 0)) {
        Handle file(CreateFileW(extended(argv[2]).c_str(),GENERIC_READ,FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr));
        if(!file) return 10;
        Handle ready(reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(_wcstoui64(argv[3],nullptr,10))));
        if(wcscmp(argv[1],L"--guihold") == 0) {
            WNDCLASSW cls{}; cls.hInstance = GetModuleHandleW(nullptr); cls.lpfnWndProc = fixtureWindow; cls.lpszClassName = L"FLI.TestHolder";
            if(!RegisterClassW(&cls)) return 12;
            HWND window = CreateWindowW(cls.lpszClassName,L"FLI owned test holder",WS_OVERLAPPED,0,0,100,100,nullptr,nullptr,cls.hInstance,nullptr);
            if(!window) return 13;
            if(!SetEvent(ready.get())) return 11;
            MSG msg{}; while(GetMessageW(&msg,nullptr,0,0)>0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
            return 0;
        }
        if(!SetEvent(ready.get())) return 11;
        Sleep(INFINITE); return 0;
    }
    // Timeout fixture: ScanJob runs this executable as if it were the scanner.
    if(argc == 4 && wcscmp(argv[1],L"--pipe") == 0) {
        Record record; record.pid = GetCurrentProcessId(); DWORD written = 0;
        if(!WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),&record,sizeof(record),&written,nullptr) || written != sizeof(record)) return 14;
        Sleep(INFINITE); return 0;
    }
    for(const auto& arg : std::vector<std::wstring>{L"",L"a b",L"C:\\",L"C:\\Örnek Dosya ğüşi.txt",L"a\"b",L"\\\\server\\share\\",L"x\\\\\"y"}) {
        auto command = quote(L"test.exe") + L" " + quote(arg); int count = 0;
        auto parsed = CommandLineToArgvW(command.c_str(),&count);
        check(parsed && count == 2 && arg == parsed[1], "argument quote round trip"); if(parsed) LocalFree(parsed);
    }
    bool rejected = false; try { fullPath(L"\\\\.\\PhysicalDrive0"); } catch(...) { rejected = true; }
    check(rejected,"device path rejected");
    auto own = processInfo(GetCurrentProcessId()); check(!own.safe,"self termination prohibited");
    check(!processInfo(4).safe,"system process termination prohibited without action");
    std::wstring error; check(!verifiedProcess(own,true,error),"verified self termination prohibited");
    wchar_t temp[32768]; DWORD n = GetTempPathW(32768,temp);
    if(!n || n >= 32768) return 1;
    auto directory = std::wstring(temp) + L"FLI-tests-" + std::to_wstring(GetCurrentProcessId());
    check(CreateDirectoryW(directory.c_str(),nullptr) != FALSE,"test directory created");
    auto path = directory + L"\\Örnek Dosya ğüşi.txt";
    Handle file(CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));
    check(bool(file),"unicode fixture created"); file.reset();
    SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};
    Handle ready(CreateEventW(&sa,TRUE,FALSE,nullptr));
    {
        Child first(path,ready.get()); check(first.pid && WaitForSingleObject(ready.get(),5000) == WAIT_OBJECT_0,"first holder ready");
        ResetEvent(ready.get());
        Child second(path,ready.get()); check(second.pid && WaitForSingleObject(ready.get(),5000) == WAIT_OBJECT_0,"second holder ready");
        Metrics normal{}, advanced{};
        auto rows = scan(path,false,&normal);
        check(contains(rows,first.pid) && contains(rows,second.pid),"Restart Manager finds both holders");
        rows = scan(path,true,&advanced);
        check(contains(rows,first.pid) && contains(rows,second.pid),"Advanced finds both holders");
        auto hardlink = directory + L"\\hardlink";
        if(CreateHardLinkW(hardlink.c_str(),path.c_str(),nullptr)) {
            rows = scan(hardlink,true); check(contains(rows,first.pid),"hard link identity matches"); DeleteFileW(hardlink.c_str());
        } else { check(false,"hard link fixture requires supported filesystem"); }
        auto record = processInfo(first.pid); auto stale = record; ++stale.created;
        check(!verifiedProcess(stale,true,error),"stale creation time rejected");
        auto wrongPath = record; copy(wrongPath.path,L"C:\\wrong.exe");
        check(!verifiedProcess(wrongPath,true,error),"changed executable rejected");
        auto handle = verifiedProcess(record,true,error); check(bool(handle),"test child passes protection checks");
        if(handle) {
            DWORD code = 0; check(forceTerminate(handle.get(),code),"explicit test-owned child termination");
            check(WaitForSingleObject(handle.get(),5000) == WAIT_OBJECT_0,"termination confirmed");
        }
        rows = scan(path,false); check(!contains(rows,first.pid) && contains(rows,second.pid),"refresh retains surviving holder");
        std::cout << "PERF normal elapsed_ms=" << normal.elapsedMs << " cpu_ms=" << normal.cpu100ns/10000 << " peak_KiB=" << normal.peakWorkingSet/1024 << '\n';
        std::cout << "PERF advanced elapsed_ms=" << advanced.elapsedMs << " cpu_ms=" << advanced.cpu100ns/10000 << " peak_KiB=" << advanced.peakWorkingSet/1024 << '\n';
    }
    auto empty = scan(path,false); check(empty.empty(),"no known holders (not proof file is unused)");
    {
        ResetEvent(ready.get()); Child gui(path,ready.get(),true);
        check(gui.pid && WaitForSingleObject(ready.get(),5000) == WAIT_OBJECT_0,"GUI holder ready");
        auto record = processInfo(gui.pid); auto handle = verifiedProcess(record,false,error); DWORD code = 0;
        check(bool(handle),"GUI child identity verified");
        if(handle) {
            check(requestClose(handle.get(),gui.pid,code),"WM_CLOSE posted to test-owned window");
            check(WaitForSingleObject(handle.get(),5000) == WAIT_OBJECT_0,"graceful close confirmed");
            check(!verifiedProcess(record,true,error),"exited process rejected");
        }
    }
    ScanJob folder; check(folder.start(directory,false),"folder scan starts");
    while(folder.active()) { folder.poll([](const Record&){}); Sleep(10); }
    check(folder.error == ERROR_DIRECTORY,"folder normal scan explicitly limited");
    ScanJob missing; check(missing.start(path + L"missing",false),"missing file worker starts");
    while(missing.active()) { missing.poll([](const Record&){}); Sleep(10); }
    check(missing.error == ERROR_FILE_NOT_FOUND,"missing file correctly reported");
    DWORD before = 0, after = 0; GetProcessHandleCount(GetCurrentProcess(),&before);
    for(int i = 0; i < 10; ++i) {
        ScanJob cancel; check(cancel.start(path,true),"cancel worker starts");
        Handle child(OpenProcess(SYNCHRONIZE,FALSE,cancel.processId()));
        cancel.cancel(); check(!cancel.active(),"cancel releases handles");
        check(child && WaitForSingleObject(child.get(),5000) == WAIT_OBJECT_0,"cancelled worker actually exits");
    }
    GetProcessHandleCount(GetCurrentProcess(),&after); check(after <= before + 2,"repeated cancellation does not leak parent handles");
    ScanJob timeout; check(timeout.start(path,true,modulePath()),"timeout fixture starts");
    ULONGLONG start = GetTickCount64(); unsigned partial = 0;
    Handle timeoutChild(OpenProcess(SYNCHRONIZE,FALSE,timeout.processId()));
    while(timeout.active()) { timeout.poll([&](const Record& r) { if(r.kind == 1) ++partial; }); Sleep(10); }
    check(timeout.error == WAIT_TIMEOUT && timeout.incomplete && GetTickCount64()-start < ScanJob::timeoutMs+3000,"hung worker bounded and incomplete");
    check(partial == 1,"partial result delivered before timeout");
    check(timeoutChild && WaitForSingleObject(timeoutChild.get(),5000) == WAIT_OBJECT_0,"timed out worker actually exits");
    DeleteFileW(path.c_str()); RemoveDirectoryW(directory.c_str());
    return failures ? 1 : 0;
}
