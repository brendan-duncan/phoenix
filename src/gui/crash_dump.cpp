#include "crash_dump.h"

#ifdef _WIN32

#include <windows.h>
#include <dbghelp.h>

#include <cstdio>
#include <cwchar>

namespace CrashDump {

namespace {

// Worked out at the start, so the handler only has to write files.
wchar_t g_folder[MAX_PATH] = L"";
char g_folderUtf8[MAX_PATH * 3] = "";

LONG WINAPI written(EXCEPTION_POINTERS* info)
{
    if (!g_folder[0])
        return EXCEPTION_CONTINUE_SEARCH;
    SYSTEMTIME now;
    GetLocalTime(&now);
    wchar_t base[MAX_PATH + 64];
    swprintf(base, sizeof(base) / sizeof(base[0]), L"%ls\\Phoenix-%04d%02d%02d-%02d%02d%02d-%lu", g_folder, now.wYear,
        now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, GetCurrentProcessId());

    // Where: the module the fault is in and how far into it, which the
    // build's symbols turn into a function without the dump.
    const EXCEPTION_RECORD* record = info ? info->ExceptionRecord : nullptr;
    const void* address = record ? record->ExceptionAddress : nullptr;
    HMODULE module = nullptr;
    wchar_t moduleName[MAX_PATH] = L"?";
    if (address && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       static_cast<LPCWSTR>(address), &module))
        GetModuleFileNameW(module, moduleName, MAX_PATH);
    wchar_t textPath[MAX_PATH + 80];
    swprintf(textPath, sizeof(textPath) / sizeof(textPath[0]), L"%ls.txt", base);
    if (FILE* text = _wfopen(textPath, L"w"))
    {
        std::fprintf(text, "Exception 0x%08lX at %p: %ls + 0x%llX\n", record ? record->ExceptionCode : 0ul, address,
            moduleName,
            static_cast<unsigned long long>(reinterpret_cast<const char*>(address) - reinterpret_cast<const char*>(module)));
        if (record && record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2)
            std::fprintf(text, "%s address 0x%llX\n",
                record->ExceptionInformation[0] == 1 ? "Writing" : record->ExceptionInformation[0] == 8 ? "Running" : "Reading",
                static_cast<unsigned long long>(record->ExceptionInformation[1]));
        std::fclose(text);
    }

    wchar_t dumpPath[MAX_PATH + 80];
    swprintf(dumpPath, sizeof(dumpPath) / sizeof(dumpPath[0]), L"%ls.dmp", base);
    const HANDLE file = CreateFileW(dumpPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE)
    {
        MINIDUMP_EXCEPTION_INFORMATION exception;
        exception.ThreadId = GetCurrentThreadId();
        exception.ExceptionPointers = info;
        exception.ClientPointers = FALSE;
        const auto kind = static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithDataSegs |
            MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules);
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, kind, info ? &exception : nullptr, nullptr, nullptr);
        CloseHandle(file);
    }
    // On to Windows' own handling (and a debugger, when one is there).
    return EXCEPTION_CONTINUE_SEARCH;
}

} // namespace

void install()
{
    wchar_t local[MAX_PATH];
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
    if (length == 0 || length >= MAX_PATH - 32)
        return;
    swprintf(g_folder, MAX_PATH, L"%ls\\Phoenix", local);
    CreateDirectoryW(g_folder, nullptr);
    swprintf(g_folder, MAX_PATH, L"%ls\\Phoenix\\Crashes", local);
    CreateDirectoryW(g_folder, nullptr);
    WideCharToMultiByte(CP_UTF8, 0, g_folder, -1, g_folderUtf8, sizeof(g_folderUtf8), nullptr, nullptr);
    SetUnhandledExceptionFilter(&written);
}

const char* folder()
{
    return g_folderUtf8;
}

} // namespace CrashDump

#else

namespace CrashDump {

void install() {}

const char* folder()
{
    return "";
}

} // namespace CrashDump

#endif
