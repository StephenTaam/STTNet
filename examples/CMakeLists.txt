add_executable(sttnet_http_hello http_hello.cpp)
target_link_libraries(sttnet_http_hello PRIVATE STTNet::sttnet)

add_executable(sttnet_http_json http_json.cpp)
target_link_libraries(sttnet_http_json PRIVATE STTNet::sttnet)

add_executable(sttnet_worker_pool worker_pool.cpp)
target_link_libraries(sttnet_worker_pool PRIVATE STTNet::sttnet)

add_executable(sttnet_websocket_echo websocket_echo.cpp)
target_link_libraries(sttnet_websocket_echo PRIVATE STTNet::sttnet)

add_executable(sttnet_tcp_echo tcp_echo.cpp)
target_link_libraries(sttnet_tcp_echo PRIVATE STTNet::sttnet)

add_executable(sttnet_udp_echo udp_echo.cpp)
target_link_libraries(sttnet_udp_echo PRIVATE STTNet::sttnet)

add_executable(sttnet_http_client http_client.cpp)
target_link_libraries(sttnet_http_client PRIVATE STTNet::sttnet)

add_executable(sttnet_websocket_client websocket_client.cpp)
target_link_libraries(sttnet_websocket_client PRIVATE STTNet::sttnet)

add_executable(sttnet_tls_https tls_https.cpp)
target_link_libraries(sttnet_tls_https PRIVATE STTNet::sttnet)

add_executable(sttnet_signal_shutdown signal_shutdown.cpp)
target_link_libraries(sttnet_signal_shutdown PRIVATE STTNet::sttnet)

add_executable(sttnet_system_settings system_settings.cpp)
target_link_libraries(sttnet_system_settings PRIVATE STTNet::sttnet)
