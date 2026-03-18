project(
  'ccache-storage-http-cpp',
  'cpp',
  version: '0.5',
  default_options: [
    'cpp_std=c++17',
    'b_ndebug=if-release',
  ],
)

configure_file(
  input: 'src/version.hpp.in',
  output: 'version.hpp',
  configuration: {'PROJECT_VERSION': meson.project_version()},
)

sources = files(
  'src/config.cpp',
  'src/ipc_server.cpp',
  'src/logger.cpp',
  'src/main.cpp',
  'src/storage_client.cpp',
)

executable(
  'ccache-storage-http',
  sources,
  dependencies: [dependency('libcurl'), dependency('libuv')],
  install: true,
)

install_symlink(
  'ccache-storage-https',
  pointing_to: 'ccache-storage-http',
  install_dir: get_option('bindir')
)
