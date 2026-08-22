# rsm-plugin-example (sysinfo)

An example plugin for [reversal-suite-mcp](https://github.com/pinefin/reversal-suite-mcp).
Demonstrates how to build an out-of-tree DLL that adds MCP tools to the host at
startup.

The example exposes three tools under the `plugin_sysinfo_*` namespace:

| Tool                        | Safety class  | What it does                                            |
| --------------------------- | ------------- | ------------------------------------------------------- |
| `plugin_sysinfo_processes`  | `local_probe` | Enumerate running processes (`pid`, `name`).            |
| `plugin_sysinfo_modules`    | `local_probe` | List modules loaded in a given `pid`.                   |
| `plugin_sysinfo_cpuid`      | `safe`        | Return CPU vendor, brand string, and feature flags.     |

## Building

Requires CMake ≥ 3.20 and a C++17 toolchain (MSVC 2022 or newer is what the
release workflow uses).

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

The output DLL is `build/Release/plugin_sysinfo.dll`.

## Installing

1. Drop `plugin_sysinfo.dll` into `<reversal-suite-mcp exe dir>/plugins/`
   (or into a folder pointed at by `--plugins-dir=<path>`).
2. Start the host with `--allow-plugins` (and `--allow-local-probe` if you
   want to actually call the process/module enumerators).
3. On startup the host logs the plugin's SHA-256 and manifest before init
   runs. If you want to pin the hash, add an entry to `plugins.lock.json`
   in the plugins directory:

   ```json
   {
     "plugin_sysinfo.dll": { "sha256": "<paste the hex from the startup log>" }
   }
   ```

   Once the lock file exists the host refuses any DLL that isn't pinned or
   whose hash doesn't match.

## Writing your own plugin

The stable ABI lives in a single header at `vendor/rsm/plugin.h`. Copy that
header into your own project and export three symbols:

```c
const rsm_plugin_manifest* rsm_plugin_manifest_v1(void);
int  rsm_plugin_init_v1(const rsm_host_api_v1* host, rsm_plugin_ctx* ctx);
void rsm_plugin_shutdown_v1(rsm_plugin_ctx* ctx);   // optional
```

During `init` you call `host->register_tool(...)` once per tool. Tool
names MUST start with `plugin_<manifest.name>_` — the loader rejects any
that don't.

See `src/plugin_sysinfo.cpp` for a minimal working reference.

## License

MIT — see `LICENSE`.
