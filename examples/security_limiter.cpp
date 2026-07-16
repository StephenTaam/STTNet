#include <sttnet.h>

#include <iostream>

static const char *decisionName(stt::security::DefenseDecision decision)
{
    using namespace stt::security;
    switch(decision)
    {
        case ALLOW: return "ALLOW";
        case DROP:  return "DROP";
        case CLOSE: return "CLOSE";
    }
    return "UNKNOWN";
}

int main()
{
    using namespace stt::security;

    // Allow at most two simultaneous connections from one IP and consider a
    // connection idle after 60 seconds.
    ConnectionLimiter limiter(2, 60);
    limiter.setConnectStrategy(RateLimitType::TokenBucket);
    limiter.setRequestStrategy(RateLimitType::SlidingWindow);
    limiter.setPathStrategy(RateLimitType::Cooldown);

    // /login receives a stricter extra rule: two requests per ten seconds.
    limiter.setPathLimit("/login", 2, 10);

    const std::string ip = "192.0.2.10";
    const int fd = 1001; // Demonstration identifier; no real socket is required.

    const DefenseDecision connect = limiter.allowConnect(ip, fd, 10, 1);
    std::cout << "connect: " << decisionName(connect) << '\n';
    if(connect != ALLOW)
        return 1;

    for(int i = 1; i <= 4; ++i)
    {
        const DefenseDecision request =
            limiter.allowRequest(ip, fd, "/login", 100, 1);
        std::cout << "login request " << i << ": "
                  << decisionName(request) << '\n';
    }

    limiter.banIP(ip, 30, "演示封禁", "demo ban");
    std::cout << "banned: " << std::boolalpha << limiter.isBanned(ip) << '\n';
    limiter.unbanIP(ip);

    // Each successfully registered fd is removed when the connection closes.
    limiter.clearIP(ip, fd);
    return 0;
}
