// SPDX-License-Identifier: BSD-3-Clause
// Copyright 2026 Joel Rosdahl

#include "config.hpp"

#include "logger.hpp"

#include <charconv>
#include <cstdlib>
#include <sstream>

template<typename T> std::optional<T> parse_int(std::string_view str, int base = 10)
{
  T value{};
  auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), value, base);
  if (ec == std::errc{} && ptr == str.data() + str.size()) {
    return value;
  }
  return std::nullopt;
}

static UrlLayout parse_layout(const std::string& str)
{
  if (str == "bazel") {
    return UrlLayout::BAZEL;
  } else if (str == "flat") {
    return UrlLayout::FLAT;
  } else {
    return UrlLayout::SUBDIRS; // Default
  }
}

std::optional<Config> parse_config()
{
  Config config;

  const char* ipc_endpoint = std::getenv("CRSH_IPC_ENDPOINT");
  if (!ipc_endpoint || ipc_endpoint[0] == '\0') {
    LOG("CRSH_IPC_ENDPOINT not set");
    return std::nullopt;
  }
#ifdef _WIN32
  config.ipc_endpoint = std::string("\\\\.\\pipe\\") + ipc_endpoint;
#else
  config.ipc_endpoint = ipc_endpoint;
#endif

  const char* url = std::getenv("CRSH_URL");
  if (!url || url[0] == '\0') {
    LOG("CRSH_URL not set");
    return std::nullopt;
  }
  config.url = url;

  const char* idle_timeout = std::getenv("CRSH_IDLE_TIMEOUT");
  if (!idle_timeout || idle_timeout[0] == '\0') {
    idle_timeout = "0";
  }
  auto idle_val = parse_int<unsigned int>(idle_timeout);
  if (!idle_val) {
    LOG("CRSH_IDLE_TIMEOUT must be a non-negative integer");
    return std::nullopt;
  }
  config.idle_timeout_seconds = *idle_val;

  const char* num_attr_str = std::getenv("CRSH_NUM_ATTR");
  if (!num_attr_str || num_attr_str[0] == '\0') {
    num_attr_str = "0";
  }
  auto num_attr_val = parse_int<size_t>(num_attr_str);
  if (!num_attr_val) {
    LOG("CRSH_NUM_ATTR must be a non-negative integer");
    return std::nullopt;
  }
  size_t num_attr = *num_attr_val;

  for (size_t i = 0; i < num_attr; ++i) {
    std::string key_env = "CRSH_ATTR_KEY_" + std::to_string(i);
    std::string value_env = "CRSH_ATTR_VALUE_" + std::to_string(i);

    const char* key = std::getenv(key_env.c_str());
    if (!key) {
      LOG(key_env + " not set");
      return std::nullopt;
    }
    const char* value = std::getenv(value_env.c_str());
    if (!value) {
      LOG(value_env + " not set");
      return std::nullopt;
    }

    std::string key_str(key);
    std::string value_str(value);

    if (key_str == "bearer-token") {
      config.bearer_token = value_str;
    } else if (key_str == "layout") {
      config.layout = parse_layout(value_str);
    } else if (key_str == "header") {
      size_t eq_pos = value_str.find('=');
      if (eq_pos != std::string::npos) {
        std::string header_name = value_str.substr(0, eq_pos);
        std::string header_value = value_str.substr(eq_pos + 1);
        config.headers.emplace_back(header_name, header_value);
      }
    }
  }

  return config;
}
