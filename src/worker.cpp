#include "scan_job.h"
#include <iostream>
#include <fcntl.h>
#include <io.h>

int wmain(int argc, wchar_t** argv) {
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    if(argc != 4 || (wcscmp(argv[1], L"--pipe") && wcscmp(argv[1], L"--scan")) ||
       (wcscmp(argv[2], L"normal") && wcscmp(argv[2], L"advanced"))) return ERROR_BAD_ARGUMENTS;
    bool binary = wcscmp(argv[1], L"--pipe") == 0;
    if(!binary) _setmode(_fileno(stdout), _O_U16TEXT);
    DWORD result = 0;
    auto sink = [&](const fli::Record& record) {
        if(binary) {
            const BYTE* p = reinterpret_cast<const BYTE*>(&record); DWORD left = sizeof(record);
            while(left) {
                DWORD n = 0;
                if(!WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), p, left, &n, nullptr) || !n) throw std::runtime_error("Pipe closed");
                p += n; left -= n;
            }
        } else if(record.kind == 1) {
            std::wcout << record.pid << L"\t" << record.created << L"\t" << record.name << L"\t" << record.path
                << L"\t" << (record.confirmed ? L"Confirmed file identity" : L"Possible (Restart Manager)") << L"\n";
        } else {
            std::wcout << L"Scan ended: " << record.error << L"; skipped handles: " << record.skipped << L"\n";
        }
        if(record.kind == 2) result = record.error;
    };
    try {
        auto path = fli::fullPath(argv[3]);
        bool advanced = wcscmp(argv[2], L"advanced") == 0;
        if(!binary) {
            fli::ScanJob scan;
            if(!scan.start(path, advanced, fli::modulePath())) return static_cast<int>(scan.error);
            while(scan.active()) { scan.poll(sink); Sleep(10); }
            std::wcout << L"Elapsed ms: " << scan.metrics.elapsedMs << L"; CPU ms: " << scan.metrics.cpu100ns / 10000
                << L"; peak KiB: " << scan.metrics.peakWorkingSet / 1024 << L"; incomplete: " << scan.incomplete << L"\n";
            return static_cast<int>(scan.error);
        }
        if(advanced) fli::advancedScan(path, sink); else fli::restartScan(path, sink);
    } catch(...) { return ERROR_UNHANDLED_EXCEPTION; }
    return static_cast<int>(result);
}
