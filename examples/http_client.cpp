#include <sttnet.h>

#include <iostream>

int main()
{
    using stt::network::HttpClient;

    // HttpClient is a synchronous client. Use a finite timeout in production
    // and run it outside an STTNet Reactor callback.
    HttpClient client;

    if(!client.getRequest("http://127.0.0.1:8080/ping", "",
                          "Connection: close", 5))
    {
        std::cerr << "request could not be sent\n";
        return 1;
    }

    // Sending successfully is not the same as receiving a complete response.
    if(!client.isReturn())
    {
        std::cerr << "server did not return a complete HTTP response\n";
        return 2;
    }

    std::cout << "--- response headers ---\n" << client.header
              << "\n--- response body ---\n" << client.body << '\n';
    return 0;
}
