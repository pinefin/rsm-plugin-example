#include "tools/tool_cpuid.hpp"
#include "common/schema.hpp"
#include "common/tool_result.hpp"

#include "rsm/plugin.h"

#include <nlohmann/json.hpp>

#include <cstring>
#include <string>

#include <intrin.h>

namespace plugin_sysinfo {

namespace {

using json = nlohmann::json;

rsm_tool_result t_cpuid(rsm_session*, const char*, void*) {
    int r[4] = {0, 0, 0, 0};

    // Vendor (leaf 0). The 12-byte vendor ID lands in EBX/EDX/ECX in
    // that order — "GenuineIntel", "AuthenticAMD", ...
    __cpuid(r, 0);
    char vendor[13] = {};
    std::memcpy(vendor + 0, &r[1], 4);
    std::memcpy(vendor + 4, &r[3], 4);
    std::memcpy(vendor + 8, &r[2], 4);
    vendor[12] = 0;
    int max_leaf = r[0];

    // Brand string (extended leaves 0x80000002..4). Three 16-byte
    // chunks concatenated; only present if extended max leaf >= 4.
    char brand[49] = {};
    __cpuid(r, 0x80000000);
    if (static_cast<unsigned>(r[0]) >= 0x80000004u) {
        for (unsigned i = 0; i < 3; ++i) {
            __cpuid(r, static_cast<int>(0x80000002u + i));
            std::memcpy(brand + i * 16 +  0, &r[0], 4);
            std::memcpy(brand + i * 16 +  4, &r[1], 4);
            std::memcpy(brand + i * 16 +  8, &r[2], 4);
            std::memcpy(brand + i * 16 + 12, &r[3], 4);
        }
        brand[48] = 0;
    }

    // Feature bits (leaf 1).
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

int register_cpuid_tool(const rsm_host_api_v1* host, rsm_plugin_ctx* ctx) {
    static const std::string schema = empty_object_schema();

    rsm_tool_desc d{};
    d.name              = "plugin_sysinfo_cpuid";
    d.title             = "CPUID vendor / brand / features";
    d.description       = "Return CPU vendor string, brand string, and common feature bits.";
    d.input_schema_json = schema.c_str();
    d.safety            = RSM_SAFETY_SAFE;
    d.handler           = &t_cpuid;
    return host->register_tool(ctx, &d);
}

}  // namespace plugin_sysinfo
