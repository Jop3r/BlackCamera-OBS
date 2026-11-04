#pragma once
// UTF-8 <-> UTF-16 conversion for Windows APIs.

#include <string>

namespace platform {

std::wstring widen(const std::string& utf8);
std::string  narrow(const std::wstring& wide);

} // namespace platform
