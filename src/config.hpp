// SPDX-License-Identifier: BSD-3-Clause
// Copyright 2026 Joel Rosdahl

#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#ifndef _WIN32
#  include <sys/types.h>
#endif

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

  // Attributes from CRSH_ATTR_*
  std::optional<std::string> bearer_token;
  UrlLayout layout = UrlLayout::SUBDIRS;
  std::vector<std::pair<std::string, std::string>> headers;
};

std::optional<Config> parse_config();
