cmake_minimum_required(VERSION 3.16)
project(STTNet VERSION 0.7.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(STTNET_TOP_LEVEL_DEFAULT OFF)
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
  set(STTNET_TOP_LEVEL_DEFAULT ON)
endif()

option(STTNET_BUILD_EXAMPLE "Build the example HTTP/WebSocket servers" ${STTNET_TOP_LEVEL_DEFAULT})
option(STTNET_BUILD_TESTS "Build regression tests" ${STTNET_TOP_LEVEL_DEFAULT})
option(STTNET_BUILD_BENCHMARK "Build the HTTP benchmark server" OFF)
option(STTNET_BUILD_DOCS "Add the Chinese and English Doxygen target" OFF)
option(STTNET_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(STTNET_ENABLE_INSTALL "Generate STTNet install and package export rules" ${STTNET_TOP_LEVEL_DEFAULT})
option(STTNET_INSTALL_DOCUMENTATION "Install README and Markdown documentation" ON)

find_package(OpenSSL 1.1.1 REQUIRED)
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(JSONCPP REQUIRED IMPORTED_TARGET jsoncpp)

add_library(sttnet src/sttnet.cpp)
add_library(STTNet::sttnet ALIAS sttnet)
target_compile_features(sttnet PUBLIC cxx_std_17)
target_include_directories(sttnet PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(sttnet PUBLIC OpenSSL::SSL OpenSSL::Crypto Threads::Threads PkgConfig::JSONCPP)
target_compile_options(sttnet PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(sttnet PROPERTIES
  EXPORT_NAME sttnet
  POSITION_INDEPENDENT_CODE ON
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR})

if(STTNET_ENABLE_SANITIZERS)
  target_compile_options(sttnet PUBLIC -fno-omit-frame-pointer -fsanitize=address,undefined)
  target_link_options(sttnet PUBLIC -fsanitize=address,undefined)
endif()

if(STTNET_BUILD_EXAMPLE)
  add_executable(sttnet_example main.cpp)
  target_link_libraries(sttnet_example PRIVATE STTNet::sttnet)
  add_subdirectory(examples)
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
  add_executable(sttnet_file_tests tests/file_tests.cpp)
  target_link_libraries(sttnet_file_tests PRIVATE sttnet)
  add_test(NAME sttnet_file_tests COMMAND sttnet_file_tests)
endif()

if(STTNET_ENABLE_INSTALL)
  set(STTNET_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/STTNet"
      CACHE STRING "Install path for STTNet CMake package files")

  configure_package_config_file(
    cmake/STTNetConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/STTNetConfig.cmake"
    INSTALL_DESTINATION "${STTNET_INSTALL_CMAKEDIR}")
  write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/STTNetConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
  configure_file(cmake/sttnet.pc.in "${CMAKE_CURRENT_BINARY_DIR}/sttnet.pc" @ONLY)

  install(TARGETS sttnet
    EXPORT STTNetTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
  install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h")
  install(EXPORT STTNetTargets
    FILE STTNetTargets.cmake
    NAMESPACE STTNet::
    DESTINATION "${STTNET_INSTALL_CMAKEDIR}")
  install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/STTNetConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/STTNetConfigVersion.cmake"
    DESTINATION "${STTNET_INSTALL_CMAKEDIR}")
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/sttnet.pc"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")
  install(FILES LICENSE README.md README_Chinese.md README_English.md
    DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/doc/STTNet")
  if(STTNET_INSTALL_DOCUMENTATION)
    file(GLOB STTNET_MARKDOWN_DOCUMENTATION CONFIGURE_DEPENDS
      "${CMAKE_CURRENT_SOURCE_DIR}/docs/*.md")
    install(FILES ${STTNET_MARKDOWN_DOCUMENTATION}
      DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/doc/STTNet")
  endif()
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
