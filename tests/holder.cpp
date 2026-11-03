#include "common.h"
#include <iostream>
int wmain(int argc, wchar_t** argv) {
    if(argc != 3) return ERROR_BAD_ARGUMENTS;
    fli::Handle ready(OpenEventW(EVENT_MODIFY_STATE, FALSE, argv[2]));
    if(!ready) return static_cast<int>(GetLastError());
    fli::Handle file(CreateFileW(argv[1], GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if(!file) return static_cast<int>(GetLastError());
    if(!SetEvent(ready.get())) return static_cast<int>(GetLastError());
    std::wcout << L"File Lock Inspector test holder\nPID: " << GetCurrentProcessId()
        << L"\nFile: " << argv[1] << L"\nPress Enter to release the file and exit.\n";
    std::cin.get();
    return 0;
}
