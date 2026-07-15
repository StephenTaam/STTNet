#include <sttnet.h>

#include <chrono>
#include <iostream>
#include <thread>

int main()
{
    using stt::file::File;
    using stt::file::FileTool;
    using stt::file::LogFile;

    // FileTool handles simple path-level operations.
    if(!FileTool::createDir("runtime"))
        std::cerr << "runtime directory may already exist or could not be created\n";

    // File offers thread-safe text and binary operations. openFile() can create
    // missing parent directories when create=true.
    File config;
    if(!config.openFile("runtime/example.conf"))
        return 1;

    config.deleteAll();
    config.appendLine("port=8080");
    config.appendLine("workers=4");

    std::string all;
    config.readAll(all);
    std::cout << "configuration:\n" << all << '\n';
    config.closeFile();

    // LogFile uses a bounded asynchronous queue. Producers never wait for disk;
    // when the queue is full, new log records are dropped and counted.
    LogFile log(8192);
    if(!log.openFile("runtime/example.log"))
        return 2;

    log.writeLog("service started");
    log.writeLog("configuration loaded");

    // Give the consumer thread a moment in this tiny demo. In a real service,
    // keep the LogFile alive until all producers have stopped.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    std::cout << "dropped logs: " << log.getDroppedLogCount() << '\n';
    log.closeFile();

    return 0;
}
