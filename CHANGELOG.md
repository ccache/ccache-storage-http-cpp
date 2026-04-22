# Changelog

## [Unreleased]

### Added

- Support for [netrc](https://everything.curl.dev/usingcurl/netrc.html)
  authentication.
- Logging of attributes.

### Fixed

- Installation of ccache-storage-https.exe on Windows.
- Use-after-free in IPC client connection lifetime management.

## [0.5] - 2026-03-18

### Added

- tar.gz release archive.

### Changed

- Change working directory to `/` (or `C:\` on Windows) to avoid blocking
  removal of the directory the server was started from.

## [0.4] - 2026-03-15

### Changed

- Set `User-Agent` header to `ccache-storage-http-cpp/$VERSION` in HTTP requests.

## [0.3] - 2026-03-07

### Added

- Source release archives.

### Changed

- Rename project to `ccache-storage-http-cpp` for clarity.
- Correct `<string_view>` includes.

## [0.2] - 2026-03-05

### Added

- Support for building with Meson.

### Changed

- Switch license to MIT.
- Generate version string in usage text from the project version.
- Rename CMake "format" target to "clang-format".

## [0.1] - 2026-01-18

### Added

- First version.
