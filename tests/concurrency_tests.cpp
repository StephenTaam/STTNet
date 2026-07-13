#include "sttnet.h"

#include <atomic>
#include <cassert>
#include <cstdint>
#include <fcntl.h>
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

class TestServer final : public stt::network::TcpServer {
public:
    using TcpServer::enqueueWrite;
    using TcpServer::flushConnectionWrites;
    using TcpServer::requestQueuedClose;
    using TcpServer::WriteFlushResult;
private:
    void handleHeartbeat() override {}
};

struct NonDefaultConstructible {
    explicit NonDefaultConstructible(int value) : value(value) {}
    NonDefaultConstructible(const NonDefaultConstructible&) = delete;
    NonDefaultConstructible& operator=(const NonDefaultConstructible&) = delete;
    NonDefaultConstructible(NonDefaultConstructible&&) noexcept = default;
    NonDefaultConstructible& operator=(NonDefaultConstructible&&) noexcept = default;
    int value;
};

void testQueueSupportsNonDefaultConstructibleValues()
{
    stt::system::MPSCQueue<NonDefaultConstructible> queue(8);
    assert(queue.push(NonDefaultConstructible(42)));
}

void testMPSCQueueUnderContention()
{
    constexpr std::size_t producerCount=4;
    constexpr std::size_t valuesPerProducer=25000;
    constexpr std::size_t totalValues=producerCount*valuesPerProducer;

    stt::system::MPSCQueue<std::uint64_t> queue(1024);
    std::vector<unsigned char> seen(totalValues,0);
    std::atomic<std::size_t> ready{0};
    std::atomic<bool> start{false};
    std::vector<std::thread> producers;

    for(std::size_t producer=0;producer<producerCount;++producer)
    {
        producers.emplace_back([&,producer] {
            ready.fetch_add(1,std::memory_order_release);
            while(!start.load(std::memory_order_acquire))
                std::this_thread::yield();
            for(std::size_t index=0;index<valuesPerProducer;++index)
            {
                const std::uint64_t value=producer*valuesPerProducer+index;
                while(!queue.push(value))
                    std::this_thread::yield();
            }
        });
    }

    while(ready.load(std::memory_order_acquire)!=producerCount)
        std::this_thread::yield();
    start.store(true,std::memory_order_release);

    std::size_t consumed=0;
    std::uint64_t value=0;
    while(consumed<totalValues)
    {
        if(!queue.pop(value))
        {
            std::this_thread::yield();
            continue;
        }
        assert(value<totalValues);
        assert(seen[value]==0);
        seen[value]=1;
        ++consumed;
    }

    for(auto &producer:producers)
        producer.join();
    assert(queue.approx_size()==0);
}

void testWorkerPoolDrainsAndRejectsAfterStop()
{
    stt::system::WorkerPool pool(4);
    std::atomic<int> completed{0};
    for(int task=0;task<1000;++task)
        assert(pool.submit([&completed] { completed.fetch_add(1,std::memory_order_relaxed); }));
    pool.stop();
    assert(completed.load(std::memory_order_relaxed)==1000);
    assert(!pool.submit([] {}));
}

void testServerHandlerDelegatesTransportOperations()
{
    stt::network::TcpFDHandler handler;
    handler.setFD(42,nullptr,true);
    std::vector<std::string> writes;
    bool closeRequested=false;
    handler.setTransportFunctions(
        [&writes](std::string data) {
            const int size=static_cast<int>(data.size());
            writes.push_back(std::move(data));
            return size;
        },
        [&closeRequested] { closeRequested=true; }
    );

    assert(handler.sendData(std::string("hello"))==5);
    const char binary[]={'\0','x','\0'};
    assert(handler.sendData(binary,sizeof(binary),false)==3);
    assert(writes.size()==2);
    assert(writes[0]=="hello");
    assert(writes[1].size()==3);
    assert(writes[1][0]=='\0'&&writes[1][1]=='x'&&writes[1][2]=='\0');
    handler.close();
    assert(closeRequested);
    assert(handler.getFD()==-1);
}

void testPerConnectionWriteHighWaterMark()
{
    TestServer server;
    auto state=std::make_shared<stt::network::ConnectionWriteState>();
    state->fd=9;
    state->connection_obj_fd=77;
    state->max_queued_bytes=8;

    assert(server.enqueueWrite(state,"1234")==4);
    assert(server.enqueueWrite(state,"5678")==4);
    assert(server.enqueueWrite(state,"x")==-101);
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        assert(state->queued_bytes==8);
        assert(state->queue.size()==2);
        assert(state->overflowed);
        assert(state->close_requested);
    }
    const auto metrics=server.getMetrics();
    assert(metrics.queued_write_bytes==8);
    assert(metrics.pending_write_bytes==8);
    assert(metrics.write_overflows==1);
}

void testConcurrentPerConnectionEnqueue()
{
    TestServer server;
    auto state=std::make_shared<stt::network::ConnectionWriteState>();
    state->fd=10;
    state->connection_obj_fd=78;
    state->max_queued_bytes=8UL*1024UL*1024UL;
    constexpr int producerCount=8;
    constexpr int writesPerProducer=2000;
    std::vector<std::thread> producers;
    for(int producer=0;producer<producerCount;++producer)
    {
        producers.emplace_back([&,producer] {
            for(int write=0;write<writesPerProducer;++write)
                assert(server.enqueueWrite(state,std::to_string(producer)+":"+std::to_string(write)+"\n")>0);
        });
    }
    for(auto &producer:producers)
        producer.join();

    std::lock_guard<std::mutex> lock(state->mutex);
    assert(state->queue.size()==producerCount*writesPerProducer);
    std::size_t actualBytes=0;
    for(const auto &entry:state->queue)
        actualBytes+=entry.size();
    assert(actualBytes==state->queued_bytes);
    assert(!state->overflowed);
}

void testWriteFlushPreservesOrderAndBackpressure()
{
    int sockets[2]={-1,-1};
    assert(::socketpair(AF_UNIX,SOCK_STREAM,0,sockets)==0);
    const int flags=fcntl(sockets[0],F_GETFL,0);
    assert(flags>=0&&fcntl(sockets[0],F_SETFL,flags|O_NONBLOCK)==0);

    TestServer server;
    auto state=std::make_shared<stt::network::ConnectionWriteState>();
    state->fd=sockets[0];
    state->connection_obj_fd=79;
    state->max_queued_bytes=2UL*1024UL*1024UL;
    stt::network::TcpFDInf connection;
    connection.fd=sockets[0];
    connection.write_state=state;

    assert(server.enqueueWrite(state,"alpha")==5);
    assert(server.enqueueWrite(state,"beta")==4);
    assert(server.flushConnectionWrites(connection)==TestServer::WriteFlushResult::Drained);
    char ordered[9];
    assert(::recv(sockets[1],ordered,sizeof(ordered),MSG_WAITALL)==9);
    assert(std::string(ordered,sizeof(ordered))=="alphabeta");

    int sendBuffer=1024;
    assert(setsockopt(sockets[0],SOL_SOCKET,SO_SNDBUF,&sendBuffer,sizeof(sendBuffer))==0);
    const std::string largePayload(1024UL*1024UL,'x');
    assert(server.enqueueWrite(state,largePayload)==static_cast<int>(largePayload.size()));
    const auto result=server.flushConnectionWrites(connection);
    assert(result==TestServer::WriteFlushResult::WaitWrite||
           result==TestServer::WriteFlushResult::Reschedule);
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        assert(state->queued_bytes>0);
        assert(state->queued_bytes<largePayload.size());
    }
    ::close(sockets[0]);
    ::close(sockets[1]);
}

void testSocketBlockingModeTransitions()
{
    int sockets[2]={-1,-1};
    assert(::socketpair(AF_UNIX,SOCK_STREAM,0,sockets)==0);
    stt::network::TcpFDHandler handler;
    handler.setFD(sockets[0],nullptr,false);
    handler.unblockSet();
    assert((fcntl(sockets[0],F_GETFL,0)&O_NONBLOCK)!=0);
    handler.blockSet();
    assert((fcntl(sockets[0],F_GETFL,0)&O_NONBLOCK)==0);
    handler.close(false);
    ::close(sockets[0]);
    ::close(sockets[1]);
}

void testTcpClientBlockingDnsAndConnectionMetadata()
{
    const int listener=::socket(AF_INET,SOCK_STREAM,0);
    assert(listener>=0);
    int reuse=1;
    assert(setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse))==0);
    sockaddr_in address{};
    address.sin_family=AF_INET;
    address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    address.sin_port=0;
    if(::bind(listener,reinterpret_cast<sockaddr*>(&address),sizeof(address))!=0)
    {
        // Some sandboxed macOS runners deny bind(2), even for loopback port 0.
        // Linux CI still exercises the complete connect/metadata path.
        if(errno==EPERM)
        {
            ::close(listener);
            return;
        }
        assert(false);
    }
    socklen_t addressSize=sizeof(address);
    assert(getsockname(listener,reinterpret_cast<sockaddr*>(&address),&addressSize)==0);
    assert(::listen(listener,1)==0);
    std::thread peer([listener] {
        const int connection=::accept(listener,nullptr,nullptr);
        assert(connection>=0);
        char data[4]{};
        assert(::recv(connection,data,sizeof(data),MSG_WAITALL)==4);
        assert(std::string(data,sizeof(data))=="ping");
        ::close(connection);
    });

    stt::network::TcpClient client;
    const int port=ntohs(address.sin_port);
    assert(client.connect("localhost",port));
    assert((fcntl(client.getFD(),F_GETFL,0)&O_NONBLOCK)==0);
    assert(client.getServerIP()=="localhost");
    assert(client.getServerPort()==port);
    assert(client.sendData("ping")==4);
    peer.join();
    ::close(listener);
}

#ifdef __linux__
void testEpollSingleOwnsAndJoinsListenerThread()
{
    const int watched=eventfd(0,EFD_NONBLOCK|EFD_CLOEXEC);
    assert(watched>=0);
    stt::network::EpollSingle listener;
    std::atomic<int> callbacks{0};
    listener.setFunction([&callbacks](const int &fd) {
        uint64_t value=0;
        (void)::read(fd,&value,sizeof(value));
        callbacks.fetch_add(1,std::memory_order_relaxed);
        return false;
    });
    listener.startListen(watched);
    const uint64_t one=1;
    assert(::write(watched,&one,sizeof(one))==static_cast<ssize_t>(sizeof(one)));
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while(listener.isListen()&&std::chrono::steady_clock::now()<deadline)
        std::this_thread::yield();
    assert(callbacks.load(std::memory_order_relaxed)==1);
    assert(listener.endListen());

    listener.startListen(watched);
    assert(listener.endListen());
    ::close(watched);
}
#endif

} // namespace

int main()
{
    testQueueSupportsNonDefaultConstructibleValues();
    testMPSCQueueUnderContention();
    testWorkerPoolDrainsAndRejectsAfterStop();
    testServerHandlerDelegatesTransportOperations();
    testPerConnectionWriteHighWaterMark();
    testConcurrentPerConnectionEnqueue();
    testWriteFlushPreservesOrderAndBackpressure();
    testSocketBlockingModeTransitions();
    testTcpClientBlockingDnsAndConnectionMetadata();
#ifdef __linux__
    testEpollSingleOwnsAndJoinsListenerThread();
#endif
    std::cout<<"all concurrency tests passed\n";
    return 0;
}
