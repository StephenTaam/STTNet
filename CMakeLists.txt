cmake_minimum_required(VERSION 3.16)
project(STTNet VERSION 0.6.0 LANGUAGES CXX)

option(STTNET_BUILD_EXAMPLE "Build the example HTTP/WebSocket server" ON)
option(STTNET_BUILD_TESTS "Build concurrency regression tests" ON)
option(STTNET_BUILD_BENCHMARK "Build the HTTP benchmark server" OFF)
option(STTNET_BUILD_DOCS "Add the Chinese and English Doxygen target" OFF)
option(STTNET_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

find_package(OpenSSL 1.1.1 REQUIRED)
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(JSONCPP REQUIRED IMPORTED_TARGET jsoncpp)

add_library(sttnet STATIC src/sttnet.cpp)
target_compile_features(sttnet PUBLIC cxx_std_17)
target_include_directories(sttnet PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(sttnet PUBLIC OpenSSL::SSL OpenSSL::Crypto Threads::Threads PkgConfig::JSONCPP)
target_compile_options(sttnet PRIVATE -Wall -Wextra -Wpedantic)

if(STTNET_ENABLE_SANITIZERS)
  target_compile_options(sttnet PUBLIC -fno-omit-frame-pointer -fsanitize=address,undefined)
  target_link_options(sttnet PUBLIC -fsanitize=address,undefined)
endif()

if(STTNET_BUILD_EXAMPLE)
  add_executable(sttnet_example main.cpp)
  target_link_libraries(sttnet_example PRIVATE sttnet)
endif()

if(STTNET_BUILD_TESTS)
  enable_testing()
  add_executable(sttnet_concurrency_tests tests/concurrency_tests.cpp)
  target_link_libraries(sttnet_concurrency_tests PRIVATE sttnet)
  add_test(NAME sttnet_concurrency_tests COMMAND sttnet_concurrency_tests)
  add_executable(sttnet_signal_tests tests/signal_tests.cpp)
  target_link_libraries(sttnet_signal_tests PRIVATE sttnet)
  add_test(NAME sttnet_signal_tests COMMAND sttnet_signal_tests)
  add_executable(sttnet_http_parser_tests tests/http_parser_tests.cpp)
  target_link_libraries(sttnet_http_parser_tests PRIVATE sttnet)
  add_test(NAME sttnet_http_parser_tests COMMAND sttnet_http_parser_tests)
  add_executable(sttnet_websocket_tests tests/websocket_tests.cpp)
  target_link_libraries(sttnet_websocket_tests PRIVATE sttnet)
  add_test(NAME sttnet_websocket_tests COMMAND sttnet_websocket_tests)
endif()

if(STTNET_BUILD_BENCHMARK)
  add_executable(sttnet_http_benchmark benchmarks/http_server.cpp)
  target_link_libraries(sttnet_http_benchmark PRIVATE sttnet)
endif()

if(STTNET_BUILD_DOCS)
  find_package(Doxygen REQUIRED)
  add_custom_target(sttnet_docs
    COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile_Chinese
    COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile_English
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Generating STTNet Doxygen documentation"
    VERBATIM)
endif()
