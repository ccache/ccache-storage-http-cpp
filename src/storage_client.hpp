// SPDX-License-Identifier: BSD-3-Clause
// Copyright 2026 Joel Rosdahl

#pragma once

#include "config.hpp"

#include <curl/curl.h>
#include <uv.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

enum class StorageResult { OK, NOOP, ERROR };

struct StorageResponse
{
  StorageResult result;
  std::string error;
  std::vector<uint8_t> data;
};

using StorageCallback = std::function<void(StorageResponse&&)>;

#undef DELETE // needed on Windows
enum class HttpOperation { GET, PUT, DELETE, HEAD };

struct HttpRequest
{
  HttpOperation operation;
  std::string url;
  std::vector<uint8_t> request_data; // For PUT
  std::vector<uint8_t> response_data;
  StorageCallback callback;
  struct curl_slist* headers = nullptr;
  char error_buf[CURL_ERROR_SIZE] = {0};
  size_t upload_pos = 0;
};

class StorageClient;

struct CurlSocketContext
{
  uv_poll_t poll_handle;
  curl_socket_t sockfd;
  StorageClient* client;
};

class StorageClient
{
public:
  StorageClient(uv_loop_t& loop, const Config& config);
  ~StorageClient();

  bool init();

  void get(const std::string& hex_key, StorageCallback&& callback);
  void put(const std::string& hex_key,
           std::vector<uint8_t>&& data,
           bool overwrite,
           StorageCallback&& callback);
  void remove(const std::string& hex_key, StorageCallback&& callback);

private:
  void do_put(const std::string& hex_key, std::vector<uint8_t>&& data, StorageCallback&& callback);
  void check_multi_info();

  CURL* create_easy_handle(HttpRequest* request);
  CurlSocketContext* create_socket_context(curl_socket_t sockfd);
  void destroy_socket_context(CurlSocketContext* ctx);

  // Static callbacks for curl:
  static int socket_callback(CURL* handle, curl_socket_t s, int action, void* userp, void* socketp);
  static int timer_callback(CURLM* multi, long timeout_ms, void* userp);
  static size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata);
  static size_t read_callback(char* ptr, size_t size, size_t nmemb, void* userdata);

  // Static callbacks for libuv:
  static void on_timeout(uv_timer_t* handle);
  static void on_poll(uv_poll_t* handle, int status, int events);

  uv_loop_t& _loop;
  const Config& _config;
  CURLM* _multi_handle = nullptr;
  uv_timer_t _timeout_timer;
  std::unordered_map<CURL*, std::unique_ptr<HttpRequest>> _active_requests;
};
