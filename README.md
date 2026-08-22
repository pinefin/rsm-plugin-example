# rsm-plugin-example

A working plugin for [reversal-suite-mcp][rsm] and a **reference / template**
for anyone (human or LLM) writing their own plugin.

The example plugin is `plugin_sysinfo` — a small system-information plugin
built on the public plugin ABI. It exposes three MCP tools once the host
loads it:

| Tool                        | Safety class  | What it does                                                     |
| --------------------------- | ------------- | ---------------------------------------------------------------- |
| `plugin_sysinfo_processes`  | `local_probe` | Enumerate running processes (`pid`, `name`).                     |
| `plugin_sysinfo_modules`    | `local_probe` | List modules (DLLs) loaded in a given `pid`.                     |
| `plugin_sysinfo_cpuid`      | `safe`        | Return CPU vendor / brand string / common feature bits.          |

Total plugin source: ~300 lines of C++ with zero external dependencies.

[rsm]: https://github.com/pinefin/reversal-suite-mcp

---

## Table of contents

1. [How the plugin system works](#how-the-plugin-system-works)
2. [Install the example](#install-the-example)
3. [Build from source](#build-from-source)
4. [Write your own plugin](#write-your-own-plugin)
   - [Minimum viable plugin](#minimum-viable-plugin)
   - [The public C ABI](#the-public-c-abi)
   - [Tool naming rules](#tool-naming-rules)
   - [Safety classes and the plugin cap](#safety-classes-and-the-plugin-cap)
   - [Memory ownership](#memory-ownership)
   - [JSON I/O](#json-io)
   - [Session helpers](#session-helpers)
   - [Logging](#logging)
5. [ABI stability contract](#abi-stability-contract)
6. [Release process](#release-process)
7. [License](#license)

---

## How the plugin system works

The host (`reversal-suite-mcp.exe`) can load out-of-tree shared libraries at
startup. Each library is a normal Windows DLL that exports **three C symbols**
and links against nothing from the host — everything the plugin needs from
the host arrives as a function-pointer table (`rsm_host_api_v1`) handed in at
init time.

Startup sequence:

```
main()
  ↳ register_all_builtin_tools()      # host's own tools go in first
  ↳ rsm::plugins::load_all(cfg)       # THIS is where plugins load
      ↳ for each *.dll in <plugins-dir>:
          1. SHA-256 the file          → logged to stderr
          2. LoadLibrary                → refuses on ABI mismatch
          3. call  rsm_plugin_manifest_v1()   → validate manifest
          4. cap  manifest.max_safety at LOCAL_PROBE
             (or SYSTEM_STATE if --allow-plugin-elevated)
          5. call  rsm_plugin_init_v1(host_api, ctx)
             ↳ plugin calls host->register_tool(ctx, &desc)  N times
          6. plugin's tools become callable as MCP tools
```

The loader is **off by default**. You must pass `--allow-plugins` for the
host to touch the plugins directory at all. See [the host's SAFETY.md][safety]
for the full policy.

[safety]: https://github.com/pinefin/reversal-suite-mcp/blob/main/SAFETY.md

---

## Install the example

### 1. Grab the DLL

Either build from source (below) or download a release zip from the GitHub
releases page. The zip contains `plugin_sysinfo.dll`, the LICENSE, and a
`SHA256SUMS` file.

### 2. Drop it into the host's plugins directory

Default location is `<exe-dir>/plugins/`, i.e. right next to
`reversal-suite-mcp.exe`:

```
reversal-suite-mcp/build/ninja-release/bin/
├── reversal-suite-mcp.exe
└── plugins/
    └── plugin_sysinfo.dll
```

Override with `--plugins-dir=<path>` if you'd rather keep plugins elsewhere.

### 3. Enable plugins on the host launch line

Add these flags to whatever launches the MCP server (for Claude Code, that's
the `args` array in your `.mcp.json` or `.claude.json` entry):

```json
{
  "command": ".../reversal-suite-mcp.exe",
  "args": [
    "--transport=stdio",
    "--allow-plugins",
    "--allow-local-probe"
  ]
}
```

- `--allow-plugins` — required, or the plugins directory is skipped
- `--allow-local-probe` — required for the `processes` and `modules` tools;
  the `cpuid` tool is `safe` class and runs without it
- (optional) `--no-broker` — if you already have another MCP instance
  running as the shared-tools broker without your plugins loaded, this
  makes the new instance stay isolated

### 4. Verify

On startup you should see something like this on stderr:

```
[plugins] loading plugin `sysinfo` v0.1.0 (sha256=e0c776f6…a13b4f1,
          cap<=local_probe, path=…\plugins\plugin_sysinfo.dll)
[plugins]   "System information (processes, modules, cpuid)"
[registry] register tool `plugin_sysinfo_processes` (safety=local_probe)
[registry] register tool `plugin_sysinfo_modules`   (safety=local_probe)
[registry] register tool `plugin_sysinfo_cpuid`     (safety=safe)
```

### 5. (Optional) Pin the hash

Once you know the SHA-256 the host logs, drop a `plugins.lock.json` next to
the DLL:

```json
{
  "plugin_sysinfo.dll": {
    "sha256": "e0c776f682bc39524d959dea3db3132ed7eae8f8a178dcbbc0aaa8ff4a13b4f1"
  }
}
```

With the lock file present, the loader refuses any DLL that isn't pinned or
whose hash doesn't match — you'll notice immediately if a plugin file is
swapped out.

---

## Build from source

Requires **CMake ≥ 3.20** and a **C++17** toolchain. The release workflow
uses MSVC 2022 (Visual Studio 17); MinGW / clang-cl should work too but
aren't tested in CI.

```sh
git clone https://github.com/pinefin/rsm-plugin-example.git
cd rsm-plugin-example

# From a Visual Studio "x64 Native Tools" prompt:
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Output: `build/plugin_sysinfo.dll`.

---

## Write your own plugin

Copy `vendor/rsm/plugin.h` (or grab it fresh from
`reversal-suite-mcp/include/rsm/plugin.h`) into your own project. That's the
only header you need — no other host code is exposed.

### Minimum viable plugin

Simplest plugin that compiles, loads, registers one tool, and returns a
static JSON response:

```cpp
// hello.cpp — build as a shared library and drop into <plugins-dir>/
#include "rsm/plugin.h"

extern "C" RSM_PLUGIN_EXPORT
const rsm_plugin_manifest* rsm_plugin_manifest_v1(void) {
    static const rsm_plugin_manifest m{
        RSM_PLUGIN_ABI_MAJOR, RSM_PLUGIN_ABI_MINOR,
        "hello",                                  // manifest name (a-z0-9_)
        "0.1.0",                                  // your version
        "Minimal example plugin",                 // one-line description
        RSM_SAFETY_SAFE,                          // max safety class
    };
    return &m;
}

static rsm_tool_result greet(rsm_session*, const char*, void*) {
    rsm_tool_result r{};
    r.result_json = "{\"greeting\":\"hi from a plugin\"}";
    return r;   // static string — no free callback needed
}

extern "C" RSM_PLUGIN_EXPORT
int rsm_plugin_init_v1(const rsm_host_api_v1* host, rsm_plugin_ctx* ctx) {
    if (!host || host->abi_major != RSM_PLUGIN_ABI_MAJOR) return 1;

    rsm_tool_desc d{};
    d.name              = "plugin_hello_greet";   // MUST be plugin_hello_*
    d.title             = "Say hi";
    d.description       = "Return a canned greeting.";
    d.input_schema_json = "{\"type\":\"object\","
                          "\"properties\":{},"
                          "\"additionalProperties\":false}";
    d.safety            = RSM_SAFETY_SAFE;
    d.handler           = &greet;

    return host->register_tool(ctx, &d) == 0 ? 0 : 2;
}
```

CMakeLists for that plugin:

```cmake
cmake_minimum_required(VERSION 3.20)
project(plugin_hello CXX)
set(CMAKE_CXX_STANDARD 17)

add_library(plugin_hello SHARED hello.cpp)
target_include_directories(plugin_hello PRIVATE vendor)   # where rsm/plugin.h lives
set_target_properties(plugin_hello PROPERTIES PREFIX "")
```

That's the whole thing.

### The public C ABI

Everything is declared in `vendor/rsm/plugin.h`. The full public surface is:

**Three exports every plugin must provide:**

| Symbol                       | Signature                                                                        | Notes                                          |
| ---------------------------- | -------------------------------------------------------------------------------- | ---------------------------------------------- |
| `rsm_plugin_manifest_v1`     | `const rsm_plugin_manifest* (*)(void)`                                           | Returns a static manifest. Called first.       |
| `rsm_plugin_init_v1`         | `int (*)(const rsm_host_api_v1* host, rsm_plugin_ctx* ctx)`                      | Registers tools. Returns 0 on success.         |
| `rsm_plugin_shutdown_v1`     | `void (*)(rsm_plugin_ctx* ctx)`                                                  | Optional. Called at host exit before unload.   |

**Function table the host hands you (`rsm_host_api_v1`):**

```c
struct rsm_host_api_v1 {
    uint32_t abi_major;                             // == RSM_PLUGIN_ABI_MAJOR
    uint32_t abi_minor;                             // host's minor (>= plugin's)

    void  (*log)(rsm_plugin_ctx*, rsm_log_level, const char*);
    int   (*register_tool)(rsm_plugin_ctx*, const rsm_tool_desc*);

    int   (*session_is_armed)(rsm_session*, const char*);
    void  (*session_arm)     (rsm_session*, const char*);
    void  (*session_disarm)  (rsm_session*, const char*);

    void* (*alloc)(size_t);
    void  (*free) (void*);

    void* _reserved[8];        // future v1.x additions
};
```

**Tool descriptor you fill and pass to `register_tool`:**

```c
struct rsm_tool_desc {
    const char*      name;                 // "plugin_<manifest_name>_*"
    const char*      title;                // human title
    const char*      description;          // shown to the AI in tools/list
    const char*      input_schema_json;    // MCP inputSchema, UTF-8 JSON
    const char*      output_schema_json;   // optional, may be NULL
    rsm_safety_class safety;               // safe | analytical | local_probe | system_state
    rsm_tool_handler handler;              // the C function pointer
    void*            user;                 // opaque, passed back to handler
};
```

**Handler signature:**

```c
typedef rsm_tool_result (*rsm_tool_handler)(
    rsm_session* session,        // opaque — pass to session_* helpers
    const char*  params_json,    // UTF-8 JSON, validated by the AI against your schema
    void*        user            // whatever you put in rsm_tool_desc.user
);
```

### Tool naming rules

Every tool name **must** start with `plugin_<manifest.name>_`. The loader
rejects any registration that doesn't. This makes it impossible for a plugin
to shadow a builtin, and makes it obvious in the AI's tool catalog which
tools came from where.

- Manifest name: `[a-z0-9_]{1,32}` (enforced)
- Tool name: `plugin_<name>_<anything>` (only the prefix is checked)

Example: manifest name `sysinfo` → tools must be `plugin_sysinfo_processes`,
`plugin_sysinfo_cpuid`, etc.

### Safety classes and the plugin cap

The host defines four safety classes:

| Class            | Meaning                                                | Flag needed on session  |
| ---------------- | ------------------------------------------------------ | ----------------------- |
| `safe`           | Pure computation. Always allowed.                      | (none)                  |
| `analytical`     | Reads user-chosen files, sandboxed math. Always on.    | (none)                  |
| `local_probe`    | Enumerate this machine, read own memory.               | `--allow-local-probe`   |
| `system_state`   | Mutate system state (services, HKLM edits).            | `--allow-system-state`  |

The loader **caps every plugin at `local_probe`** by default. To register a
`system_state`-class tool, the host operator must also pass
`--allow-plugin-elevated`. A plugin that declares `max_safety = system_state`
in its manifest but runs on a host without `--allow-plugin-elevated` will
have every `system_state` registration rejected — so declare accurately and
your plugin degrades gracefully rather than failing to load entirely.

If a session's flags can't satisfy a tool's safety class, that tool is
hidden from `tools/list` for that session (the AI doesn't see it at all,
so it can't call something that would just refuse).

### Memory ownership

`rsm_tool_result` uses a small ownership contract to keep the boundary
clean:

```c
struct rsm_tool_result {
    const char*   result_json;      // UTF-8 JSON value; NULL on error
    int32_t       error_code;       // 0 == success
    const char*   error_message;    // NULL on success
    void        (*free)(void*);     // optional deallocator (called by host)
    void*         userdata;         // opaque, passed to `free`
};
```

**Three patterns you can use:**

**A. Static / string-literal result.** Simplest, zero allocation:

```c
rsm_tool_result r{};
r.result_json = "{\"ok\":true}";     // valid for the process lifetime
// r.free stays NULL — host will not try to free anything
return r;
```

**B. Host-allocated blob.** Recommended when the response is dynamic:

```c
// Allocate everything through host->alloc so the host's CRT owns the memory.
struct owned { char* result_json; char* error_message; };
auto* o = static_cast<owned*>(host->alloc(sizeof(owned)));
o->result_json   = /* strcpy into host->alloc(len+1) */;
o->error_message = nullptr;

rsm_tool_result r{};
r.result_json = o->result_json;
r.free        = &owned_free;   // frees the blob AND its strings
r.userdata    = o;
return r;
```

`owned_free` walks the struct and calls `host->free` on each allocation —
see `src/plugin_sysinfo.cpp` for the working implementation.

**C. Plugin-owned static / pooled buffer.** If your plugin manages its own
memory, set `r.free = NULL` and keep the buffer valid for the entire
process lifetime (or refill on each call from a thread-local scratch pool
that outlives the return).

**⚠️ Never mix allocators across the boundary.** Memory obtained from
`host->alloc` **must** be freed via `host->free` — never `std::free`,
never `HeapFree`. And memory obtained from `malloc` inside the plugin
DLL must be freed inside the plugin DLL. Crossing the CRT boundary is
undefined behavior on Windows.

### JSON I/O

- **Params in:** the host passes `params_json` as a UTF-8, null-terminated
  string containing the JSON that the AI provided as `arguments`. It has
  already been validated against your `input_schema_json`, so you can
  parse it without further shape checks (but do check anything the schema
  can't express, like value ranges).
- **Result out:** `result_json` is a bare JSON *value* — an object, array,
  string, number, or bool. The host wraps it into a proper MCP
  `tools/call` result envelope with `content[]` and `structuredContent`.
- **Errors:** set `error_code` non-zero and `error_message` to a human
  string. `result_json` should be `NULL`. The host surfaces this as
  `isError: true` in the MCP response.

You don't have to depend on nlohmann/json or any other library — small
plugins can hand-format JSON (see `src/plugin_sysinfo.cpp` for a minimal
manual writer with proper escaping). Larger plugins should use a real
parser; the ABI has no opinion.

### Session helpers

Every handler gets an `rsm_session*` — an opaque handle to the calling
MCP client's session. Two things you can do with it via `host->`:

- `session_is_armed(session, "kind/name")` — has this session armed the
  given named backend?
- `session_arm(session, "kind/name")` / `session_disarm(...)` — flip that
  arm flag.

Sessions are isolated between clients; nothing you store in one client's
session leaks to another. This is the mechanism the built-in `emulator`
tool uses to require an explicit arm-step before dangerous operations.

### Logging

`host->log(ctx, RSM_LOG_INFO, "message")` writes into the host's log
stream, tagged as `plugin/<your-manifest-name>`. Levels are
`RSM_LOG_TRACE/DEBUG/INFO/WARN/ERROR`. Messages are treated as literal
strings — safe to include `{}` sequences, they won't be interpreted as
format placeholders.

Log to stderr / the log stream is fine; **never** write to stdout — that
channel carries the MCP JSON-RPC protocol and any stray bytes there will
break the transport.

### Admin elevation on Windows

`--allow-plugin-elevated` raises the plugin **safety-class cap** from
`local_probe` to `system_state` — but it does **not** give the host process
an admin token. Windows does not allow a process to elevate its own token
after launch; elevation is a shell-mediated action that spawns a *new*
process. Since the MCP server is glued to its parent's stdio pipes, a
mid-flight re-launch would break the transport. So the plugin ABI does
not (and can not) offer a "please elevate me" call.

If your plugin needs an admin-only Win32 call, here are the patterns that
actually work, in decreasing order of how sane they are:

1. **Launch the host elevated in the first place.** Right-click
   `reversal-suite-mcp.exe` → *Run as administrator*, or configure your
   launcher (`.mcp.json`, a shortcut, a scheduled task) to start it
   elevated. Simplest thing that works — one UAC prompt at startup and
   every plugin runs with the token it needs.

2. **Ship a helper Windows service.** Register a small service as SYSTEM
   or LocalService at install time (this is when the *installer* prompts
   for admin, once). Your plugin acts as a client — talks to the service
   over a named pipe (`\\.\pipe\<your-plugin>`) or LPC, service does the
   privileged work, plugin marshals the result back. Best pattern for
   anything with more than a handful of admin operations. Zero UAC
   prompts at runtime.

3. **Ship a companion `.exe` with `requireAdministrator` in its manifest.**
   Your plugin launches it via
   `ShellExecuteExW({ .lpVerb = L"runas", ... })` per operation. Works,
   but fires a UAC prompt *every time* — painful UX except for rare
   operations.

4. **COM elevation moniker.** Register a COM object with elevation
   metadata; `CoCreateInstance` with `CLSCTX_LOCAL_SERVER` will UAC-prompt
   once and hand you an elevated in-proc-looking handle. More setup than
   option 3, but only one prompt per session.

**Recommended plugin pattern** for admin-requiring tools regardless of
which mechanism you pick: at `rsm_plugin_init_v1` time, detect whether
the host is running elevated (see snippet below), then either register
the tool normally (if elevated) or register a "shim" version that always
returns `isError: true` with a clear message telling the operator to
restart the host as administrator. That way the tool still shows up in
`tools/list` so the AI knows the capability exists, and the failure is
diagnostic rather than mysterious.

```cpp
// Detect elevation at init — pure Win32, no host help needed.
static bool host_is_elevated() {
    HANDLE tok = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) return false;
    TOKEN_ELEVATION e{};
    DWORD sz = sizeof(e);
    bool ok = GetTokenInformation(tok, TokenElevation, &e, sz, &sz) && e.TokenIsElevated;
    CloseHandle(tok);
    return ok;
}
```

Never assume you're elevated because `--allow-plugin-elevated` was
passed — that flag governs the safety cap, not the token.

---

## ABI stability contract

The header declares `RSM_PLUGIN_ABI_MAJOR` and `RSM_PLUGIN_ABI_MINOR`.

- **Major bump** (v2.0) means the layout of one of the ABI structs changed
  incompatibly. Plugins built against v1 will be rejected by a v2 host.
  A major bump should be extremely rare.
- **Minor bump** (v1.1, v1.2, …) means additive-only changes: new function
  pointers appended after the `_reserved` slots, new manifest fields at
  the end. A v1.N plugin runs on a v1.M host for any M ≥ N. When the host
  hands you the API table, `host->abi_minor` tells you what's available;
  never call a function pointer that came from a slot introduced after
  your compiled-against minor without checking.

The `_reserved` slots in `rsm_host_api_v1` are guaranteed to be `NULL` in
v1.0 and can be used to detect an old host at runtime if you need to.

---

## Release process

Tag a version, push the tag, and CI produces a zip:

```sh
git tag v0.1.0
git push origin v0.1.0
```

The workflow at `.github/workflows/release.yml` runs on `windows-2022`,
builds Release with MSVC 2022, and uploads:

```
plugin_sysinfo-v0.1.0-win-x64.zip
├── plugin_sysinfo.dll
├── README.md
├── LICENSE
└── SHA256SUMS        # printed hash of the DLL for verification / pinning
```

`workflow_dispatch` is also enabled — run the release workflow manually
from the Actions tab and it'll produce the zip as a workflow artifact
without cutting a GitHub release.

---

## License

MIT — see `LICENSE`. Use as a starter for your own plugin, fork it,
strip it down to whatever you need. No attribution required.
