#pragma once

// plugin_sysinfo_modules — list DLLs loaded by a target pid.
// Takes one integer param `pid`. Uses Toolhelp32 snapshot;
// callers without the right access token get an error result
// rather than a partial listing. Safety class: LOCAL_PROBE.

struct rsm_host_api_v1;
struct rsm_plugin_ctx;

namespace plugin_sysinfo {

int register_modules_tool(const rsm_host_api_v1* host, rsm_plugin_ctx* ctx);

}  // namespace plugin_sysinfo
