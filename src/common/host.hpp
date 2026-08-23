#pragma once

// Host API access.
//
// The `rsm_host_api_v1*` handed to us in rsm_plugin_init_v1 is stored
// once and read from every tool handler and every alloc helper. We keep
// it behind an accessor so nothing outside common/host.cpp touches the
// raw pointer — makes the write-once contract obvious to readers.

#include "rsm/plugin.h"

namespace plugin_sysinfo {

// Stash the host API pointer. Called exactly once from
// rsm_plugin_init_v1. Passing null is legal (a shutdown-during-init
// path) but every subsequent host() call will crash — the plugin is
// dead by that point either way.
void set_host(const rsm_host_api_v1* h) noexcept;

// Return the host API. Never null after set_host has fired with a
// valid pointer. Marked hot because tool handlers hit it on every
// alloc / log / free.
const rsm_host_api_v1* host() noexcept;

}  // namespace plugin_sysinfo
