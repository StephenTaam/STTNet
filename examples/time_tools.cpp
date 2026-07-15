#include <sttnet.h>

#include <chrono>
#include <iostream>
#include <thread>

int main()
{
    using stt::time::DateTime;
    using stt::time::Duration;

    // Read the current local time using STTNet's default ISO-like format.
    std::string now;
    DateTime::getTime(now, "yyyy-mm-ddThh:mi:ss.sss");
    std::cout << "now: " << now << '\n';

    // Convert the same time text to a display-oriented format.
    std::string display = now;
    if(DateTime::convertFormat(display,
                               "yyyy-mm-ddThh:mi:ss.sss",
                               "yyyy/mm/dd hh:mi:ss.sss"))
        std::cout << "display: " << display << '\n';

    // Duration represents an interval, not an absolute wall-clock time.
    Duration retryDelay(0, 0, 1, 30, 0);
    std::cout << "retry delay: " << retryDelay.convertToSec() << " seconds\n";

    // DateTime uses steady_clock for elapsed-time measurement, so wall-clock
    // changes do not distort the result.
    DateTime timer;
    timer.startTiming();
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    Duration elapsed = timer.endTiming();
    std::cout << "elapsed: " << elapsed.convertToMsec() << " ms\n";

    return 0;
}
