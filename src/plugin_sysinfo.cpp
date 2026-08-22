// plugin_sysinfo — reference plugin for reversal-suite-mcp.
//
// Registers three tools:
//   plugin_sysinfo_processes  — enumerate running processes
//   plugin_sysinfo_modules    — list modules loaded by a target pid
//   plugin_sysinfo_cpuid      — return CPU vendor / brand / features
//
// The whole file is a template for third-party plugin authors: it uses only
// the C ABI from rsm/plugin.h plus Win32 / <intrin.h>. No STL leaks across
// the ABI boundary.

#include "rsm/plugin.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <windows.h>
#include <tlhelp32.h>
#include <intrin.h>

namespace {

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

rsm_tool_result ok_json(const std::string& j) {
    auto* o = static_cast<owned_result_t*>(g_host->alloc(sizeof(owned_result_t)));
    o->result_json   = dup_str(j);
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
// Minimal JSON writer. Enough for our three tools; no dep on a JSON lib
// so the plugin stays tiny.
// -----------------------------------------------------------------------

void json_escape_into(std::string& out, const char* s) {
    for (const char* p = s; *p; ++p) {
        unsigned char c = static_cast<unsigned char>(*p);
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
}

std::string wide_to_utf8(const wchar_t* w) {
    if (!w || !*w) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string out(static_cast<size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, out.data(), n, nullptr, nullptr);
    return out;
}

// -----------------------------------------------------------------------
// Params helpers — we don't parse JSON in the plugin; for `modules` we
// just look for a `"pid":<integer>` substring. Real plugins should use a
// proper parser; this file stays dependency-free deliberately.
// -----------------------------------------------------------------------

bool find_int_param(const char* json, const char* key, int64_t& out) {
    if (!json || !key) return false;
    std::string needle = std::string("\"") + key + "\"";
    const char* p = std::strstr(json, needle.c_str());
    if (!p) return false;
    p += needle.size();
    while (*p == ' ' || *p == '\t' || *p == ':') ++p;
    char* end = nullptr;
    long long v = std::strtoll(p, &end, 10);
    if (end == p) return false;
    out = v;
    return true;
}

// -----------------------------------------------------------------------
// Tool: plugin_sysinfo_processes
// -----------------------------------------------------------------------

rsm_tool_result t_processes(rsm_session*, const char*, void*) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        return err(1, "CreateToolhelp32Snapshot failed");
    }

    std::string out = "{\"processes\":[";
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);

    bool first = true;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (!first) out += ",";
            first = false;

            std::string name = wide_to_utf8(pe.szExeFile);
            char pid_buf[32];
            std::snprintf(pid_buf, sizeof(pid_buf), "%lu",
                          static_cast<unsigned long>(pe.th32ProcessID));

            out += "{\"pid\":";
            out += pid_buf;
            out += ",\"name\":\"";
            json_escape_into(out, name.c_str());
            out += "\"}";
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    out += "]}";
    return ok_json(out);
}

// -----------------------------------------------------------------------
// Tool: plugin_sysinfo_modules
// -----------------------------------------------------------------------

rsm_tool_result t_modules(rsm_session*, const char* params, void*) {
    int64_t pid = 0;
    if (!find_int_param(params, "pid", pid) || pid <= 0) {
        return err(2, "missing or invalid `pid` param");
    }

    HANDLE snap = CreateToolhelp32Snapshot(
        TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32,
        static_cast<DWORD>(pid));
    if (snap == INVALID_HANDLE_VALUE) {
        char msg[128];
        std::snprintf(msg, sizeof(msg),
                      "CreateToolhelp32Snapshot(pid=%lld) failed (err %lu)",
                      static_cast<long long>(pid),
                      static_cast<unsigned long>(GetLastError()));
        return err(3, msg);
    }

    std::string out = "{\"pid\":";
    {
        char b[32];
        std::snprintf(b, sizeof(b), "%lld", static_cast<long long>(pid));
        out += b;
    }
    out += ",\"modules\":[";

    MODULEENTRY32W me{};
    me.dwSize = sizeof(me);

    bool first = true;
    if (Module32FirstW(snap, &me)) {
        do {
            if (!first) out += ",";
            first = false;

            std::string name = wide_to_utf8(me.szModule);
            std::string path = wide_to_utf8(me.szExePath);
            char base_buf[32], size_buf[32];
            std::snprintf(base_buf, sizeof(base_buf), "%llu",
                          reinterpret_cast<unsigned long long>(me.modBaseAddr));
            std::snprintf(size_buf, sizeof(size_buf), "%lu",
                          static_cast<unsigned long>(me.modBaseSize));

            out += "{\"name\":\"";  json_escape_into(out, name.c_str());
            out += "\",\"path\":\""; json_escape_into(out, path.c_str());
            out += "\",\"base\":";  out += base_buf;
            out += ",\"size\":";    out += size_buf;
            out += "}";
        } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);
    out += "]}";
    return ok_json(out);
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
            __cpuid(r, 0x80000002 + i);
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

    char buf[512];
    std::snprintf(buf, sizeof(buf),
                  "{\"vendor\":\"%s\",\"brand\":\"%s\","
                  "\"max_leaf\":%d,"
                  "\"features\":{\"leaf1_ecx\":%u,\"leaf1_edx\":%u,"
                  "\"sse2\":%s,\"sse4_1\":%s,\"sse4_2\":%s,"
                  "\"avx\":%s,\"aes\":%s,\"rdrand\":%s,\"popcnt\":%s}}",
                  vendor, brand, max_leaf,
                  feat_ecx, feat_edx,
                  (feat_edx & (1u << 26)) ? "true" : "false",   // SSE2
                  (feat_ecx & (1u << 19)) ? "true" : "false",   // SSE4.1
                  (feat_ecx & (1u << 20)) ? "true" : "false",   // SSE4.2
                  (feat_ecx & (1u << 28)) ? "true" : "false",   // AVX
                  (feat_ecx & (1u << 25)) ? "true" : "false",   // AES
                  (feat_ecx & (1u << 30)) ? "true" : "false",   // RDRAND
                  (feat_ecx & (1u << 23)) ? "true" : "false");  // POPCNT
    return ok_json(buf);
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
