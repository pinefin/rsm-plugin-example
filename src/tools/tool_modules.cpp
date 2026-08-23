#include "tools/tool_modules.hpp"
#include "common/encoding.hpp"
#include "common/schema.hpp"
#include "common/tool_result.hpp"

#include "rsm/plugin.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <exception>
#include <string>

#include <windows.h>
#include <tlhelp32.h>

namespace plugin_sysinfo {

namespace {

using json = nlohmann::json;

rsm_tool_result t_modules(rsm_session*, const char* params, void*) {
    json p;
    try {
        p = params ? json::parse(params) : json::object();
    } catch (const std::exception& e) {
        return err(2, std::string("invalid params JSON: ") + e.what());
    }
    if (!p.contains("pid") || !p["pid"].is_number_integer()) {
        return err(2, "missing or invalid `pid` param (integer, minimum 1)");
    }
    std::int64_t pid = p["pid"].get<std::int64_t>();
    if (pid <= 0) {
        return err(2, "`pid` must be a positive integer");
    }

    HANDLE snap = CreateToolhelp32Snapshot(
        TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32,
        static_cast<DWORD>(pid));
    if (snap == INVALID_HANDLE_VALUE) {
        DWORD e = GetLastError();
        return err(3, "CreateToolhelp32Snapshot(pid=" + std::to_string(pid) +
                       ") failed (err " + std::to_string(e) + ")");
    }

    json mods = json::array();
    MODULEENTRY32W me{};
    me.dwSize = sizeof(me);

    if (Module32FirstW(snap, &me)) {
        do {
            mods.push_back({
                {"name", wide_to_utf8(me.szModule)},
                {"path", wide_to_utf8(me.szExePath)},
                {"base", reinterpret_cast<std::uint64_t>(me.modBaseAddr)},
                {"size", static_cast<std::uint32_t>(me.modBaseSize)},
            });
        } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);

    return ok_json({{"pid", pid}, {"modules", std::move(mods)}});
}

}  // namespace

int register_modules_tool(const rsm_host_api_v1* host, rsm_plugin_ctx* ctx) {
    static const std::string schema = single_integer_schema("pid", 1);

    rsm_tool_desc d{};
    d.name              = "plugin_sysinfo_modules";
    d.title             = "List modules for pid";
    d.description       = "List modules (DLLs) loaded in the given process. Requires local_probe.";
    d.input_schema_json = schema.c_str();
    d.safety            = RSM_SAFETY_LOCAL_PROBE;
    d.handler           = &t_modules;
    return host->register_tool(ctx, &d);
}

}  // namespace plugin_sysinfo
