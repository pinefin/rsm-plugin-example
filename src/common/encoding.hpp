#pragma once

// Tiny UTF-16 → UTF-8 helper for the Win32 wide-string APIs. Every
// tool that touches Toolhelp32 / kernel32 handles gets back
// wchar_t* buffers; the plugin ABI is UTF-8 JSON, so we need one
// pass to convert.

#include <string>

namespace plugin_sysinfo {

// Convert a null-terminated wide string to UTF-8. Returns empty for
// null / empty inputs. Never throws.
std::string wide_to_utf8(const wchar_t* w);

}  // namespace plugin_sysinfo
