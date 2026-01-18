// SPDX-License-Identifier: BSD-3-Clause
// Copyright 2026 Joel Rosdahl

#pragma once

#include <chrono>
#include <ctime>
#include <fstream>
#include <string>

class Logger
{
public:
  Logger() = default;

  Logger(const Logger&) = delete;
  Logger& operator=(const Logger&) = delete;

  void init(const std::string& log_file_path);
  void log(const std::string& msg);

  bool is_enabled() const { return _enabled; }

private:
  std::ofstream _file;
  bool _enabled = false;
};

extern Logger g_logger;

void init_logger();

#define LOG(msg)                                                                                   \
  do {                                                                                             \
    if (g_logger.is_enabled()) {                                                                   \
      g_logger.log(msg);                                                                           \
    }                                                                                              \
  } while (false)
