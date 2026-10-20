#include "sample_profiler.h"

#include <QHash>
#include <QSet>
#include <QStringList>

#include <algorithm>
#include <atomic>
#include <thread>
#include <vector>

// The unwinding below reads x64 registers.
#if defined(Q_OS_WIN) && defined(_M_X64)
#define PHOENIX_SAMPLER
#endif

#ifdef PHOENIX_SAMPLER
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#include <timeapi.h>
#pragma comment(lib, "winmm.lib")
#endif

struct SampleProfiler::Impl
{
    std::thread sampler;
    std::atomic<bool> running{false};
    /// Each sample: the return addresses, innermost first.
    std::vector<std::vector<quint64>> stacks;
#ifdef PHOENIX_SAMPLER
    HANDLE target = nullptr;
#endif
};

SampleProfiler::SampleProfiler()
    : _impl(std::make_unique<Impl>())
{
}

SampleProfiler::~SampleProfiler()
{
    stop();
}

bool SampleProfiler::start()
{
#ifdef PHOENIX_SAMPLER
    Impl& d = *_impl;
    if (d.running)
        return true;
    if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &d.target,
            THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, 0))
        return false;
    d.running = true;
    // Sleep(1) sleeps a millisecond only with the timer set that fine.
    timeBeginPeriod(1);
    d.sampler = std::thread([&d]() {
        constexpr int kDepth = 96;
        quint64 frames[kDepth];
        while (d.running)
        {
            Sleep(1);
            if (SuspendThread(d.target) == static_cast<DWORD>(-1))
                continue;
            int depth = 0;
            CONTEXT context = {};
            context.ContextFlags = CONTEXT_FULL;
            if (GetThreadContext(d.target, &context))
            {
                // Unwound by the tables the compiler wrote, which takes no
                // lock the stopped thread may hold.
                while (depth < kDepth && context.Rip)
                {
                    frames[depth++] = context.Rip;
                    DWORD64 base = 0;
                    RUNTIME_FUNCTION* function = RtlLookupFunctionEntry(context.Rip, &base, nullptr);
                    if (!function)
                    {
                        // A leaf: its return address is at the top of the stack.
                        context.Rip = *reinterpret_cast<DWORD64*>(context.Rsp);
                        context.Rsp += 8;
                        continue;
                    }
                    void* handler = nullptr;
                    DWORD64 frame = 0;
                    RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, context.Rip, function, &context, &handler, &frame, nullptr);
                }
            }
            ResumeThread(d.target);
            d.stacks.emplace_back(frames, frames + depth);
        }
    });
    return true;
#else
    return false;
#endif
}

void SampleProfiler::stop()
{
    Impl& d = *_impl;
    if (!d.running)
        return;
    d.running = false;
    if (d.sampler.joinable())
        d.sampler.join();
#ifdef PHOENIX_SAMPLER
    timeEndPeriod(1);
    CloseHandle(d.target);
    d.target = nullptr;
#endif
}

QString SampleProfiler::report(int top) const
{
#ifdef PHOENIX_SAMPLER
    const Impl& d = *_impl;
    if (d.stacks.empty())
        return "No samples.\n";
    HANDLE process = GetCurrentProcess();
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(process, nullptr, TRUE);
    QHash<quint64, QString> names;
    const auto nameOf = [&](quint64 address) -> QString {
        auto found = names.constFind(address);
        if (found != names.constEnd())
            return *found;
        alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 512];
        auto* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 511;
        DWORD64 offset = 0;
        QString name;
        if (SymFromAddr(process, address, &offset, symbol))
            name = QString::fromLatin1(symbol->Name, static_cast<int>(symbol->NameLen));
        else
        {
            IMAGEHLP_MODULE64 module = {};
            module.SizeOfStruct = sizeof(module);
            name = SymGetModuleInfo64(process, address, &module) ? QString("[%1]").arg(module.ModuleName)
                                                                   : QString("0x%1").arg(address, 0, 16);
        }
        names.insert(address, name);
        return name;
    };
    QHash<QString, int> self;
    QHash<QString, int> total;
    for (const std::vector<quint64>& stack : d.stacks)
    {
        if (stack.empty())
            continue;
        ++self[nameOf(stack.front())];
        QSet<QString> seen;
        for (const quint64 address : stack)
        {
            const QString name = nameOf(address);
            if (!seen.contains(name))
            {
                seen.insert(name);
                ++total[name];
            }
        }
    }
    SymCleanup(process);
    const double count = static_cast<double>(d.stacks.size());
    const auto table = [&](const QHash<QString, int>& counts, const QString& title) {
        std::vector<std::pair<int, QString>> rows;
        for (auto it = counts.cbegin(); it != counts.cend(); ++it)
            rows.emplace_back(it.value(), it.key());
        std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        QString out = title + "\n";
        for (int i = 0; i < std::min<int>(top, static_cast<int>(rows.size())); ++i)
            out += QString("  %1%  %2\n").arg(100.0 * rows[static_cast<size_t>(i)].first / count, 5, 'f', 1)
                       .arg(rows[static_cast<size_t>(i)].second.left(150));
        return out;
    };
    return QString("%1 samples\n\n").arg(d.stacks.size()) + table(total, "With what they call:") + "\n" +
        table(self, "On their own:");
#else
    Q_UNUSED(top);
    return "Sampling works on Windows only.\n";
#endif
}
