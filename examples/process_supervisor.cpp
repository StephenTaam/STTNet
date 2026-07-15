#include <sttnet.h>

#include <chrono>
#include <iostream>
#include <thread>
#include <unistd.h>

int main()
{
    using stt::system::Process;

    // sec=2 creates a helper process that launches the worker again two seconds
    // after each exit. The helper is tied to this parent on Linux, so it stops
    // when the parent program ends.
    const bool started = Process::startProcess(
        [] {
            const char message[] = "supervised child ran once\n";
            // write() is used because the child exits with _exit(), which does not
            // flush C++ iostream buffers.
            ::write(STDOUT_FILENO, message, sizeof(message) - 1);
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
        },
        2);

    if(!started)
    {
        std::cerr << "failed to create supervisor\n";
        return 1;
    }

    std::cout << "parent pid=" << ::getpid()
              << "; observe several child runs for seven seconds" << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(7));

    // This is supervision, not full daemonization: Process::startProcess() does
    // not call setsid(), change the working directory, or redirect standard I/O.
    return 0;
}
