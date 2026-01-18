cmake_minimum_required(VERSION 3.16)
project(ccache-storage-http VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(CURL QUIET CONFIG)
if(NOT CURL_FOUND)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(CURL REQUIRED IMPORTED_TARGET libcurl)
endif()

find_package(libuv QUIET CONFIG)
if(NOT libuv_FOUND)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(LIBUV REQUIRED IMPORTED_TARGET libuv)
endif()

set(
  SOURCES
  src/config.cpp
  src/ipc_server.cpp
  src/logger.cpp
  src/main.cpp
  src/storage_client.cpp
)

add_executable(ccache-storage-http ${SOURCES})

if(TARGET CURL::libcurl)
  target_link_libraries(ccache-storage-http PRIVATE CURL::libcurl)
else()
  target_link_libraries(ccache-storage-http PRIVATE PkgConfig::CURL)
endif()

if(TARGET libuv::uv_a)
  target_link_libraries(ccache-storage-http PRIVATE libuv::uv_a)
elseif(TARGET libuv::libuv)
  target_link_libraries(ccache-storage-http PRIVATE libuv::libuv)
else()
  target_link_libraries(ccache-storage-http PRIVATE PkgConfig::LIBUV)
endif()

if(WIN32)
  target_compile_definitions(ccache-storage-http PRIVATE _WIN32_WINNT=0x0601 WIN32_LEAN_AND_MEAN NOGDI NOMINMAX)
  if(MSVC)
    target_compile_options(ccache-storage-http PRIVATE /W2)
  else()
    target_compile_options(ccache-storage-http PRIVATE -Wall -Wextra)
  endif()
else()
  target_compile_options(ccache-storage-http PRIVATE -Wall -Wextra)
endif()

install(TARGETS ccache-storage-http RUNTIME DESTINATION bin)
install(CODE "execute_process(COMMAND \${CMAKE_COMMAND} -E create_symlink ccache-storage-http \$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/bin/ccache-storage-https)")

find_program(CLANG_FORMAT clang-format)
if(CLANG_FORMAT)
  file(GLOB_RECURSE ALL_SOURCE_FILES src/*.cpp src/*.hpp)
  add_custom_target(format COMMAND ${CLANG_FORMAT} -i ${ALL_SOURCE_FILES} COMMENT "Running clang-format on all source files" VERBATIM)
endif()
