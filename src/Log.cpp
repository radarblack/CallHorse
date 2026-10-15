#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>

#include "Log.h"

namespace
{
    std::mutex g_mutex;
    std::FILE* g_file = nullptr;
    bool g_attempted = false;

    std::wstring LogPath()
    {
        wchar_t buffer[MAX_PATH] = {};
        const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
        std::wstring path(buffer, length);

        const std::size_t slash = path.find_last_of(L"\\/");
        if (slash == std::wstring::npos)
            return L"CallHorse.log";

        return path.substr(0, slash + 1) + L"CallHorse.log";
    }

    std::FILE* Open()
    {
        if (!g_attempted)
        {
            g_attempted = true;
            g_file = _wfopen(LogPath().c_str(), L"a");
        }
        return g_file;
    }
}

void Log(const char* format, ...)
{
    std::lock_guard<std::mutex> lock(g_mutex);

    std::FILE* file = Open();
    if (!file)
        return;

    SYSTEMTIME now;
    GetLocalTime(&now);
    std::fprintf(file, "%04d-%02d-%02d %02d:%02d:%02d.%03d ",
        now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);

    va_list args;
    va_start(args, format);
    std::vfprintf(file, format, args);
    va_end(args);

    std::fputc('\n', file);
    std::fflush(file);
}
