#pragma once

// Small builders for the JSON-Schema fragments each tool advertises
// as its `input_schema_json`. Every tool has one — either "no params"
// or a small object of typed fields — and hand-writing each schema
// as raw JSON gets noisy fast. These helpers keep the tool files
// focused on their actual behaviour.
//
// The returned strings are freshly-owned std::string; the caller
// keeps them alive as long as the host needs to read the C pointer
// (in practice, until rsm_plugin_shutdown_v1 or the registration
// call finishes copying).

#include <initializer_list>
#include <string>
#include <string_view>

namespace plugin_sysinfo {

// The classic "no params" schema. Object with no properties and
// additionalProperties = false. Reused across every tool that takes
// no input; the returned std::string is stable-per-call, so callers
// typically store it in a `static const std::string`.
std::string empty_object_schema();

// One integer field with a lower bound. Produces:
//   { "type":"object",
//     "properties": { <field>: { "type":"integer", "minimum": <min> } },
//     "required":   [ <field> ],
//     "additionalProperties": false }
std::string single_integer_schema(std::string_view field, long long minimum);

}  // namespace plugin_sysinfo
