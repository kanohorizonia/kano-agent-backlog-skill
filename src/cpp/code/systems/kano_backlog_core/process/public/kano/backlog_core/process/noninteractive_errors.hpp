#pragma once

#ifdef _WIN32
#include <crtdbg.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <windows.h>
#endif

namespace kano::backlog_core {

inline void ConfigureNoninteractiveErrorHandling() {
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    SetThreadErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX, nullptr);

    SetUnhandledExceptionFilter([](EXCEPTION_POINTERS*) -> LONG {
        std::fputs("Fatal native exception.\n", stderr);
        std::fflush(stderr);
        return EXCEPTION_EXECUTE_HANDLER;
    });

    _set_error_mode(_OUT_TO_STDERR);
    _set_invalid_parameter_handler(
        [](const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t) {});
    _set_thread_local_invalid_parameter_handler(
        [](const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t) {});
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);

    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
}

} // namespace kano::backlog_core
