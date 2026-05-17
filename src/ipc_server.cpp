// SPDX-License-Identifier: MIT
// Copyright 2026 Joel Rosdahl

#include "ipc_server.hpp"

#include "logger.hpp"
#include "version.hpp"

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <sstream>

#ifndef _WIN32
#  include <sys/stat.h>
#  include <unistd.h>
#endif

namespace {

constexpr uint8_t PROTOCOL_VERSION = 0x01;
constexpr uint8_t CAP_GET_PUT_REMOVE = 0x00;
constexpr uint8_t CAP_INFO = 0x01;
constexpr uint8_t CAP_EXISTS = 0x02;

constexpr uint8_t STATUS_OK = 0x00;
constexpr uint8_t STATUS_NOOP = 0x01;
constexpr uint8_t STATUS_ERR = 0x02;

constexpr uint8_t REQ_GET = 0x00;
constexpr uint8_t REQ_PUT = 0x01;
constexpr uint8_t REQ_REMOVE = 0x02;
constexpr uint8_t REQ_STOP = 0x03;
constexpr uint8_t REQ_INFO = 0x04;
constexpr uint8_t REQ_EXISTS = 0x05;

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

  r = uv_listen(reinterpret_cast<uv_stream_t*>(&_server_pipe), 4096, on_new_connection);
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

  auto client = std::make_shared<ClientConnection>();
  client->server = server;

  int r = uv_pipe_init(&server->_loop, &client->handle, 0);
  if (r != 0) {
    LOG("Failed to initialize client pipe: " + std::string(uv_strerror(r)));
    return;
  }
  client->handle.data = client.get();

  // Track client before any uv_close call so on_close always finds a valid shared_ptr
  server->_clients[&client->handle] = client;

  r = uv_accept(server_stream, reinterpret_cast<uv_stream_t*>(&client->handle));
  if (r != 0) {
    LOG("Failed to accept connection: " + std::string(uv_strerror(r)));
    close_client(*client);
    return;
  }

  LOG("Client connected");

  // Send greeting: version(u8) + num_capabilities(u8) + capabilities...
  std::vector<uint8_t> greeting = {PROTOCOL_VERSION, 3, CAP_GET_PUT_REMOVE, CAP_INFO, CAP_EXISTS};
  server->send_response(*client, std::move(greeting));

  r = uv_read_start(reinterpret_cast<uv_stream_t*>(&client->handle), alloc_buffer, on_client_read);
  if (r != 0) {
    LOG("Failed to start reading: " + std::string(uv_strerror(r)));
    close_client(*client);
  }
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
    close_client(*static_cast<ClientConnection*>(stream->data));
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
    std::string hex_key;

    auto parse_key = [&] {
      if (len < offset + 1) {
        return false; // incomplete message
      }
      uint8_t key_len = data[offset++];
      if (len < offset + key_len) {
        return false; // incomplete message
      }
      hex_key = format_hex(data + offset, key_len);
      offset += key_len;
      return true;
    };

    switch (request_type) {
    case REQ_EXISTS: {
      if (!parse_key()) {
        return;
      }
      LOG("EXISTS request for key " + hex_key);
      auto client_ptr = client.shared_from_this();
      _storage_client.exists(hex_key, [this, client_ptr](StorageResponse&& response) {
        if (client_ptr->disconnected) {
          return;
        }
        if (response.result == StorageResult::ERROR) {
          LOG("EXISTS failed: " + response.error);
          std::vector<uint8_t> err_resp;
          err_resp.push_back(STATUS_ERR);
          uint8_t msg_len = static_cast<uint8_t>(std::min(response.error.size(), MAX_MSG_LEN));
          err_resp.push_back(msg_len);
          err_resp.insert(err_resp.end(), response.error.begin(), response.error.begin() + msg_len);
          send_response(*client_ptr, std::move(err_resp));
        } else {
          uint8_t exists = (response.result == StorageResult::OK) ? 0x01 : 0x00;
          send_response(*client_ptr, std::vector<uint8_t>{STATUS_OK, exists});
        }
      });
      break;
    }

    case REQ_INFO: {
      LOG("INFO request");
      std::vector<uint8_t> response;
      std::string identity = std::string("ccache-storage-http-cpp ") + PROJECT_VERSION;
      uint8_t id_len = static_cast<uint8_t>(std::min(identity.size(), MAX_MSG_LEN));
      response.push_back(id_len);
      response.insert(response.end(), identity.begin(), identity.begin() + id_len);
      const auto& diags = _config.diagnostics;
      uint8_t diag_num = static_cast<uint8_t>(std::min(diags.size(), size_t{255}));
      response.push_back(diag_num);
      for (uint8_t i = 0; i < diag_num; ++i) {
        uint8_t msg_len = static_cast<uint8_t>(std::min(diags[i].size(), MAX_MSG_LEN));
        response.push_back(msg_len);
        response.insert(response.end(), diags[i].begin(), diags[i].begin() + msg_len);
      }

      send_response(*client.shared_from_this(), std::move(response));
      break;
    }

    case REQ_GET: {
      if (!parse_key()) {
        return;
      }
      LOG("GET request for key " + hex_key);
      auto client_ptr = client.shared_from_this();
      _storage_client.get(hex_key, [this, client_ptr](StorageResponse&& response) {
        if (client_ptr->disconnected) {
          return;
        }
        if (response.result == StorageResult::OK) {
          std::vector<uint8_t> header;
          header.reserve(9);
          header.push_back(STATUS_OK);
          write_u64_host_byte_order(header, response.data.size());
          send_response(*client_ptr, std::move(header));
          send_response(*client_ptr, std::move(response.data));
        } else {
          send_simple_response(*client_ptr, "GET", response);
        }
      });
      break;
    }

    case REQ_PUT: {
      if (!parse_key()) {
        return;
      }
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

      // buf now contains the header and the full value, and might (in theory
      // but not in practice) also include excess bytes written by the client.
      // To avoid copying the potentially large payload, hand over the full
      // buffer to the storage client.
      size_t excess_bytes = len - offset - value_len;
      DataSlice put_data;
      buf.swap(put_data.storage);
      put_data.offset = offset;
      put_data.size = value_len;
      buf.insert(buf.begin(),
                 data + offset + value_len,
                 data + offset + value_len + excess_bytes); // retain excess bytes

      bool overwrite = (flags & PUT_FLAG_OVERWRITE) != 0;
      LOG("PUT request for key " + hex_key + " (" + std::to_string(value_len) + " bytes)");

      auto client_ptr = client.shared_from_this();
      _storage_client.put(
        hex_key, std::move(put_data), overwrite, [this, client_ptr](StorageResponse&& response) {
          if (client_ptr->disconnected) {
            return;
          }
          send_simple_response(*client_ptr, "PUT", response);
        });
      offset = 0; // we have adjusted buf ourselves
      break;
    }

    case REQ_REMOVE: {
      if (!parse_key()) {
        return;
      }
      LOG("REMOVE request for key " + hex_key);
      auto client_ptr = client.shared_from_this();
      _storage_client.remove(hex_key, [this, client_ptr](StorageResponse&& response) {
        if (client_ptr->disconnected) {
          return;
        }
        send_simple_response(*client_ptr, "REMOVE", response);
      });
      break;
    }

    case REQ_STOP:
      LOG("STOP request received");
      send_response(client, std::vector<uint8_t>{STATUS_OK});
      stop();
      return;

    default:
      LOG("Unknown request type: " + std::to_string(request_type));
      stop();
      return;
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
    send_response(client, std::move(err_resp));
    break;
  }
}

void IpcServer::close_client(ClientConnection& client)
{
  client.disconnected = true;

  auto* handle = reinterpret_cast<uv_handle_t*>(&client.handle);
  if (!uv_is_closing(handle)) {
    uv_close(handle, on_close);
  }
}

void IpcServer::flush_write_queue(ClientConnection& client)
{
  if (client.disconnected || uv_is_closing(reinterpret_cast<uv_handle_t*>(&client.handle))) {
    client.write_queue.clear();
    return;
  }

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
  auto* client = static_cast<ClientConnection*>(handle->data);
  // Remove from _clients map; shared_ptr prevent premature deletion
  // if callbacks are still pending
  client->server->_clients.erase(reinterpret_cast<uv_pipe_t*>(handle));
}
