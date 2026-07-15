#include <sttnet.h>

#include <iostream>

int main()
{
    using stt::file::File;
    using stt::file::FileTool;
    using stt::file::LogFile;

    // createDir() returns false when creation fails; an existing directory may
    // also make it return false, so inspect the path when this matters.
    (void)FileTool::createDir("runtime");

    // File provides thread-safe text and binary operations. openFile() can create
    // missing parent directories when creation is enabled by the API.
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

    // FileTool::copy() now copies raw bytes. It returns false when either file
    // cannot be opened or when the write does not complete successfully.
    if(!FileTool::copy("runtime/example.conf","runtime/example.conf.copy"))
        return 2;

    // LogFile owns a bounded asynchronous queue. Keep the object alive until all
    // producer threads have stopped. Its destructor drains queued records before
    // closing; do not use an arbitrary sleep as a substitute for lifecycle order.
    {
        LogFile log(8192);
        if(!log.openFile("runtime/example.log"))
            return 3;
        log.writeLog("service started");
        log.writeLog("configuration loaded");
        std::cout << "dropped logs: " << log.getDroppedLogCount() << '\n';
    }
    return 0;
}
