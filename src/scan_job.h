#pragma once
#include "core.h"

namespace fli {
struct Metrics { uint64_t elapsedMs = 0, cpu100ns = 0; size_t peakWorkingSet = 0; };
class ScanJob {
    Handle job_, process_, read_;
    std::vector<BYTE> pending_;
    ULONGLONG started_ = 0;
    bool doneMessage_ = false;
    void collectMetrics();
public:
    static constexpr DWORD timeoutMs = 20000;
    Metrics metrics;
    DWORD error = 0;
    bool incomplete = false;
    ScanJob() = default;
    ~ScanJob() { cancel(); }
    ScanJob(const ScanJob&) = delete;
    ScanJob& operator=(const ScanJob&) = delete;
    bool start(const std::wstring& path, bool advanced, const std::wstring& executable = sibling(L"FileLockWorker.exe"));
    bool active() const { return bool(process_); }
    DWORD processId() const { return process_ ? GetProcessId(process_.get()) : 0; }
    void poll(const Sink& sink);
    void cancel();
};
}
