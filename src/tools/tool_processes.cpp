#include "tools/tool_processes.hpp"
#include "common/encoding.hpp"
#include "common/schema.hpp"
#include "common/tool_result.hpp"

#include "rsm/plugin.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>

#include <windows.h>
#include <tlhelp32.h>

namespace plugin_sysinfo {

namespace {

using json = nlohmann::json;

rsm_tool_result t_processes(rsm_session*, const char*, void*) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        return err(1, "CreateToolhelp32Snapshot failed");
    }

    json procs = json::array();
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);

    if (Process32FirstW(snap, &pe)) {
        do {
            procs.push_back({
                {"pid",  static_cast<std::uint32_t>(pe.th32ProcessID)},
                {"name", wide_to_utf8(pe.szExeFile)},
            });
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);

    return ok_json({{"processes", std::move(procs)}});
}

}  // namespace

int register_processes_tool(const rsm_host_api_v1* host, rsm_plugin_ctx* ctx) {
    // Schema string outlives the register_tool call (the host copies)
    // — a function-local static is enough.
    static const std::string schema = empty_object_schema();

    rsm_tool_desc d{};
    d.name              = "plugin_sysinfo_processes";
    d.title             = "Enumerate processes";
    d.description       = "List running processes on this machine (pid, name).";
    d.input_schema_json = schema.c_str();
    d.safety            = RSM_SAFETY_LOCAL_PROBE;
    d.handler           = &t_processes;
    return host->register_tool(ctx, &d);
}

}  // namespace plugin_sysinfo
