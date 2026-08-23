#include "common/schema.hpp"

#include <nlohmann/json.hpp>

namespace plugin_sysinfo {

using json = nlohmann::json;

std::string empty_object_schema() {
    return json{
        {"type", "object"},
        {"properties", json::object()},
        {"additionalProperties", false},
    }.dump();
}

std::string single_integer_schema(std::string_view field, long long minimum) {
    // std::string_view -> string once, because nlohmann's JSON keys
    // are stored as std::string internally.
    const std::string key(field);
    return json{
        {"type", "object"},
        {"properties", {
            {key, {
                {"type", "integer"},
                {"minimum", minimum},
            }},
        }},
        {"required", json::array({key})},
        {"additionalProperties", false},
    }.dump();
}

}  // namespace plugin_sysinfo
