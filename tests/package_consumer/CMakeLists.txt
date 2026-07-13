cmake_minimum_required(VERSION 3.16)
project(STTNetPackageConsumer LANGUAGES CXX)

find_package(STTNet 0.7 CONFIG REQUIRED)

add_executable(sttnet_package_consumer main.cpp)
target_link_libraries(sttnet_package_consumer PRIVATE STTNet::sttnet)
