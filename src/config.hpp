// SPDX-License-Identifier: MIT
// Copyright 2026 Joel Rosdahl

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

enum class UrlLayout {
  BAZEL,   // ac/ + 64 hex digits
  FLAT,    // key directly appended
  SUBDIRS, // first 2 chars / rest of key
};

struct Config
{
  std::string ipc_endpoint;
  std::string url;
  unsigned int idle_timeout_seconds = 0;

  std::vector<std::string> diagnostics;

  // Attributes from CRSH_ATTR_*
  std::optional<std::string> bearer_token;
  std::optional<std::string> bearer_token_file;
  std::optional<long> connection_pool_size;
  UrlLayout layout = UrlLayout::SUBDIRS;
  std::vector<std::pair<std::string, std::string>> headers;
  bool use_netrc = false;
  std::optional<std::string> netrc_file;

  // TLS certificate store overrides from the environment.
  std::optional<std::string> ssl_cert_file;
  std::optional<std::string> ssl_cert_dir;
};

std::optional<Config> parse_config();
