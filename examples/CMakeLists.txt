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

add_executable(sttnet_json_tools json_tools.cpp)
target_link_libraries(sttnet_json_tools PRIVATE STTNet::sttnet)

add_executable(sttnet_time_tools time_tools.cpp)
target_link_libraries(sttnet_time_tools PRIVATE STTNet::sttnet)

add_executable(sttnet_file_logging file_logging.cpp)
target_link_libraries(sttnet_file_logging PRIVATE STTNet::sttnet)

add_executable(sttnet_crypto_encoding crypto_encoding.cpp)
target_link_libraries(sttnet_crypto_encoding PRIVATE STTNet::sttnet)

add_executable(sttnet_data_tools data_tools.cpp)
target_link_libraries(sttnet_data_tools PRIVATE STTNet::sttnet)

add_executable(sttnet_security_limiter security_limiter.cpp)
target_link_libraries(sttnet_security_limiter PRIVATE STTNet::sttnet)

add_executable(sttnet_process_supervisor process_supervisor.cpp)
target_link_libraries(sttnet_process_supervisor PRIVATE STTNet::sttnet)

add_executable(sttnet_protocol_parsing protocol_parsing.cpp)
target_link_libraries(sttnet_protocol_parsing PRIVATE STTNet::sttnet)
