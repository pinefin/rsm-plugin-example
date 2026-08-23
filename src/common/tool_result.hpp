#pragma once

// rsm_tool_result construction helpers.
//
// The plugin ABI hands ownership of the returned result buffer back to
// the host via a free-callback. To make that a single free (not two —
// one for the result JSON, one for the error string), we allocate an
// `owned_result_t` blob that fits both string pointers, then package
// them up in one rsm_tool_result whose `userdata` points at that blob.
// The `free` callback in the result does the two `host->free` calls
// plus the blob itself, in one shot.
//
// Callers just say ok_json(nlohmann_value) or err(code, "message").

#include "rsm/plugin.h"

#include <nlohmann/json_fwd.hpp>
#include <string>

namespace plugin_sysinfo {

// Copy a std::string into host-allocated memory. Returns a null-
// terminated C string owned by the host allocator (safe for the host
// to pass back into our free callback later). Returns null on OOM.
char* dup_str(const std::string& s);

// Success result: `data` is the JSON payload the tool wants to return.
// The host takes ownership; the caller doesn't need to hang on to the
// nlohmann::json value once this returns.
rsm_tool_result ok_json(const nlohmann::json& data);

// Error result. `code` is the tool-defined error code (1..N; the host
// treats it opaquely apart from "nonzero = error"). `message` becomes
// the user-visible reason. Both strings are host-owned; freed by the
// same callback as ok_json.
rsm_tool_result err(int code, const std::string& message);

}  // namespace plugin_sysinfo
