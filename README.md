# ccache-storage-http-cpp

A [ccache remote storage helper](https://ccache.dev/storage-helpers.html) for
HTTP/HTTPS storage, written in **C++**.

## Overview

This is a storage helper for [ccache] that enables caching compilation results
on HTTP/HTTPS servers. It implements the [ccache remote storage helper
protocol].

This project aims to:

1. Provide a high-performance, production-ready HTTP(S) ccache storage helper.
2. Serve as an example implementation of a ccache storage helper in **C++**.
   Feel free to use it as a starting point for implementing helpers for other
   storage service protocols.

See also the similar [ccache-remote-http-go] project for an example (and
production ready) **Go** implementation.

[ccache]: https://ccache.dev
[ccache remote storage helper protocol]: https://github.com/jrosdahl/ccache/blob/crsh/doc/remote_storage_helper_spec.md
[ccache-remote-http-go]: https://github.com/ccache/ccache-storage-http-go

## Features

- Supports HTTP and HTTPS
- High-performance concurrent request handling
- HTTP keep-alive for efficient connection reuse
- Cross-platform: Linux, macOS, Windows
- Multiple layout modes: `flat`, `subdirs`, `bazel`
- Bearer token authentication support
- Support for custom HTTP headers
- Optional debug logging

## Installation

The helper should be installed in a [location where ccache searches for helper
programs]. Install it as the name `ccache-storage-http` for HTTP support and/or
`ccache-storage-https` for HTTPS support.

[location where ccache searches for helper programs]: https://github.com/jrosdahl/ccache/blob/crsh/doc/manual.adoc#storage-helper-process

### Building from source

Make sure you have needed dependencies installed:

- [libcurl](https://curl.se/libcurl/)
- [libuv](https://libuv.org)
- [CMake](https://cmake.org) 3.16+
- C++17 compiler

(You can also install dependencies and build the project using
[Conan](https://docs.conan.io/2/).)

Clone the repository:

```bash
git clone https://github.com/ccache/ccache-storage-http-cpp
cd ccache-storage-http-cpp
```

Build and install:

```bash
cmake -B build -D CMAKE_BUILD_TYPE=Release
cmake --build build
cmake --install build
```

This will install both `ccache-storage-http` and `ccache-storage-https` to
`/usr/local/bin`. Pass `-D CMAKE_INSTALL_PREFIX=/example/dir` to `cmake` to
install elsewhere.

## Configuration

The helper is configured via ccache's [`remote_storage` configuration]. The
binary is automatically invoked by ccache when needed.

For example:

```bash
# Set the CCACHE_REMOTE_STORAGE environment variable:
export CCACHE_REMOTE_STORAGE="https://cache.example.com"

# Or set remote_storage in ccache's configuration file:
ccache -o remote_storage="https://cache.example.com"
```

[`remote_storage` configuration]: https://github.com/jrosdahl/ccache/blob/crsh/doc/manual.adoc#remote-storage-backends

See also the [HTTP storage wiki page] for tips on how to set up a storage server.

[HTTP storage wiki page]: https://github.com/ccache/ccache/wiki/HTTP-storage

### Configuration attributes

The helper supports the following custom attributes:

- `@bearer-token`: Bearer token for `Authorization` header
- `@header`: Custom HTTP headers (can be specified multiple times)
- `@layout`: Storage layout mode
  - `subdirs` (default): First 2 hex chars as subdirectory
  - `flat`: All files in root directory
  - `bazel`: Bazel Remote Execution API compatible layout

Example:

```bash
export CCACHE_REMOTE_STORAGE="https://cache.example.com @header=Content-Type=application/octet-stream"
```

## Optional debug logging

You can set the `CRSH_LOGFILE` environment variable to enable debug logging to a
file:

```bash
export CRSH_LOGFILE=/path/to/debug.log
```

Note: The helper process is spawned by ccache, so the environment variable must
be set before ccache is invoked.

## Contributing

Contributions are welcome! Please submit pull requests or open issues on GitHub.
