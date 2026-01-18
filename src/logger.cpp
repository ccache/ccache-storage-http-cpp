// SPDX-License-Identifier: BSD-3-Clause
// Copyright 2026 Joel Rosdahl

#include "logger.hpp"

#include <cstdlib>
#include <iomanip>

Logger g_logger;

void init_logger()
{
  const char* log_file = std::getenv("CRSH_LOGFILE");
  if (log_file) {
    g_logger.init(log_file);
  }
}

void Logger::init(const std::string& log_file_path)
{
  if (log_file_path.empty()) {
    _enabled = false;
    return;
  }

  _file.open(log_file_path, std::ios::app);
  if (_file.is_open()) {
    _enabled = true;
  }
}

void Logger::log(const std::string& msg)
{
  if (!_enabled || !_file.is_open()) {
    return;
  }

  auto now = std::chrono::system_clock::now();
  auto time = std::chrono::system_clock::to_time_t(now);
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

  struct tm tm;
#ifdef _WIN32
  localtime_s(&tm, &time);
#else
  localtime_r(&time, &tm);
#endif

  _file << "[" << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S") << '.' << std::setfill('0')
        << std::setw(3) << ms.count() << "] " << msg << std::endl;
}
