// rsm/plugin.h — stable C ABI for out-of-tree plugins.
//
// A plugin is a shared library the host loads at startup from a plugins
// directory. It exports three C symbols:
//
//   const rsm_plugin_manifest* rsm_plugin_manifest_v1(void);
//   int  rsm_plugin_init_v1(const rsm_host_api_v1*, rsm_plugin_ctx*);
//   void rsm_plugin_shutdown_v1(rsm_plugin_ctx*);        // optional
//
// The host reads the manifest first, checks ABI, then calls init. During
// init the plugin calls host->register_tool(...) once per tool it wants
// to expose. Tool names MUST start with `plugin_<manifest.name>_` so a
// plugin can never shadow a builtin.
//
// The ABI is intentionally C-only: no STL types, no C++ classes, no
// exceptions across the boundary. JSON is exchanged as UTF-8
// null-terminated char*. This lets a plugin be built with any compiler /
// STL / config, and lets the host swap toolchains without breaking
// existing plugins.

#ifndef RSM_PLUGIN_H
#define RSM_PLUGIN_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ABI version. Host guarantees:
//   - it will only load plugins whose abi_major == RSM_PLUGIN_ABI_MAJOR
//   - additive changes (new host_api slots, new manifest fields) bump minor
//   - a v1.N plugin runs on a v1.M host as long as M >= N
#define RSM_PLUGIN_ABI_MAJOR 1u
#define RSM_PLUGIN_ABI_MINOR 0u

// Opaque handles. The plugin never dereferences these.
typedef struct rsm_plugin_ctx rsm_plugin_ctx;   // per-plugin scratch owned by host
typedef struct rsm_session    rsm_session;      // per-client session owned by host

typedef enum rsm_safety_class {
    RSM_SAFETY_SAFE          = 0,   // pure computation, no side effects
    RSM_SAFETY_ANALYTICAL    = 1,   // read user-chosen files, sandboxed math
    RSM_SAFETY_LOCAL_PROBE   = 2,   // enumerate this machine / read own memory
    RSM_SAFETY_SYSTEM_STATE  = 3    // mutate system state; needs elevated flag
} rsm_safety_class;

typedef enum rsm_log_level {
    RSM_LOG_TRACE = 0,
    RSM_LOG_DEBUG = 1,
    RSM_LOG_INFO  = 2,
    RSM_LOG_WARN  = 3,
    RSM_LOG_ERROR = 4
} rsm_log_level;

// Return value from a tool handler. On success: fill result_json and leave
// error_code == 0. On error: leave result_json NULL and fill error_message.
//
// Memory:
//   - Prefer allocating result_json / error_message with host->alloc().
//     The host will call host->free() on them after copy. Set `free` to
//     NULL in that case.
//   - Or use your own allocator, and set `free` + `userdata` — the host
//     will call `free(userdata)` after copy.
typedef struct rsm_tool_result {
    const char*   result_json;      // UTF-8 JSON value; null on error
    int32_t       error_code;       // 0 == success
    const char*   error_message;    // null on success

    // Optional custom deallocator. If both `free` and userdata are NULL and
    // strings were not allocated via host->alloc, they must remain valid
    // for the lifetime of the plugin (static strings are fine).
    void        (*free)(void* userdata);
    void*         userdata;
} rsm_tool_result;

typedef rsm_tool_result (*rsm_tool_handler)(
    rsm_session*   session,       // opaque; pass to session_* host helpers
    const char*    params_json,   // UTF-8; validated against the tool's input schema by the AI
    void*          user           // whatever was in rsm_tool_desc.user
);

typedef struct rsm_tool_desc {
    // MCP tool name. MUST start with "plugin_<manifest.name>_".
    // Registration is rejected otherwise.
    const char*      name;
    const char*      title;                // human title
    const char*      description;          // shown to the AI in tools/list
    const char*      input_schema_json;    // MCP inputSchema, UTF-8 JSON
    const char*      output_schema_json;   // optional, may be NULL
    rsm_safety_class safety;               // will be capped at plugin cap
    rsm_tool_handler handler;
    void*            user;                 // opaque, passed to handler
} rsm_tool_desc;

typedef struct rsm_plugin_manifest {
    uint32_t         abi_major;    // == RSM_PLUGIN_ABI_MAJOR
    uint32_t         abi_minor;    // <= host abi_minor
    const char*      name;         // [a-z0-9_]{1,32}; becomes tool-name prefix
    const char*      version;      // freeform; typically semver
    const char*      description;  // one-liner shown in host startup log
    rsm_safety_class max_safety;   // ceiling this plugin promises not to exceed
                                   // (host may cap lower still)
} rsm_plugin_manifest;

// Host API — the function table passed to rsm_plugin_init_v1.
// Additive-only: new function pointers can be appended in future minor
// versions after the reserved slots. Plugins must NEVER call a slot whose
// address is NULL (check host->abi_minor before calling anything added
// after v1.0).
typedef struct rsm_host_api_v1 {
    uint32_t abi_major;                   // == RSM_PLUGIN_ABI_MAJOR
    uint32_t abi_minor;                   // host's minor (may exceed plugin's)

    // Structured log line, tagged in host log as "plugin/<manifest.name>".
    void  (*log)(rsm_plugin_ctx* ctx, rsm_log_level lvl, const char* msg);

    // Copy `desc` and its strings into the host registry. Returns 0 on
    // success; nonzero if the name is invalid, safety exceeds the cap, or
    // the tool name is already registered.
    int   (*register_tool)(rsm_plugin_ctx* ctx, const rsm_tool_desc* desc);

    // Per-session state helpers. Sessions never leak between clients.
    int   (*session_is_armed)(rsm_session* s, const char* kind_and_name);
    void  (*session_arm)     (rsm_session* s, const char* kind_and_name);
    void  (*session_disarm)  (rsm_session* s, const char* kind_and_name);

    // Host-managed allocator for tool result strings. Freed by the host
    // after the result is copied out. Do not use for long-lived allocations.
    void* (*alloc)(size_t bytes);
    void  (*free) (void* p);

    // Reserved for future v1.x additions. MUST be NULL in v1.0. Plugins
    // must not touch these unless host->abi_minor advertises support.
    void* _reserved[8];
} rsm_host_api_v1;

// Exported symbol names the host looks up with GetProcAddress / dlsym.
#define RSM_PLUGIN_MANIFEST_SYM   "rsm_plugin_manifest_v1"
#define RSM_PLUGIN_INIT_SYM       "rsm_plugin_init_v1"
#define RSM_PLUGIN_SHUTDOWN_SYM   "rsm_plugin_shutdown_v1"   // optional

typedef const rsm_plugin_manifest* (*rsm_plugin_manifest_fn)(void);
typedef int  (*rsm_plugin_init_fn)(const rsm_host_api_v1*, rsm_plugin_ctx*);
typedef void (*rsm_plugin_shutdown_fn)(rsm_plugin_ctx*);

// Platform-appropriate export decoration for the plugin side.
#if defined(_WIN32)
#  define RSM_PLUGIN_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#  define RSM_PLUGIN_EXPORT __attribute__((visibility("default")))
#else
#  define RSM_PLUGIN_EXPORT
#endif

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // RSM_PLUGIN_H
