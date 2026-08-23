// plugin_sysinfo — reference plugin for reversal-suite-mcp.
//
// This file only wires the ABI-visible symbols (manifest / init /
// shutdown). All actual tool logic lives in src/tools/*, with the
// shared allocation + encoding + schema helpers under src/common/.
// The idea is that when you copy this repo as a template for your
// own plugin, plugin_entry.cpp stays roughly the same size no
// matter how many tools you add.

#include "rsm/plugin.h"

#include "common/host.hpp"
#include "tools/tool_cpuid.hpp"
#include "tools/tool_modules.hpp"
#include "tools/tool_processes.hpp"

#include <cstddef>

namespace {

// One row in the registration table below.
//
// Every tool implementation exposes exactly one entry point of this
// shape, packages its own descriptor internally, and hands the host
// the pointer via host->register_tool. Adding a new tool = drop a
// new header include + one row in `g_tools` below. No other file
// changes.
struct tool_row_t {
    const char* short_name;   // for the load-time log line
    int (*register_fn)(const rsm_host_api_v1*, rsm_plugin_ctx*);
    int         err_code;     // returned from init on registration failure
};

constexpr tool_row_t g_tools[] = {
    { "processes", &plugin_sysinfo::register_processes_tool, 2 },
    { "modules",   &plugin_sysinfo::register_modules_tool,   3 },
    { "cpuid",     &plugin_sysinfo::register_cpuid_tool,     4 },
};

}  // namespace

// -----------------------------------------------------------------------
// Plugin exports
// -----------------------------------------------------------------------

extern "C" RSM_PLUGIN_EXPORT const rsm_plugin_manifest* rsm_plugin_manifest_v1(void) {
    static const rsm_plugin_manifest m{
        RSM_PLUGIN_ABI_MAJOR,
        RSM_PLUGIN_ABI_MINOR,
        "sysinfo",
        "0.1.0",
        "System information (processes, modules, cpuid)",
        RSM_SAFETY_LOCAL_PROBE,
    };
    return &m;
}

extern "C" RSM_PLUGIN_EXPORT int rsm_plugin_init_v1(const rsm_host_api_v1* host,
                                                    rsm_plugin_ctx* ctx) {
    if (!host || host->abi_major != RSM_PLUGIN_ABI_MAJOR) return 1;
    plugin_sysinfo::set_host(host);

    // v1.1 helper is available when host->abi_minor >= 1. Log elevation
    // status once at load so operators can see whether admin-only tools
    // will actually work in this session.
    if (host->abi_minor >= 1 && host->is_elevated) {
        const int elevated = host->is_elevated();
        host->log(ctx, RSM_LOG_INFO,
                  elevated ? "host token is elevated (admin)"
                           : "host token is NOT elevated");
    }

    for (const auto& t : g_tools) {
        if (int rc = t.register_fn(host, ctx); rc != 0) {
            // The host is free to log this itself; we still bubble a
            // distinct code up so an operator running with tracing on
            // can tell which tool failed to register.
            (void)t.short_name;
            return t.err_code;
        }
    }

    constexpr std::size_t k_tool_count = sizeof(g_tools) / sizeof(g_tools[0]);
    (void)k_tool_count;   // used by the log line below
    host->log(ctx, RSM_LOG_INFO, "sysinfo plugin loaded (3 tools)");
    return 0;
}

extern "C" RSM_PLUGIN_EXPORT void rsm_plugin_shutdown_v1(rsm_plugin_ctx*) {
    // Nothing to release. The tool_result::free callbacks handle
    // their own per-call memory; the host tears the DLL down after
    // this returns.
}
