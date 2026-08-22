// plugin_sysinfo — reference plugin for reversal-suite-mcp.
//
// Registers three tools:
//   plugin_sysinfo_processes  — enumerate running processes
//   plugin_sysinfo_modules    — list modules loaded by a target pid
//   plugin_sysinfo_cpuid      — return CPU vendor / brand / features
//
// Uses nlohmann/json to build and parse the payloads that cross the
// plugin ABI boundary. The ABI itself is UTF-8 JSON in a C string — any
// library (or hand-written writer) works; nlohmann is just what this
// example picks because it's what the host already uses. Third-party
// plugins can pick differently.

#include "rsm/plugin.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>

#include <windows.h>
#include <tlhelp32.h>
#include <intrin.h>

namespace {

using json = nlohmann::json;

// -----------------------------------------------------------------------
// Owned result — the plugin allocates one blob with host->alloc, packs
// both strings into it, and hands the whole blob back so the host frees
// it in one shot via our free callback.
// -----------------------------------------------------------------------

const rsm_host_api_v1* g_host = nullptr;

struct owned_result_t {
    char* result_json;    // may be NULL
    char* error_message;  // may be NULL
};

void owned_result_free(void* ud) {
    if (!ud) return;
    auto* o = static_cast<owned_result_t*>(ud);
    if (o->result_json)   g_host->free(o->result_json);
    if (o->error_message) g_host->free(o->error_message);
    g_host->free(o);
}

char* dup_str(const std::string& s) {
    char* p = static_cast<char*>(g_host->alloc(s.size() + 1));
    if (!p) return nullptr;
    std::memcpy(p, s.data(), s.size());
    p[s.size()] = '\0';
    return p;
}

rsm_tool_result ok_json(const json& j) {
    auto* o = static_cast<owned_result_t*>(g_host->alloc(sizeof(owned_result_t)));
    o->result_json   = dup_str(j.dump());
    o->error_message = nullptr;
    rsm_tool_result r{};
    r.result_json = o->result_json;
    r.free        = &owned_result_free;
    r.userdata    = o;
    return r;
}

rsm_tool_result err(int code, const std::string& m) {
    auto* o = static_cast<owned_result_t*>(g_host->alloc(sizeof(owned_result_t)));
    o->result_json   = nullptr;
    o->error_message = dup_str(m);
    rsm_tool_result r{};
    r.error_code    = code;
    r.error_message = o->error_message;
    r.free          = &owned_result_free;
    r.userdata      = o;
    return r;
}

// -----------------------------------------------------------------------
// UTF-16 → UTF-8 for Win32 strings.
// -----------------------------------------------------------------------

std::string wide_to_utf8(const wchar_t* w) {
    if (!w || !*w) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string out(static_cast<std::size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, out.data(), n, nullptr, nullptr);
    return out;
}

// -----------------------------------------------------------------------
// Tool: plugin_sysinfo_processes
// -----------------------------------------------------------------------

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

// -----------------------------------------------------------------------
// Tool: plugin_sysinfo_modules
// -----------------------------------------------------------------------

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

// -----------------------------------------------------------------------
// Tool: plugin_sysinfo_cpuid
// -----------------------------------------------------------------------

rsm_tool_result t_cpuid(rsm_session*, const char*, void*) {
    int r[4] = {0, 0, 0, 0};

    // Vendor (leaf 0)
    __cpuid(r, 0);
    char vendor[13] = {};
    std::memcpy(vendor + 0, &r[1], 4);
    std::memcpy(vendor + 4, &r[3], 4);
    std::memcpy(vendor + 8, &r[2], 4);
    vendor[12] = 0;
    int max_leaf = r[0];

    // Brand string (extended leaves 0x80000002..4)
    char brand[49] = {};
    __cpuid(r, 0x80000000);
    if (static_cast<unsigned>(r[0]) >= 0x80000004u) {
        for (unsigned i = 0; i < 3; ++i) {
            __cpuid(r, static_cast<int>(0x80000002u + i));
            std::memcpy(brand + i * 16 + 0,  &r[0], 4);
            std::memcpy(brand + i * 16 + 4,  &r[1], 4);
            std::memcpy(brand + i * 16 + 8,  &r[2], 4);
            std::memcpy(brand + i * 16 + 12, &r[3], 4);
        }
        brand[48] = 0;
    }

    // Feature bits (leaf 1)
    unsigned feat_ecx = 0, feat_edx = 0;
    if (max_leaf >= 1) {
        __cpuid(r, 1);
        feat_ecx = static_cast<unsigned>(r[2]);
        feat_edx = static_cast<unsigned>(r[3]);
    }

    return ok_json({
        {"vendor",   vendor},
        {"brand",    brand},
        {"max_leaf", max_leaf},
        {"features", {
            {"leaf1_ecx", feat_ecx},
            {"leaf1_edx", feat_edx},
            {"sse2",     (feat_edx & (1u << 26)) != 0},
            {"sse4_1",   (feat_ecx & (1u << 19)) != 0},
            {"sse4_2",   (feat_ecx & (1u << 20)) != 0},
            {"avx",      (feat_ecx & (1u << 28)) != 0},
            {"aes",      (feat_ecx & (1u << 25)) != 0},
            {"rdrand",   (feat_ecx & (1u << 30)) != 0},
            {"popcnt",   (feat_ecx & (1u << 23)) != 0},
        }},
    });
}

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
    g_host = host;

    // v1.1 helper is available when host->abi_minor >= 1. Log elevation
    // status once at load so operators can see whether admin-only tools
    // will actually work in this session.
    if (host->abi_minor >= 1 && host->is_elevated) {
        const int elevated = host->is_elevated();
        host->log(ctx, RSM_LOG_INFO,
                  elevated ? "host token is elevated (admin)"
                           : "host token is NOT elevated");
    }

    {
        rsm_tool_desc d{};
        d.name              = "plugin_sysinfo_processes";
        d.title             = "Enumerate processes";
        d.description       = "List running processes on this machine (pid, name).";
        d.input_schema_json =
            "{\"type\":\"object\",\"properties\":{},\"additionalProperties\":false}";
        d.safety            = RSM_SAFETY_LOCAL_PROBE;
        d.handler           = &t_processes;
        if (host->register_tool(ctx, &d) != 0) return 2;
    }

    {
        rsm_tool_desc d{};
        d.name              = "plugin_sysinfo_modules";
        d.title             = "List modules for pid";
        d.description       = "List modules (DLLs) loaded in the given process. Requires local_probe.";
        d.input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{\"pid\":{\"type\":\"integer\",\"minimum\":1}},"
            "\"required\":[\"pid\"],\"additionalProperties\":false}";
        d.safety            = RSM_SAFETY_LOCAL_PROBE;
        d.handler           = &t_modules;
        if (host->register_tool(ctx, &d) != 0) return 3;
    }

    {
        rsm_tool_desc d{};
        d.name              = "plugin_sysinfo_cpuid";
        d.title             = "CPUID vendor / brand / features";
        d.description       = "Return CPU vendor string, brand string, and common feature bits.";
        d.input_schema_json =
            "{\"type\":\"object\",\"properties\":{},\"additionalProperties\":false}";
        d.safety            = RSM_SAFETY_SAFE;
        d.handler           = &t_cpuid;
        if (host->register_tool(ctx, &d) != 0) return 4;
    }

    host->log(ctx, RSM_LOG_INFO, "sysinfo plugin loaded (3 tools)");
    return 0;
}

extern "C" RSM_PLUGIN_EXPORT void rsm_plugin_shutdown_v1(rsm_plugin_ctx*) {
    // Nothing to release.
}
