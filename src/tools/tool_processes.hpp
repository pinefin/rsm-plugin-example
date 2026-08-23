#pragma once

// plugin_sysinfo_processes — list running processes on this machine.
// Enumeration only; no handle opens, no memory reads. Safety class:
// LOCAL_PROBE (the host still must allow this via `--allow-plugins`).

struct rsm_host_api_v1;
struct rsm_plugin_ctx;

namespace plugin_sysinfo {

// Register the tool with the host. Returns 0 on success, non-zero
// error code on failure (bubbled up to rsm_plugin_init_v1's return).
int register_processes_tool(const rsm_host_api_v1* host, rsm_plugin_ctx* ctx);

}  // namespace plugin_sysinfo
