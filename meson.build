project(
  'ccache-storage-http-cpp',
  'cpp',
  version: files('version.txt'),
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

ccache_storage_http = executable(
  'ccache-storage-http',
  sources,
  dependencies: [dependency('libcurl'), dependency('libuv')],
  install: true,
)

if host_machine.system() == 'windows'
  fs = import('fs')
  fs.copyfile(
    ccache_storage_http.full_path(),
    'ccache-storage-https.exe',
    install: true,
    install_dir: get_option('bindir'),
  )
else
  install_symlink(
    'ccache-storage-https',
    pointing_to: 'ccache-storage-http',
    install_dir: get_option('bindir'),
  )
endif

if host_machine.system() != 'windows'
  test(
    'integration',
    find_program('tests/integration/test_storage_helper'),
    args: ['--binary', ccache_storage_http.full_path()],
  )
endif
