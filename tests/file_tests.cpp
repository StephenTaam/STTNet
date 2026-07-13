#include "sttnet.h"

#include <atomic>
#include "test_assert.h"
#include <chrono>
#include <limits>
#include <string>
#include <thread>
#include <unistd.h>

int main()
{
    const std::string path="/tmp/sttnet-file-boundary-"+std::to_string(getpid());
    (void)::unlink(path.c_str());
    stt::file::File file;
    STTNET_CHECK(file.openFile(path));
    STTNET_CHECK(file.lockMemory());

    std::string line;
    std::string lines;
    STTNET_CHECK(!file.readLineC(line,0));
    STTNET_CHECK(!file.readLineC(line,-1));
    STTNET_CHECK(file.readC(lines,0,1).empty());
    STTNET_CHECK(file.readC(lines,1,0).empty());
    STTNET_CHECK(file.findC("missing",0)==-1);
    STTNET_CHECK(!file.deleteLineC());
    STTNET_CHECK(!file.chgLineC("replacement"));

    STTNET_CHECK(file.appendLineC("first"));
    STTNET_CHECK(file.readLineC(line,1));
    STTNET_CHECK(line=="first");
    STTNET_CHECK(file.readC(lines,1,1)=="first");
    STTNET_CHECK(file.findC("irs")==1);
    STTNET_CHECK(!file.deleteLineC(-1));
    STTNET_CHECK(!file.chgLineC("replacement",-1));
    STTNET_CHECK(file.chgLineC("replacement"));
    STTNET_CHECK(file.deleteLineC());
    STTNET_CHECK(!file.deleteLineC());

    STTNET_CHECK(file.unlockMemory());
    STTNET_CHECK(file.closeFile(true));

    const std::string binaryPath=path+"-binary";
    (void)::unlink(binaryPath.c_str());
    stt::file::File binaryFile;
    STTNET_CHECK(binaryFile.openFile(binaryPath,true,1,16));
    STTNET_CHECK(binaryFile.lockMemory());
    const char payload[]={'a','\0','b'};
    STTNET_CHECK(binaryFile.writeC(payload,0,sizeof(payload)));
    STTNET_CHECK(!binaryFile.writeC(payload,std::numeric_limits<size_t>::max()-1,sizeof(payload)));
    char received[sizeof(payload)]{};
    STTNET_CHECK(binaryFile.readC(received,0,sizeof(received)));
    STTNET_CHECK(std::string(received,sizeof(received))==std::string(payload,sizeof(payload)));
    STTNET_CHECK(!binaryFile.readC(received,std::numeric_limits<size_t>::max(),1));
    STTNET_CHECK(binaryFile.unlockMemory());
    STTNET_CHECK(binaryFile.closeFile(true));
    STTNET_CHECK(binaryFile.closeFile(true));

    const std::string concurrentPath=path+"-concurrent";
    (void)::unlink(concurrentPath.c_str());
    stt::file::File concurrentFile;
    STTNET_CHECK(concurrentFile.openFile(concurrentPath));
    std::atomic<bool> transactionStarted{false};
    std::atomic<bool> releaseTransaction{false};
    std::atomic<bool> closeFinished{false};
    std::thread transaction([&] {
        STTNET_CHECK(concurrentFile.lockMemory());
        transactionStarted.store(true,std::memory_order_release);
        while(!releaseTransaction.load(std::memory_order_acquire))
            std::this_thread::yield();
        STTNET_CHECK(concurrentFile.unlockMemory(true));
    });
    while(!transactionStarted.load(std::memory_order_acquire))
        std::this_thread::yield();
    STTNET_CHECK(!concurrentFile.unlockMemory(true));
    std::thread closer([&] {
        STTNET_CHECK(concurrentFile.closeFile(true));
        closeFinished.store(true,std::memory_order_release);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    STTNET_CHECK(!closeFinished.load(std::memory_order_acquire));
    releaseTransaction.store(true,std::memory_order_release);
    transaction.join();
    closer.join();
    STTNET_CHECK(closeFinished.load(std::memory_order_acquire));
    return 0;
}
