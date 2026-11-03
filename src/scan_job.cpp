#include "scan_job.h"
#include <psapi.h>

namespace fli {
class Attributes {
    std::vector<BYTE> storage_;
    LPPROC_THREAD_ATTRIBUTE_LIST list_ = nullptr;
public:
    Attributes() {
        SIZE_T bytes = 0; InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
        storage_.resize(bytes); auto list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage_.data());
        if(InitializeProcThreadAttributeList(list, 1, 0, &bytes)) list_ = list;
    }
    ~Attributes() { if(list_) DeleteProcThreadAttributeList(list_); }
    LPPROC_THREAD_ATTRIBUTE_LIST get() const { return list_; }
};
bool ScanJob::start(const std::wstring& path, bool advanced, const std::wstring& executable) {
    cancel(); pending_.clear(); error = 0; incomplete = false; doneMessage_ = false; metrics = {};
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE}; HANDLE r = nullptr, w = nullptr;
    if(!CreatePipe(&r, &w, &sa, 65536)) { error = GetLastError(); return false; }
    read_.reset(r); Handle write(w);
    if(!SetHandleInformation(r, HANDLE_FLAG_INHERIT, 0)) { error = GetLastError(); read_.reset(); return false; }
    job_.reset(CreateJobObjectW(nullptr, nullptr));
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_ACTIVE_PROCESS;
    limits.BasicLimitInformation.ActiveProcessLimit = 1;
    if(!job_ || !SetInformationJobObject(job_.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
        error = GetLastError(); cancel(); return false;
    }
    Attributes attrs; HANDLE inherited[] = {write.get()};
    if(!attrs.get() || !UpdateProcThreadAttribute(attrs.get(), 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, sizeof(inherited), nullptr, nullptr)) {
        error = GetLastError(); cancel(); return false;
    }
    STARTUPINFOEXW si{}; si.StartupInfo.cb = sizeof(si); si.lpAttributeList = attrs.get();
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdOutput = write.get(); si.StartupInfo.hStdError = write.get();
    // stdin is unused. Only the output pipe can be inherited.
    si.StartupInfo.hStdInput = nullptr;
    PROCESS_INFORMATION pi{};
    auto cmd = quote(executable) + (advanced ? L" --pipe advanced " : L" --pipe normal ") + quote(path);
    if(cmd.size() >= 32767) { error = ERROR_FILENAME_EXCED_RANGE; cancel(); return false; }
    if(!CreateProcessW(executable.c_str(), cmd.data(), nullptr, nullptr, TRUE,
        CREATE_SUSPENDED | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr, &si.StartupInfo, &pi)) {
        error = GetLastError(); cancel(); return false;
    }
    process_.reset(pi.hProcess); Handle thread(pi.hThread); started_ = GetTickCount64();
    if(!AssignProcessToJobObject(job_.get(), process_.get())) {
        error = GetLastError(); TerminateProcess(process_.get(), error); cancel(); return false;
    }
    if(ResumeThread(thread.get()) == DWORD(-1)) { error = GetLastError(); cancel(); return false; }
    return true;
}
void ScanJob::collectMetrics() {
    if(!process_) return;
    metrics.elapsedMs = GetTickCount64() - started_;
    FILETIME c{}, e{}, k{}, u{};
    if(GetProcessTimes(process_.get(), &c, &e, &k, &u)) metrics.cpu100ns = stamp(k) + stamp(u);
    PROCESS_MEMORY_COUNTERS pm{}; pm.cb = sizeof(pm);
    if(GetProcessMemoryInfo(process_.get(), &pm, sizeof(pm))) metrics.peakWorkingSet = pm.PeakWorkingSetSize;
}
void ScanJob::cancel() {
    if(process_) { collectMetrics(); incomplete = true; }
    job_.reset(); // Kill-on-close owns even a blocked native query; never touches inspected processes.
    process_.reset(); read_.reset(); pending_.clear();
}
void ScanJob::poll(const Sink& sink) {
    if(!process_) return;
    // Bound work per UI tick, including when the worker floods results.
    for(unsigned chunk = 0; chunk < 32; ++chunk) {
        DWORD available = 0;
        if(!PeekNamedPipe(read_.get(), nullptr, 0, nullptr, &available, nullptr)) {
            DWORD e = GetLastError();
            if(e != ERROR_BROKEN_PIPE) { error = e; cancel(); return; }
            break;
        }
        if(!available) break;
        BYTE bytes[8192]; DWORD n = 0;
        if(!ReadFile(read_.get(), bytes, (std::min)(available, DWORD(sizeof(bytes))), &n, nullptr)) {
            error = GetLastError(); cancel(); return;
        }
        pending_.insert(pending_.end(), bytes, bytes + n);
        while(pending_.size() >= sizeof(Record)) {
            Record record; memcpy(&record, pending_.data(), sizeof(record));
            pending_.erase(pending_.begin(), pending_.begin() + sizeof(record));
            if(record.magic != 0x464c4931 || (record.kind != 1 && record.kind != 2) || doneMessage_ ||
               record.path[32767] || record.name[259] || record.user[255] || record.type[31] || record.reason[255]) {
                error = ERROR_INVALID_DATA; cancel(); return;
            }
            if(record.kind == 2) { doneMessage_ = true; error = record.error; incomplete = record.error || record.skipped; }
            sink(record);
        }
    }
    if(WaitForSingleObject(process_.get(), 0) == WAIT_OBJECT_0) {
        DWORD available = 0;
        if(PeekNamedPipe(read_.get(), nullptr, 0, nullptr, &available, nullptr) && available) return;
        DWORD exitCode = 0; GetExitCodeProcess(process_.get(), &exitCode);
        if(!doneMessage_ || !pending_.empty() || exitCode) { incomplete = true; if(!error) error = exitCode ? exitCode : ERROR_INVALID_DATA; }
        collectMetrics(); process_.reset(); read_.reset(); job_.reset(); return;
    }
    if(GetTickCount64() - started_ >= timeoutMs) { error = WAIT_TIMEOUT; cancel(); }
}
}
