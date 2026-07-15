#include <sttnet.h>

#include <chrono>
#include <iostream>
#include <thread>

int main()
{
    using stt::time::DateTime;
    using stt::time::Duration;

    // DateTime::getTime() writes the current local wall-clock time into a string.
    // It does not return a Unix timestamp or a Duration.
    std::string now;
    DateTime::getTime(now, ISO8086B);
    std::cout << "now: " << now << '\n';

    // convertFormat() rearranges text that matches the declared old format.
    // It modifies the input string and returns false when parsing fails.
    std::string display=now;
    if(DateTime::convertFormat(display,ISO8086B,"yyyy/mm/dd hh:mi:ss.sss"))
        std::cout << "display: " << display << '\n';

    // Duration is STTNet's interval type. Constructor order:
    // day, hour, minute, second, millisecond.
    const Duration retryDelay(0,0,1,30,0);
    std::cout << "retry delay: " << retryDelay.convertToMsec() << " ms\n";

    // calculateTime(time1,time2,...) also returns its result through Duration.
    Duration difference;
    DateTime::calculateTime("2026-07-15T12:01:30.250",
                            "2026-07-15T12:00:00.000",
                            difference,ISO8086B,ISO8086B);
    if(difference.isValid())
        std::cout << "difference: " << difference.convertToMsec() << " ms\n";

    // startTiming(), checkTime(), and endTiming() use steady_clock. Both read
    // methods return stt::time::Duration; they do not return an integer.
    DateTime timer;
    if(!timer.startTiming())
        return 1;
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    const Duration current=timer.checkTime(); // Read without stopping.
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    const Duration total=timer.endTiming();   // Stop and save the last interval.

    if(!current.isValid()||!total.isValid())
        return 2;
    std::cout << "current: " << current.convertToMsec() << " ms\n";
    std::cout << "total: " << total.convertToMsec() << " ms\n";
    std::cout << "saved: " << timer.getDt().convertToMsec() << " ms\n";

    // recoverForm() modifies the current Duration object.
    Duration normalized;
    normalized.recoverForm(90061001);
    std::cout << "normalized: " << normalized.day << "d "
              << normalized.hour << "h " << normalized.min << "m "
              << normalized.sec << "s " << normalized.msec << "ms\n";
    return 0;
}
