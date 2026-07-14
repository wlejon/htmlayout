#pragma once
//
// A sampling profiler, small enough to live in the benchmark.
//
// The bench can price a suspect it already suspects — a style lookup, a malloc,
// a shaping call — but that only ever confirms or denies a guess, and the sum of
// the guesses came to 16ms of a 53ms pass. The other 37ms had no candidate. This
// finds candidates instead of testing them: it parks a thread next to the one
// doing the work, stops it thousands of times a second, and writes down where the
// instruction pointer was. Functions that show up a lot are where the time is,
// whether or not anyone suspected them.
//
// Self time only — it samples the leaf PC, not the call stack. That is the right
// question here ("which code is executing") and it is also the cheap one: no
// stack walk, no unwind tables, just a register read. It cannot say who *called*
// the hot function, so when the answer is a std::string or hash-map internal,
// read it as "somebody is doing a lot of this" and go find them with the call-site
// histogram in style_util.h.
//
// Windows-only, and only compiled into the bench.

#if defined(_WIN32)

#include <windows.h>
#include <dbghelp.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#pragma comment(lib, "dbghelp.lib")

namespace bench {

class Sampler {
public:
    // intervalUs: how long to sleep between samples. The thread is suspended for
    // the length of one GetThreadContext, so the target runs at close to full
    // speed; what the interval really buys is sample count, and a profile needs
    // thousands to separate a 5% function from a 2% one.
    explicit Sampler(unsigned intervalUs = 100) : intervalUs_(intervalUs) {
        DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(),
                        &target_, THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, 0);
    }
    ~Sampler() { stop(); if (target_) CloseHandle(target_); }

    void start() {
        running_ = true;
        worker_ = std::thread([this] { loop(); });
    }
    void stop() {
        if (!running_) return;
        running_ = false;
        if (worker_.joinable()) worker_.join();
    }

    // Resolve the collected addresses to symbols and print the top `top` by self
    // time. Call after stop().
    void report(const char* title, int top = 25) {
        HANDLE proc = GetCurrentProcess();
        SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
        SymInitialize(proc, nullptr, TRUE);

        // Fold per-address counts into per-function counts.
        std::unordered_map<std::string, uint64_t> byFunc;
        uint64_t total = 0, unresolved = 0;
        alignas(SYMBOL_INFO) char buf[sizeof(SYMBOL_INFO) + MAX_SYM_NAME];
        auto* sym = reinterpret_cast<SYMBOL_INFO*>(buf);
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen = MAX_SYM_NAME;

        for (auto& [addr, n] : counts_) {
            total += n;
            DWORD64 disp = 0;
            // Name the module as well as the function. Inside a system DLL the
            // only symbols are its exports, so a PC in some internal heap routine
            // resolves to whichever export happens to sit below it — which is how
            // an allocation-heavy profile sprouts a fictitious 7% in
            // RtlCreateUnicodeString. Tagging the module makes that legible as
            // "7% somewhere in ntdll" instead of a specific lie.
            IMAGEHLP_MODULE64 mod{};
            mod.SizeOfStruct = sizeof(mod);
            const char* modName = SymGetModuleInfo64(proc, addr, &mod) ? mod.ModuleName : "";
            bool ours = modName && _stricmp(modName, "htmlayout_bench") == 0;

            if (SymFromAddr(proc, addr, &disp, sym)) {
                std::string name = sym->Name;
                // Only trust a symbol name in our own module, where every function
                // is in the PDB. Elsewhere, report the module.
                if (!ours) name = std::string("[") + (modName[0] ? modName : "?") + "] " + name;
                byFunc[name] += n;
            } else {
                byFunc[modName[0] ? std::string("[") + modName + "] <no symbol>"
                                  : std::string("<unresolved>")] += n;
                unresolved += n;
            }
        }
        SymCleanup(proc);

        std::vector<std::pair<std::string, uint64_t>> v(byFunc.begin(), byFunc.end());
        std::sort(v.begin(), v.end(),
                  [](const auto& a, const auto& b) { return a.second > b.second; });

        printf("\n  === %s: %llu samples, %zu distinct functions ===\n", title,
               (unsigned long long)total, v.size());
        if (!total) {
            printf("  (no samples — is the profiled region long enough?)\n");
            return;
        }
        if (unresolved * 4 > total)
            printf("  (%.0f%% unresolved — build RelWithDebInfo so the PDB exists)\n",
                   100.0 * unresolved / total);
        double cum = 0;
        for (int i = 0; i < top && i < (int)v.size(); i++) {
            double pct = 100.0 * v[i].second / total;
            cum += pct;
            printf("  %6.2f%%  (cum %5.1f%%)  %s\n", pct, cum, v[i].first.c_str());
        }
    }

private:
    void loop() {
        // Spin on the performance counter rather than sleeping. Windows' default
        // timer granularity is 15.6ms, so a sleep_for(50us) actually sleeps 15ms
        // and a three-second profile collects ~190 samples — enough to see the top
        // entry and nothing else. Spinning burns one core, which is free here (the
        // target is single-threaded and the machine has others) and buys three
        // orders of magnitude more samples.
        LARGE_INTEGER freq;
        QueryPerformanceFrequency(&freq);
        const double ticksPerUs = double(freq.QuadPart) / 1e6;
        const long long interval = (long long)(intervalUs_ * ticksPerUs);
        LARGE_INTEGER next;
        QueryPerformanceCounter(&next);
        while (running_) {
            if (SuspendThread(target_) != (DWORD)-1) {
                CONTEXT ctx{};
                ctx.ContextFlags = CONTEXT_CONTROL;
                if (GetThreadContext(target_, &ctx)) {
#if defined(_M_X64)
                    counts_[ctx.Rip]++;
#elif defined(_M_ARM64)
                    counts_[ctx.Pc]++;
#endif
                }
                ResumeThread(target_);
            }
            next.QuadPart += interval;
            LARGE_INTEGER now;
            do {
                YieldProcessor();
                QueryPerformanceCounter(&now);
            } while (now.QuadPart < next.QuadPart && running_);
        }
    }

    HANDLE target_ = nullptr;
    std::thread worker_;
    std::atomic<bool> running_{false};
    unsigned intervalUs_;
    std::unordered_map<DWORD64, uint64_t> counts_;
};

} // namespace bench

#else
namespace bench {
struct Sampler {
    explicit Sampler(unsigned = 100) {}
    void start() {}
    void stop() {}
    void report(const char*, int = 25) {
        printf("\n  (sampling profiler is Windows-only)\n");
    }
};
} // namespace bench
#endif
