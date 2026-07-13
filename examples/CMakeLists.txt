add_executable(sttnet_http_hello http_hello.cpp)
target_link_libraries(sttnet_http_hello PRIVATE STTNet::sttnet)

add_executable(sttnet_websocket_echo websocket_echo.cpp)
target_link_libraries(sttnet_websocket_echo PRIVATE STTNet::sttnet)
