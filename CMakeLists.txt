file(READ "${CMAKE_SOURCE_DIR}/version.txt" PROJECT_VERSION_RAW)
string(STRIP "${PROJECT_VERSION_RAW}" PROJECT_VERSION)

cmake_minimum_required(VERSION 3.16)
project(ccache-storage-http-cpp VERSION "${PROJECT_VERSION}" LANGUAGES CXX)

enable_testing()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CCACHE_STORAGE_HTTPS_EXE "ccache-storage-https${CMAKE_EXECUTABLE_SUFFIX}")

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

configure_file(src/version.hpp.in version.hpp)

set(
  SOURCES
  src/config.cpp
  src/ipc_server.cpp
  src/logger.cpp
  src/main.cpp
  src/storage_client.cpp
)

add_executable(ccache-storage-http ${SOURCES})
target_include_directories(ccache-storage-http PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

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
if(WIN32)
  install(PROGRAMS $<TARGET_FILE:ccache-storage-http> DESTINATION bin RENAME ${CCACHE_STORAGE_HTTPS_EXE})
else()
  install(CODE "execute_process(COMMAND \${CMAKE_COMMAND} -E create_symlink ccache-storage-http \$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/bin/${CCACHE_STORAGE_HTTPS_EXE})")
endif()

find_program(CLANG_FORMAT clang-format)
if(CLANG_FORMAT)
  file(GLOB_RECURSE ALL_SOURCE_FILES src/*.cpp src/*.hpp)
  add_custom_target(clang-format COMMAND ${CLANG_FORMAT} -i ${ALL_SOURCE_FILES} COMMENT "Running clang-format on all source files" VERBATIM)
endif()

if(NOT WIN32)
  add_test(
    NAME integration
    COMMAND
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_storage_helper"
      --binary
      "$<TARGET_FILE:ccache-storage-http>"
  )
endif()
