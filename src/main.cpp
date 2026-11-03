#include "ui.h"
#include <shellapi.h>
#include <commctrl.h>
#include <objbase.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
    HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES}; InitCommonControlsEx(&controls);
    int argc = 0; auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    int result = 1;
    try {
        if(!argv || argc != 2) {
            MessageBoxW(nullptr, L"Select one file or folder using Explorer, or run:\nFileLockInspector.exe \"C:\\path\\file\"\n\nMultiple selections are not supported. Folders: Advanced Scan checks only the folder itself, never its contents.", L"File Lock Inspector", MB_OK | MB_ICONINFORMATION);
        } else result = fli::runUi(instance, fli::fullPath(argv[1]), show);
    } catch(...) { MessageBoxW(nullptr, L"Unable to open the requested path or initialize the application.", L"File Lock Inspector", MB_OK | MB_ICONERROR); }
    if(argv) LocalFree(argv);
    if(SUCCEEDED(com)) CoUninitialize();
    return result;
}
