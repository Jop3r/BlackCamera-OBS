#include "log.h"

#include <windows.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>

namespace logging {

static std::atomic<Sink> s_sink{ nullptr };

void setSink(Sink sink) { s_sink = sink; }

void write(char level, const char* fmt, ...) {
    char msg[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    if (const Sink sink = s_sink.load()) {
        sink(level, msg);
        return;
    }
    char line[1100];
    snprintf(line, sizeof(line), "[BlackCamera] [%c] %s\n", level, msg);
    OutputDebugStringA(line);
}

} // namespace logging
