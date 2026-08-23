#include "common/tool_result.hpp"
#include "common/host.hpp"

#include <nlohmann/json.hpp>

#include <cstring>

namespace plugin_sysinfo {

namespace {

// Single "everything the free callback needs" blob. The host stores
// this as rsm_tool_result::userdata and hands it back on shutdown.
// Keeping the two payload pointers here (instead of, say, doing two
// separate host allocations) means the host frees us in one shot.
struct owned_result_t {
    char* result_json;    // may be null
    char* error_message;  // may be null
};

void owned_result_free(void* ud) {
    if (!ud) return;
    auto* o = static_cast<owned_result_t*>(ud);
    if (o->result_json)   host()->free(o->result_json);
    if (o->error_message) host()->free(o->error_message);
    host()->free(o);
}

}  // namespace

char* dup_str(const std::string& s) {
    char* p = static_cast<char*>(host()->alloc(s.size() + 1));
    if (!p) return nullptr;
    std::memcpy(p, s.data(), s.size());
    p[s.size()] = '\0';
    return p;
}

rsm_tool_result ok_json(const nlohmann::json& data) {
    auto* o = static_cast<owned_result_t*>(host()->alloc(sizeof(owned_result_t)));
    o->result_json   = dup_str(data.dump());
    o->error_message = nullptr;
    rsm_tool_result r{};
    r.result_json = o->result_json;
    r.free        = &owned_result_free;
    r.userdata    = o;
    return r;
}

rsm_tool_result err(int code, const std::string& message) {
    auto* o = static_cast<owned_result_t*>(host()->alloc(sizeof(owned_result_t)));
    o->result_json   = nullptr;
    o->error_message = dup_str(message);
    rsm_tool_result r{};
    r.error_code    = code;
    r.error_message = o->error_message;
    r.free          = &owned_result_free;
    r.userdata      = o;
    return r;
}

}  // namespace plugin_sysinfo
