#pragma once
// Logging for the effect code. Messages go to the sink set by the host (OBS blog()
// in the plugin); without a sink they go to OutputDebugString.

namespace logging {

void write(char level, const char* fmt, ...);

using Sink = void (*)(char level, const char* message);
void setSink(Sink sink); // nullptr restores OutputDebugString

} // namespace logging

#define LOG_INFO(...)  ::logging::write('I', __VA_ARGS__)
#define LOG_WARN(...)  ::logging::write('W', __VA_ARGS__)
#define LOG_ERROR(...) ::logging::write('E', __VA_ARGS__)
