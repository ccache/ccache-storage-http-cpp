// SPDX-License-Identifier: BSD-3-Clause
// Copyright 2026 Joel Rosdahl

#include "ipc_server.hpp"

#include "logger.hpp"

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <string_view>

#ifndef _WIN32
#  include <sys/stat.h>
#  include <unistd.h>
#endif

namespace {

constexpr uint8_t PROTOCOL_VERSION = 0x01;
constexpr uint8_t CAP_GET_PUT_REMOVE_STOP = 0x00;

constexpr uint8_t STATUS_OK = 0x00;
constexpr uint8_t STATUS_NOOP = 0x01;
constexpr uint8_t STATUS_ERR = 0x02;

constexpr uint8_t REQ_GET = 0x00;
constexpr uint8_t REQ_PUT = 0x01;
constexpr uint8_t REQ_REMOVE = 0x02;
constexpr uint8_t REQ_STOP = 0x03;

constexpr uint8_t PUT_FLAG_OVERWRITE = 0x01;
constexpr size_t MAX_MSG_LEN = 255;

static std::string format_hex(const uint8_t* data, size_t len)
{
  std::ostringstream oss;
  oss << std::hex << std::setfill('0');
  for (size_t i = 0; i < len; ++i) {
    oss << std::setw(2) << static_cast<int>(data[i]);
  }
  return oss.str();
}

static uint64_t read_u64_host_byte_order(const uint8_t* data)
{
  uint64_t value;
  std::memcpy(&value, data, sizeof(value));
  return value;
}

static void write_u64_host_byte_order(std::vector<uint8_t>& buf, uint64_t value)
{
  size_t offset = buf.size();
  buf.resize(offset + sizeof(value));
  std::memcpy(buf.data() + offset, &value, sizeof(value));
}

} // namespace

IpcServer::IpcServer(uv_loop_t& loop, const Config& config, StorageClient& storage_client)
  : _loop(loop),
    _config(config),
    _storage_client(storage_client)
{
}

bool IpcServer::init()
{
  int r = uv_pipe_init(&_loop, &_server_pipe, 0);
  if (r != 0) {
    LOG("Failed to initialize pipe: " + std::string(uv_strerror(r)));
    return false;
  }
  _server_pipe.data = this;

#ifdef _WIN32
  r = uv_pipe_bind(&_server_pipe, _config.ipc_endpoint.c_str());
#else
  unlink(_config.ipc_endpoint.c_str());
  mode_t old_umask = umask(0077);
  r = uv_pipe_bind(&_server_pipe, _config.ipc_endpoint.c_str());
  umask(old_umask);
#endif

  if (r != 0) {
    LOG("Failed to bind to IPC endpoint: " + std::string(uv_strerror(r)));
    return false;
  }

  r = uv_listen(reinterpret_cast<uv_stream_t*>(&_server_pipe), 128, on_new_connection);
  if (r != 0) {
    LOG("Failed to listen on IPC endpoint: " + std::string(uv_strerror(r)));
    return false;
  }

  r = uv_timer_init(&_loop, &_idle_timer);
  if (r != 0) {
    LOG("Failed to initialize idle timer: " + std::string(uv_strerror(r)));
    return false;
  }

  _idle_timer.data = this;
  reset_idle_timer();

  LOG("IPC server listening on " + _config.ipc_endpoint);
  return true;
}

void IpcServer::stop()
{
  LOG("Shutting down");
  uv_stop(&_loop); // unlinks the socket as well
}

void IpcServer::reset_idle_timer()
{
  if (_config.idle_timeout_seconds == 0) {
    return;
  }
  uint64_t timeout_ms = static_cast<uint64_t>(_config.idle_timeout_seconds) * 1000;
  uv_timer_start(&_idle_timer, on_idle_timeout, timeout_ms, 0);
}

void IpcServer::on_idle_timeout(uv_timer_t* handle)
{
  IpcServer* server = static_cast<IpcServer*>(handle->data);
  LOG("Idle timeout reached, shutting down");
  server->stop();
}

void IpcServer::on_new_connection(uv_stream_t* server_stream, int status)
{
  if (status < 0) {
    LOG("Connection error: " + std::string(uv_strerror(status)));
    return;
  }

  IpcServer* server = static_cast<IpcServer*>(server_stream->data);
  server->reset_idle_timer();

  auto client = std::make_unique<ClientConnection>();
  client->server = server;

  int r = uv_pipe_init(&server->_loop, &client->handle, 0);
  if (r != 0) {
    LOG("Failed to initialize client pipe: " + std::string(uv_strerror(r)));
    return;
  }
  client->handle.data = client.get();

  r = uv_accept(server_stream, reinterpret_cast<uv_stream_t*>(&client->handle));
  if (r != 0) {
    LOG("Failed to accept connection: " + std::string(uv_strerror(r)));
    uv_close(reinterpret_cast<uv_handle_t*>(&client->handle), on_close);
    client.release(); // Will be deleted in on_close
    return;
  }

  LOG("Client connected");

  // Send greeting: version(u8) + num_capabilities(u8) + capabilities...
  std::vector<uint8_t> greeting = {PROTOCOL_VERSION, 1, CAP_GET_PUT_REMOVE_STOP};
  server->send_response(*client, std::move(greeting));

  r = uv_read_start(reinterpret_cast<uv_stream_t*>(&client->handle), alloc_buffer, on_client_read);
  if (r != 0) {
    LOG("Failed to start reading: " + std::string(uv_strerror(r)));
    uv_close(reinterpret_cast<uv_handle_t*>(&client->handle), on_close);
  }

  client.release(); // Ownership transferred to libuv callbacks
}

void IpcServer::alloc_buffer(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf)
{
  ClientConnection* client = static_cast<ClientConnection*>(handle->data);
  client->alloc_buf.resize(suggested_size);
  buf->base = client->alloc_buf.data();
  buf->len = suggested_size;
}

void IpcServer::on_client_read(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf)
{
  if (nread > 0) {
    ClientConnection* client = static_cast<ClientConnection*>(stream->data);
    IpcServer* server = client->server;
    server->reset_idle_timer();
    client->read_buf.insert(client->read_buf.end(), buf->base, buf->base + nread);
    server->process_client_data(*client);
  } else if (nread < 0) {
    if (nread != UV_EOF) {
      LOG("Read error: " + std::string(uv_strerror(static_cast<int>(nread))));
    }
    uv_close(reinterpret_cast<uv_handle_t*>(stream), on_close);
  }
}

void IpcServer::process_client_data(ClientConnection& client)
{
  auto& buf = client.read_buf;

  while (!buf.empty()) {
    const uint8_t* data = buf.data();
    size_t len = buf.size();
    uint8_t request_type = data[0];
    size_t offset = 1;

    if (request_type == REQ_STOP) {
      buf.erase(buf.begin(), buf.begin() + offset);
      LOG("STOP request received");
      send_response(client, std::vector<uint8_t>{STATUS_OK});
      stop();
      return;
    }

    if (request_type != REQ_GET && request_type != REQ_PUT && request_type != REQ_REMOVE) {
      LOG("Unknown request type: " + std::to_string(request_type));
      stop();
      return;
    }

    if (len < offset + 1) {
      return; // incomplete message
    }
    uint8_t key_len = data[offset++];
    if (len < offset + key_len) {
      return; // incomplete message
    }
    std::string hex_key = format_hex(data + offset, key_len);
    offset += key_len;

    switch (request_type) {
    case REQ_GET:
      LOG("GET request for key " + hex_key);
      _storage_client.get(hex_key, [&](StorageResponse&& response) {
        if (response.result == StorageResult::OK) {
          std::vector<uint8_t> header;
          header.reserve(9);
          header.push_back(STATUS_OK);
          write_u64_host_byte_order(header, response.data.size());
          send_response(client, std::move(header));
          send_response(client, std::move(response.data));
        } else {
          send_simple_response(client, "GET", response);
        }
      });
      break;

    case REQ_PUT: {
      if (len < offset + 1) {
        return; // incomplete message
      }
      uint8_t flags = data[offset++];
      if (len < offset + sizeof(uint64_t)) {
        return; // incomplete message
      }

      uint64_t value_len = read_u64_host_byte_order(data + offset);
      offset += sizeof(uint64_t);
      if (len < offset + value_len) {
        return; // incomplete message
      }

      std::vector<uint8_t> value(data + offset, data + offset + value_len);
      offset += value_len;
      bool overwrite = (flags & PUT_FLAG_OVERWRITE) != 0;
      LOG("PUT request for key " + hex_key + " (" + std::to_string(value.size()) + " bytes)");

      _storage_client.put(hex_key, std::move(value), overwrite, [&](StorageResponse&& response) {
        send_simple_response(client, "PUT", response);
      });
      break;
    }

    case REQ_REMOVE:
      LOG("REMOVE request for key " + hex_key);
      _storage_client.remove(hex_key, [&](StorageResponse&& response) {
        send_simple_response(client, "REMOVE", response);
      });
      break;
    }

    buf.erase(buf.begin(), buf.begin() + offset);
  }
}

void IpcServer::send_simple_response(ClientConnection& client,
                                     const std::string& operation,
                                     const StorageResponse& response)
{
  switch (response.result) {
  case StorageResult::OK:
    send_response(client, std::vector<uint8_t>{STATUS_OK});
    break;
  case StorageResult::NOOP:
    send_response(client, std::vector<uint8_t>{STATUS_NOOP});
    break;
  case StorageResult::ERROR:
    LOG(operation + " failed: " + response.error);
    std::vector<uint8_t> err_resp;
    err_resp.push_back(STATUS_ERR);
    uint8_t msg_len = std::min(response.error.size(), MAX_MSG_LEN);
    err_resp.push_back(msg_len);
    err_resp.insert(err_resp.end(), response.error.begin(), response.error.begin() + msg_len);
    send_response(client, err_resp);
    break;
  }
}

void IpcServer::flush_write_queue(ClientConnection& client)
{
  if (client.writing || client.write_queue.empty()) {
    return;
  }

  client.writing = true;
  auto write_req = std::make_unique<uv_write_t>();
  auto data = std::make_unique<std::vector<uint8_t>>(std::move(client.write_queue.front()));
  client.write_queue.erase(client.write_queue.begin());

  uv_buf_t buf =
    uv_buf_init(reinterpret_cast<char*>(data->data()), static_cast<unsigned int>(data->size()));
  write_req->data = data.get();

  auto stream = reinterpret_cast<uv_stream_t*>(&client.handle);
  int r = uv_write(write_req.get(), stream, &buf, 1, on_write_complete);
  if (r != 0) {
    LOG("Write error: " + std::string(uv_strerror(r)));
    client.writing = false;
    return;
  }

  // uv_write succeeded; data and write_req will be deleted in on_write_complete
  data.release();
  write_req.release();
}

void IpcServer::on_write_complete(uv_write_t* req, int status)
{
  if (status < 0) {
    LOG("Write failed: " + std::string(uv_strerror(status)));
  }
  auto client = static_cast<ClientConnection*>(req->handle->data);
  client->writing = false;
  client->server->flush_write_queue(*client);
  delete static_cast<std::vector<uint8_t>*>(req->data);
  delete req;
}

void IpcServer::on_close(uv_handle_t* handle)
{
  LOG("Client disconnected");
  delete static_cast<ClientConnection*>(handle->data);
}
