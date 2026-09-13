# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## Unreleased

## [0.11] - 2026-09-13

[0.11]: https://github.com/ccache/ccache-storage-http-cpp/releases/tag/v0.11

### Added

- New `connection-pool-size` attribute for setting the maximum number of
  connections.

## [0.10] - 2026-08-30

[0.10]: https://github.com/ccache/ccache-storage-http-cpp/releases/tag/v0.10

### Added

- Support for `SSL_CERT_FILE` and `SSL_CERT_DIR` environment variables for
  custom CA store.
- New `bearer-token-file` attribute for reading the bearer token from a file
  for each request, so that the token can be rotated without restarting the
  helper.

## [0.9] - 2026-07-18

[0.9]: https://github.com/ccache/ccache-storage-http-cpp/releases/tag/v0.9

### Fixed

- Fix libuv handle shutdown ordering.
- Fix PUT seek handling after redirect.
- Fix bazel key padding for short keys.
- Close IPC client connection on write failure.
- Only close client connection instead of whole server on unknown request.
- Reject pipelined IPC requests.

## [0.8] - 2026-05-18

[0.8]: https://github.com/ccache/ccache-storage-http-cpp/releases/tag/v0.8

### Added

- Support for info operation.
- Support for exists operation.

### Removed

- Support for experimental CRSH greeting message format 2.

## [0.7] - 2026-05-10

[0.7]: https://github.com/ccache/ccache-storage-http-cpp/releases/tag/v0.7

### Added

- Basic integration test suite.

### Changed

- Improve connection pool size.
- Avoid an extra copy of PUT payloads.
- Preallocate response buffer for GET requests.
- Increase uv_listen backlog to 4096.

## [0.6] - 2026-04-22

[0.6]: https://github.com/ccache/ccache-storage-http-cpp/releases/tag/v0.6

### Added

- Support for [netrc] authentication.
- Logging of attributes.
- Support for CRSH greeting message format 2.
- Sending of config errors/warnings to ccache.

[netrc]: https://everything.curl.dev/usingcurl/netrc.html

### Changed

- Improve logging of attribute parse errors.

### Fixed

- Installation of ccache-storage-https.exe on Windows.
- Use-after-free in IPC client connection lifetime management.

## [0.5] - 2026-03-18

[0.5]: https://github.com/ccache/ccache-storage-http-cpp/releases/tag/v0.5

### Added

- tar.gz release archive.

### Changed

- Change working directory to `/` (or `C:\` on Windows) to avoid blocking
  removal of the directory the server was started from.

## [0.4] - 2026-03-15

[0.4]: https://github.com/ccache/ccache-storage-http-cpp/releases/tag/v0.4

### Changed

- Set `User-Agent` header to `ccache-storage-http-cpp/$VERSION` in HTTP requests.

## [0.3] - 2026-03-07

[0.3]: https://github.com/ccache/ccache-storage-http-cpp/releases/tag/v0.3

### Added

- Source release archives.

### Changed

- Rename project to `ccache-storage-http-cpp` for clarity.
- Correct `<string_view>` includes.

## [0.2] - 2026-03-05

[0.2]: https://github.com/ccache/ccache-storage-http-cpp/releases/tag/v0.2

### Added

- Support for building with Meson.

### Changed

- Switch license to MIT.
- Generate version string in usage text from the project version.
- Rename CMake "format" target to "clang-format".

## [0.1] - 2026-01-18

[0.1]: https://github.com/ccache/ccache-storage-http-cpp/releases/tag/v0.1

### Added

- First version.
