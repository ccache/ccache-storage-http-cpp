// SPDX-License-Identifier: BSD-3-Clause
// Copyright 2026 Joel Rosdahl

#include "storage_client.hpp"

#include "logger.hpp"

#include <cstring>
#include <sstream>

namespace {

static std::string build_url(const Config& config, const std::string& hex_key)
{
  std::string base_url = config.url;

  if (base_url.empty() || base_url.back() != '/') {
    base_url += '/';
  }

  std::ostringstream url;
  url << base_url;

  switch (config.layout) {
  case UrlLayout::BAZEL: {
    // Bazel format: ac/ + 64 hex digits, so pad shorter keys by repeating the key prefix to reach
    // the expected SHA256 size.
    constexpr size_t sha256_hex_size = 64;
    url << "ac/";
    if (hex_key.size() >= sha256_hex_size) {
      url << hex_key.substr(0, sha256_hex_size);
    } else {
      url << hex_key << hex_key.substr(0, sha256_hex_size - hex_key.size());
    }
    break;
  }

  case UrlLayout::FLAT:
    url << hex_key;
    break;

  case UrlLayout::SUBDIRS:
    if (hex_key.size() >= 2) {
      url << hex_key.substr(0, 2) << "/" << hex_key.substr(2);
    } else {
      url << hex_key;
    }
    break;
  }

  return url.str();
}

} // namespace

StorageClient::StorageClient(uv_loop_t& loop, const Config& config)
  : _loop(loop),
    _config(config)
{
}

StorageClient::~StorageClient()
{
  if (_multi_handle) {
    for (auto& pair : _active_requests) {
      curl_multi_remove_handle(_multi_handle, pair.first);
      if (pair.second->headers) {
        curl_slist_free_all(pair.second->headers);
      }
      curl_easy_cleanup(pair.first);
    }
    _active_requests.clear();
    curl_multi_cleanup(_multi_handle);
  }
  curl_global_cleanup();
}

bool StorageClient::init()
{
  CURLcode result = curl_global_init(CURL_GLOBAL_DEFAULT);
  if (result != CURLE_OK) {
    LOG("Failed to initialize curl: " + std::string(curl_easy_strerror(result)));
    return false;
  }

  _multi_handle = curl_multi_init();
  if (!_multi_handle) {
    LOG("Failed to initialize curl multi handle");
    return false;
  }

  curl_multi_setopt(_multi_handle, CURLMOPT_SOCKETFUNCTION, socket_callback);
  curl_multi_setopt(_multi_handle, CURLMOPT_SOCKETDATA, this);
  curl_multi_setopt(_multi_handle, CURLMOPT_TIMERFUNCTION, timer_callback);
  curl_multi_setopt(_multi_handle, CURLMOPT_TIMERDATA, this);
  curl_multi_setopt(_multi_handle, CURLMOPT_MAX_HOST_CONNECTIONS, 16L);
  curl_multi_setopt(_multi_handle, CURLMOPT_MAXCONNECTS, 16L);

  uv_timer_init(&_loop, &_timeout_timer);
  _timeout_timer.data = this;

  return true;
}

void StorageClient::get(const std::string& hex_key, StorageCallback&& callback)
{
  auto request = std::make_unique<HttpRequest>();
  request->operation = HttpOperation::GET;
  request->url = build_url(_config, hex_key);
  request->callback = std::move(callback);

  LOG("GET " + request->url);

  CURL* handle = create_easy_handle(request.get());
  if (!handle) {
    request->callback(StorageResponse{StorageResult::ERROR, "Failed to create curl handle", {}});
    return;
  }

  curl_easy_setopt(handle, CURLOPT_HTTPGET, 1L);
  _active_requests[handle] = std::move(request);
  curl_multi_add_handle(_multi_handle, handle);
}

void StorageClient::put(const std::string& hex_key,
                        std::vector<uint8_t>&& data,
                        bool overwrite,
                        StorageCallback&& callback)
{
  LOG("PUT " + hex_key + " (" + std::to_string(data.size())
      + " bytes, overwrite=" + (overwrite ? "true" : "false") + ")");

  if (overwrite) {
    do_put(hex_key, std::move(data), std::move(callback));
  } else {
    std::string url = build_url(_config, hex_key);
    auto request = std::make_unique<HttpRequest>();
    request->operation = HttpOperation::HEAD;
    request->url = url;

    CURL* handle = create_easy_handle(request.get());
    if (!handle) {
      callback(StorageResponse{StorageResult::ERROR, "Failed to create curl handle", {}});
      return;
    }

    request->callback = [this, hex_key, data = std::move(data), callback = std::move(callback)](
                          StorageResponse&& response) mutable {
      if (response.result == StorageResult::NOOP) {
        LOG("HEAD check: resource doesn't exist, proceeding with PUT");
        do_put(hex_key, std::move(data), std::move(callback));
      } else if (response.result == StorageResult::OK) {
        LOG("HEAD check: resource exists, not overwriting");
        callback(StorageResponse{StorageResult::NOOP, "", {}});
      } else {
        callback(std::move(response));
      }
    };

    curl_easy_setopt(handle, CURLOPT_NOBODY, 1L);
    _active_requests[handle] = std::move(request);
    curl_multi_add_handle(_multi_handle, handle);
  }
}

void StorageClient::do_put(const std::string& hex_key,
                           std::vector<uint8_t>&& data,
                           StorageCallback&& callback)
{
  size_t data_size = data.size();
  auto request = std::make_unique<HttpRequest>();
  request->operation = HttpOperation::PUT;
  request->url = build_url(_config, hex_key);
  request->request_data = std::move(data);
  request->callback = std::move(callback);

  CURL* handle = create_easy_handle(request.get());
  if (!handle) {
    request->callback(StorageResponse{StorageResult::ERROR, "Failed to create curl handle", {}});
    return;
  }

  curl_easy_setopt(handle, CURLOPT_UPLOAD, 1L);
  curl_easy_setopt(handle, CURLOPT_INFILESIZE_LARGE, static_cast<curl_off_t>(data_size));
  curl_easy_setopt(handle, CURLOPT_READFUNCTION, read_callback);
  curl_easy_setopt(handle, CURLOPT_READDATA, request.get());
  _active_requests[handle] = std::move(request);
  curl_multi_add_handle(_multi_handle, handle);
}

void StorageClient::remove(const std::string& hex_key, StorageCallback&& callback)
{
  auto request = std::make_unique<HttpRequest>();
  request->operation = HttpOperation::DELETE;
  request->url = build_url(_config, hex_key);
  request->callback = std::move(callback);

  LOG("DELETE " + request->url);

  CURL* handle = create_easy_handle(request.get());
  if (!handle) {
    request->callback(StorageResponse{StorageResult::ERROR, "Failed to create curl handle", {}});
    return;
  }

  curl_easy_setopt(handle, CURLOPT_CUSTOMREQUEST, "DELETE");
  _active_requests[handle] = std::move(request);
  curl_multi_add_handle(_multi_handle, handle);
}

CURL* StorageClient::create_easy_handle(HttpRequest* request)
{
  CURL* handle = curl_easy_init();
  if (!handle) {
    return nullptr;
  }

  curl_easy_setopt(handle, CURLOPT_ERRORBUFFER, request->error_buf);
  curl_easy_setopt(handle, CURLOPT_EXPECT_100_TIMEOUT_MS, 0L);
  curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(handle, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);
  curl_easy_setopt(handle, CURLOPT_MAXREDIRS, 5L);
  curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(handle, CURLOPT_PRIVATE, request);
  curl_easy_setopt(handle, CURLOPT_TCP_KEEPALIVE, 1L);
  curl_easy_setopt(handle, CURLOPT_URL, request->url.c_str());
  curl_easy_setopt(handle, CURLOPT_WRITEDATA, request);
  curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, write_callback);

  curl_slist* headers = nullptr;

  if (_config.bearer_token) {
    std::string auth_header = "Authorization: Bearer " + *_config.bearer_token;
    headers = curl_slist_append(headers, auth_header.c_str());
  }

  for (const auto& header : _config.headers) {
    std::string header_line = header.first + ": " + header.second;
    headers = curl_slist_append(headers, header_line.c_str());
  }

  if (headers) {
    curl_easy_setopt(handle, CURLOPT_HTTPHEADER, headers);
    request->headers = headers;
  }

  return handle;
}

CurlSocketContext* StorageClient::create_socket_context(curl_socket_t sockfd)
{
  auto ctx = new CurlSocketContext;
  ctx->sockfd = sockfd;
  ctx->client = this;
  uv_poll_init_socket(&_loop, &ctx->poll_handle, sockfd);
  ctx->poll_handle.data = ctx;
  return ctx;
}

void StorageClient::destroy_socket_context(CurlSocketContext* ctx)
{
  uv_poll_stop(&ctx->poll_handle);
  uv_close(reinterpret_cast<uv_handle_t*>(&ctx->poll_handle),
           [](uv_handle_t* handle) { delete static_cast<CurlSocketContext*>(handle->data); });
}

void StorageClient::check_multi_info()
{
  CURLMsg* msg;
  int msgs_left;

  while ((msg = curl_multi_info_read(_multi_handle, &msgs_left))) {
    if (msg->msg != CURLMSG_DONE) {
      continue;
    }

    CURL* handle = msg->easy_handle;
    CURLcode result = msg->data.result;
    HttpRequest* request = nullptr;
    curl_easy_getinfo(handle, CURLINFO_PRIVATE, &request);

    long http_code = 0;
    curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &http_code);

    StorageResult http_result = StorageResult::ERROR;
    std::string error;

    if (result != CURLE_OK) {
      error = request->error_buf[0] ? request->error_buf : curl_easy_strerror(result);
      LOG("Curl error: " + error);
      http_result = StorageResult::ERROR;
    } else {
      switch (request->operation) {
      case HttpOperation::GET:
        if (http_code == 200) {
          http_result = StorageResult::OK;
        } else if (http_code == 404) {
          // Not found means key doesn't exist -> NOOP
          http_result = StorageResult::NOOP;
          request->response_data.clear();
        } else {
          error = "HTTP " + std::to_string(http_code);
          http_result = StorageResult::ERROR;
        }
        break;

      case HttpOperation::HEAD:
        // HEAD is used to check if resource exists before PUT.
        if (http_code == 200) {
          // Resource exists -> OK (callback will convert to NOOP if needed)
          http_result = StorageResult::OK;
        } else if (http_code == 404) {
          // Resource doesn't exist -> NOOP (callback will proceed with PUT)
          http_result = StorageResult::NOOP;
        } else {
          // Other HTTP error from HEAD
          error = "HTTP " + std::to_string(http_code);
          http_result = StorageResult::ERROR;
        }
        break;

      case HttpOperation::PUT:
        if (http_code >= 200 && http_code < 300) {
          http_result = StorageResult::OK;
        } else if (http_code == 412 || http_code == 409) {
          // Precondition failed or conflict -> NOOP (key already exists, not overwritten)
          http_result = StorageResult::NOOP;
        } else {
          error = "HTTP " + std::to_string(http_code);
          http_result = StorageResult::ERROR;
        }
        break;

      case HttpOperation::DELETE:
        if (http_code >= 200 && http_code < 300) {
          http_result = StorageResult::OK;
        } else if (http_code == 404) {
          // Key not found -> NOOP (nothing to remove)
          http_result = StorageResult::NOOP;
        } else {
          error = "HTTP " + std::to_string(http_code);
          http_result = StorageResult::ERROR;
        }
        break;
      }
    }

    LOG("Request completed: " + request->url + " HTTP " + std::to_string(http_code));

    auto it = _active_requests.find(handle);
    if (it != _active_requests.end()) {
      auto req = std::move(it->second);
      _active_requests.erase(it);
      curl_multi_remove_handle(_multi_handle, handle);
      if (req->headers) {
        curl_slist_free_all(req->headers);
      }
      curl_easy_cleanup(handle);
      req->callback(StorageResponse{http_result, std::move(error), std::move(req->response_data)});
    }
  }
}

int StorageClient::socket_callback(
  CURL* /*handle*/, curl_socket_t s, int what, void* userp, void* socketp)
{
  StorageClient* client = static_cast<StorageClient*>(userp);
  CurlSocketContext* ctx = static_cast<CurlSocketContext*>(socketp);

  if (what == CURL_POLL_REMOVE) {
    if (ctx) {
      client->destroy_socket_context(ctx);
    }
    return 0;
  }

  if (!ctx) {
    ctx = client->create_socket_context(s);
    curl_multi_assign(client->_multi_handle, s, ctx);
  }

  int events = 0;
  if (what & CURL_POLL_IN) {
    events |= UV_READABLE;
  }
  if (what & CURL_POLL_OUT) {
    events |= UV_WRITABLE;
  }

  uv_poll_start(&ctx->poll_handle, events, on_poll);

  return 0;
}

int StorageClient::timer_callback(CURLM* /*handle*/, long timeout_ms, void* userp)
{
  StorageClient* client = static_cast<StorageClient*>(userp);

  if (timeout_ms < 0) {
    uv_timer_stop(&client->_timeout_timer);
  } else {
    uv_timer_start(&client->_timeout_timer, on_timeout, timeout_ms, 0);
  }

  return 0;
}

void StorageClient::on_timeout(uv_timer_t* handle)
{
  StorageClient* client = static_cast<StorageClient*>(handle->data);
  int running_handles;
  curl_multi_socket_action(client->_multi_handle, CURL_SOCKET_TIMEOUT, 0, &running_handles);
  client->check_multi_info();
}

void StorageClient::on_poll(uv_poll_t* handle, int status, int events)
{
  CurlSocketContext* ctx = static_cast<CurlSocketContext*>(handle->data);
  StorageClient* client = ctx->client;

  int flags = 0;
  if (status < 0) {
    flags = CURL_CSELECT_ERR;
  } else {
    if (events & UV_READABLE) {
      flags |= CURL_CSELECT_IN;
    }
    if (events & UV_WRITABLE) {
      flags |= CURL_CSELECT_OUT;
    }
  }

  int running_handles;
  curl_multi_socket_action(client->_multi_handle, ctx->sockfd, flags, &running_handles);
  client->check_multi_info();
}

size_t StorageClient::write_callback(char* ptr, size_t size, size_t nmemb, void* userdata)
{
  HttpRequest* request = static_cast<HttpRequest*>(userdata);
  size_t total = size * nmemb;
  request->response_data.insert(request->response_data.end(), ptr, ptr + total);
  return total;
}

size_t StorageClient::read_callback(char* ptr, size_t size, size_t nmemb, void* userdata)
{
  HttpRequest* request = static_cast<HttpRequest*>(userdata);

  size_t max_bytes = size * nmemb;
  const std::vector<uint8_t>& data = request->request_data;
  size_t remaining = (request->upload_pos < data.size()) ? (data.size() - request->upload_pos) : 0;
  size_t to_copy = std::min(remaining, max_bytes);
  if (to_copy > 0) {
    std::memcpy(ptr, data.data() + request->upload_pos, to_copy);
    request->upload_pos += to_copy;
  }

  return to_copy;
}
