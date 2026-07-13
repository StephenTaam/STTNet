cmake_minimum_required(VERSION 3.16)
project(STTNetSubdirectoryConsumer LANGUAGES CXX)

add_subdirectory(../.. sttnet-source)

if(STTNET_BUILD_TESTS OR STTNET_BUILD_EXAMPLE OR STTNET_ENABLE_INSTALL)
  message(FATAL_ERROR "STTNet tests/examples/install rules must default to OFF as a subproject")
endif()

add_executable(sttnet_subdirectory_consumer ../package_consumer/main.cpp)
target_link_libraries(sttnet_subdirectory_consumer PRIVATE STTNet::sttnet)
