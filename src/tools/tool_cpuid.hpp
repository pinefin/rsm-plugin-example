#pragma once

// plugin_sysinfo_cpuid — vendor string, brand string, and common
// feature bits (SSE2/4.1/4.2, AVX, AES, RDRAND, POPCNT). No params;
// purely local computation. Safety class: SAFE.

struct rsm_host_api_v1;
struct rsm_plugin_ctx;

namespace plugin_sysinfo {

int register_cpuid_tool(const rsm_host_api_v1* host, rsm_plugin_ctx* ctx);

}  // namespace plugin_sysinfo
