#include "CrashDump.h"

#if defined(_MSC_VER)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>
#include <cstdio>
#include <cstring>

#pragma comment(lib, "dbghelp.lib")

namespace
{
    // Fixed buffers: the filter runs in a crashed process and avoids allocating.
    char g_dir[MAX_PATH];
    char g_serverLog[MAX_PATH];
    LONG volatile g_fired = 0;

    // The dump and the note are written on a thread of their own. A stack overflow leaves the
    // faulting thread a few KB of stack, too little for MiniDumpWriteDump, fopen or snprintf:
    // before this, every overflow left a 0-byte .dmp, no CRASH line, and Windows reported a
    // second fault inside ntdll's exception dispatch (ntdll+0x12c513) instead of the real one.
    struct Job
    {
        EXCEPTION_POINTERS* info;
        DWORD threadId;
    };

    void WriteText(char const* path, char const* text)
    {
        if (!path[0])
            return;
        HANDLE f = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f == INVALID_HANDLE_VALUE)
            return;
        DWORD written = 0;
        WriteFile(f, text, DWORD(std::strlen(text)), &written, nullptr);
        CloseHandle(f);
    }

    DWORD WINAPI WriteReport(LPVOID param)
    {
        Job const& job = *static_cast<Job const*>(param);
        EXCEPTION_RECORD const* rec = job.info->ExceptionRecord;
        ULONG_PTR const addr = ULONG_PTR(rec->ExceptionAddress);
        HMODULE mod = nullptr;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(addr), &mod);
        char modPath[MAX_PATH] = "?";
        if (mod)
            GetModuleFileNameA(mod, modPath, MAX_PATH);
        char const* modName = std::strrchr(modPath, '\\');
        modName = modName ? modName + 1 : modPath;
        ULONG_PTR const offset = mod ? addr - ULONG_PTR(mod) : addr;

        SYSTEMTIME t;
        GetLocalTime(&t);
        char base[MAX_PATH * 2];
        std::snprintf(base, sizeof(base), "%smangosd-%04u%02u%02u-%02u%02u%02u", g_dir, t.wYear, t.wMonth, t.wDay,
                      t.wHour, t.wMinute, t.wSecond);

        char dumpPath[MAX_PATH * 2 + 16];
        std::snprintf(dumpPath, sizeof(dumpPath), "%s.dmp", base);
        bool dumped = false;
        HANDLE file = CreateFileA(dumpPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE)
        {
            MINIDUMP_EXCEPTION_INFORMATION mei;
            mei.ThreadId = job.threadId;
            mei.ExceptionPointers = job.info;
            mei.ClientPointers = FALSE;
            MINIDUMP_TYPE const type = MINIDUMP_TYPE(MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo |
                                                     MiniDumpWithUnloadedModules);
            dumped = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, type, &mei, nullptr, nullptr) != FALSE;
            CloseHandle(file);
            if (!dumped)
                DeleteFileA(dumpPath);
        }

        char line[MAX_PATH * 4];
        std::snprintf(line, sizeof(line),
                      "CRASH: exception 0x%08lX%s at %s+0x%llX (thread %lu)%s%s\n"
                      "       resolve: python server/contrib/crash/symbolize.py <bin>/mangosd.exe 0x%llX\n",
                      rec->ExceptionCode, rec->ExceptionCode == EXCEPTION_STACK_OVERFLOW ? " (stack overflow)" : "",
                      modName, static_cast<unsigned long long>(offset), job.threadId,
                      dumped ? "; minidump " : "; no minidump", dumped ? dumpPath : "",
                      static_cast<unsigned long long>(offset));
        char notePath[MAX_PATH * 2 + 16];
        std::snprintf(notePath, sizeof(notePath), "%s.crash.txt", base);
        WriteText(notePath, line);
        WriteText(g_serverLog, line);
        DWORD written = 0;
        WriteFile(GetStdHandle(STD_ERROR_HANDLE), line, DWORD(std::strlen(line)), &written, nullptr);
        return 0;
    }

    LONG WINAPI OnUnhandledException(EXCEPTION_POINTERS* info)
    {
        // One report: a second thread faulting meanwhile just goes on to Windows.
        if (InterlockedExchange(&g_fired, 1))
            return EXCEPTION_CONTINUE_SEARCH;

        // Only what fits in the scraps of an overflowed stack happens here.
        static Job job;
        job.info = info;
        job.threadId = GetCurrentThreadId();
        if (HANDLE writer = CreateThread(nullptr, 1024 * 1024, &WriteReport, &job, 0, nullptr))
        {
            WaitForSingleObject(writer, 120 * 1000);
            CloseHandle(writer);
        }

        // Let Windows finish as before (the Application log entry, WER).
        return EXCEPTION_CONTINUE_SEARCH;
    }
}

void CrashDump::Install(std::string const& logsDir, std::string const& serverLog)
{
    std::string dir = logsDir;
    if (!dir.empty() && dir.back() != '/' && dir.back() != '\\')
        dir += '/';
    std::snprintf(g_dir, sizeof(g_dir), "%s", dir.c_str());
    if (serverLog.empty())
        g_serverLog[0] = 0;
    else
        std::snprintf(g_serverLog, sizeof(g_serverLog), "%s%s", dir.c_str(), serverLog.c_str());
    SetUnhandledExceptionFilter(&OnUnhandledException);
}

#else

void CrashDump::Install(std::string const&, std::string const&)
{
}

#endif
