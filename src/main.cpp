// SPDX-License-Identifier: MIT
// Copyright 2026 Joel Rosdahl

#include "config.hpp"
#include "ipc_server.hpp"
#include "logger.hpp"
#include "storage_client.hpp"
#include "version.hpp"

#include <uv.h>

#ifdef _WIN32
#  include <direct.h>
#else
#  include <unistd.h>
#endif

#include <cstdlib>
#include <iostream>

static constexpr auto USAGE =
  "This is a ccache HTTP(S) storage helper, usually started automatically by ccache\n"
  "when needed. More information here: https://ccache.dev/storage-helpers.html\n"
  "\n"
  "Project: https://github.com/ccache/ccache-storage-http-cpp\n"
  "Version: " PROJECT_VERSION "\n";

int main()
{
  if (!std::getenv("CRSH_IPC_ENDPOINT") || !std::getenv("CRSH_URL")) {
    std::cerr << USAGE;
    return 1;
  }

  init_logger();

  LOG("Starting");

  auto config = parse_config();
  if (!config) {
    LOG("Error: failed to parse configuration");
    return 1;
  }

  uv_loop_t* loop = uv_default_loop();
  if (!loop) {
    LOG("Error: failed to create event loop");
    return 1;
  }

  StorageClient storage_client(*loop, *config);
  if (!storage_client.init()) {
    LOG("Error: failed to initialize storage client");
    return 1;
  }

  IpcServer ipc_server(*loop, *config, storage_client);
  if (!ipc_server.init()) {
    LOG("Error: failed to initialize IPC server");
    return 1;
  }

#ifdef _WIN32
  if (_chdir("C:\\") != 0) {
    LOG("Failed to chdir to C:\\");
  }
#else
  if (chdir("/") != 0) {
    LOG("Failed to chdir to /");
  }
#endif

  int result = uv_run(loop, UV_RUN_DEFAULT);
  LOG("Event loop exited with code " + std::to_string(result));

  uv_walk(
    loop,
    [](uv_handle_t* handle, void* /*arg*/) {
      if (!uv_is_closing(handle)) {
        uv_close(handle, nullptr);
      }
    },
    nullptr);

  // Run loop again to process close callbacks.
  uv_run(loop, UV_RUN_DEFAULT);
  uv_loop_close(loop);

  LOG("Shutdown complete");
  return 0;
}
