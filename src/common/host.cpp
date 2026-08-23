#include "common/host.hpp"

namespace plugin_sysinfo {

namespace {
const rsm_host_api_v1* g_host = nullptr;
}

void set_host(const rsm_host_api_v1* h) noexcept { g_host = h; }
const rsm_host_api_v1* host() noexcept { return g_host; }

}  // namespace plugin_sysinfo
