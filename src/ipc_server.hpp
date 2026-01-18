// SPDX-License-Identifier: BSD-3-Clause
// Copyright 2026 Joel Rosdahl

#pragma once

#include "config.hpp"
#include "storage_client.hpp"

#include <uv.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

class IpcServer;

struct ClientConnection
{
  uv_pipe_t handle;
  IpcServer* server;
  std::vector<uint8_t> read_buf;
  std::vector<char> alloc_buf; // Reusable buffer for libuv reads
  bool writing = false;
  std::vector<std::vector<uint8_t>> write_queue;
};

class IpcServer
{
public:
  IpcServer(uv_loop_t& loop, const Config& config, StorageClient& storage_client);

  bool init();
  void stop();
  void reset_idle_timer();

  template<typename T> void send_response(ClientConnection& client, T&& data)
  {
    client.write_queue.push_back(std::forward<T>(data));
    flush_write_queue(client);
  }

private:
  static void on_new_connection(uv_stream_t* server, int status);
  static void on_client_read(uv_stream_t* client, ssize_t nread, const uv_buf_t* buf);
  static void alloc_buffer(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf);
  static void on_write_complete(uv_write_t* req, int status);
  static void on_close(uv_handle_t* handle);
  static void on_idle_timeout(uv_timer_t* handle);

  void process_client_data(ClientConnection& client);
  void flush_write_queue(ClientConnection& client);
  void send_simple_response(ClientConnection& client,
                            const std::string& operation,
                            const StorageResponse& response);

  uv_loop_t& _loop;
  const Config& _config;
  StorageClient& _storage_client;
  uv_pipe_t _server_pipe;
  uv_timer_t _idle_timer;
};
