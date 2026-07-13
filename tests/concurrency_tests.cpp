#include "sttnet.h"

#include <atomic>
#include "test_assert.h"
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
    using TcpServer::TcpServer;
    using TcpServer::enqueueWrite;
    using TcpServer::flushConnectionWrites;
    using TcpServer::notifyReactor;
    using TcpServer::requestQueuedClose;
    using TcpServer::WriteFlushResult;
    void installWorkerEventFD(const int value)
    {
        workerEventFD.store(value,std::memory_order_release);
        workerWakePending.store(false,std::memory_order_release);
    }
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

struct ThrowingCopyValue {
    explicit ThrowingCopyValue(int value=0) : value(value) {}
    ThrowingCopyValue(const ThrowingCopyValue &other) : value(other.value)
    {
        if(throwOnCopy)
            throw std::runtime_error("copy failed");
    }
    ThrowingCopyValue(ThrowingCopyValue&&) noexcept = default;
    ThrowingCopyValue& operator=(ThrowingCopyValue&&) noexcept = default;
    int value;
    static bool throwOnCopy;
};

bool ThrowingCopyValue::throwOnCopy=false;

void testQueueSupportsNonDefaultConstructibleValues()
{
    stt::system::MPSCQueue<NonDefaultConstructible> queue(8);
    STTNET_CHECK(queue.push(NonDefaultConstructible(42)));
}

void testQueueCopyFailureDoesNotPoisonRing()
{
    stt::system::MPSCQueue<ThrowingCopyValue> queue(2);
    const ThrowingCopyValue source(7);
    ThrowingCopyValue::throwOnCopy=true;
    bool threw=false;
    try { (void)queue.push(source); }
    catch(const std::runtime_error &) { threw=true; }
    ThrowingCopyValue::throwOnCopy=false;
    STTNET_CHECK(threw);
    STTNET_CHECK(queue.push(ThrowingCopyValue(9)));
    ThrowingCopyValue output;
    STTNET_CHECK(queue.pop(output));
    STTNET_CHECK(output.value==9);
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
        STTNET_CHECK(value<totalValues);
        STTNET_CHECK(seen[value]==0);
        seen[value]=1;
        ++consumed;
    }

    for(auto &producer:producers)
        producer.join();
    STTNET_CHECK(queue.approx_size()==0);
}

void testWorkerPoolDrainsAndRejectsAfterStop()
{
    stt::system::WorkerPool pool(4);
    std::atomic<int> completed{0};
    for(int task=0;task<1000;++task)
        STTNET_CHECK(pool.submit([&completed] { completed.fetch_add(1,std::memory_order_relaxed); }));
    pool.stop();
    STTNET_CHECK(completed.load(std::memory_order_relaxed)==1000);
    STTNET_CHECK(!pool.submit([] {}));
}

void testWorkerPoolRejectsZeroWorkers()
{
    bool threw=false;
    try { stt::system::WorkerPool pool(0); }
    catch(const std::invalid_argument &) { threw=true; }
    STTNET_CHECK(threw);
}

void testWorkerPoolBoundsPendingTasks()
{
    stt::system::WorkerPool pool(1,2);
    std::atomic<bool> started{false};
    std::atomic<bool> release{false};
    std::atomic<int> completed{0};
    STTNET_CHECK(pool.maxPendingTasks()==2);
    STTNET_CHECK(pool.submit([&] {
        started.store(true,std::memory_order_release);
        while(!release.load(std::memory_order_acquire))
            std::this_thread::yield();
        completed.fetch_add(1,std::memory_order_relaxed);
    }));
    while(!started.load(std::memory_order_acquire))
        std::this_thread::yield();
    STTNET_CHECK(pool.submit([&] {completed.fetch_add(1,std::memory_order_relaxed);}));
    STTNET_CHECK(pool.submit([&] {completed.fetch_add(1,std::memory_order_relaxed);}));
    STTNET_CHECK(pool.pendingTasks()==2);
    STTNET_CHECK(pool.peakPendingTasks()==2);
    STTNET_CHECK(!pool.submit([] {}));
    release.store(true,std::memory_order_release);
    pool.stop();
    STTNET_CHECK(completed.load(std::memory_order_relaxed)==3);
    STTNET_CHECK(pool.pendingTasks()==0);
}

void testWorkerPoolCanDiscardPendingTasks()
{
    stt::system::WorkerPool pool(1,4);
    std::atomic<bool> started{false};
    std::atomic<bool> release{false};
    std::atomic<int> completed{0};
    STTNET_CHECK(pool.submit([&] {
        started.store(true,std::memory_order_release);
        while(!release.load(std::memory_order_acquire))
            std::this_thread::yield();
        completed.fetch_add(1,std::memory_order_relaxed);
    }));
    while(!started.load(std::memory_order_acquire))
        std::this_thread::yield();
    STTNET_CHECK(pool.submit([&] {completed.fetch_add(1,std::memory_order_relaxed);}));
    STTNET_CHECK(pool.submit([&] {completed.fetch_add(1,std::memory_order_relaxed);}));
    std::thread stopper([&] {pool.stop(false);});
    while(pool.pendingTasks()!=0)
        std::this_thread::yield();
    release.store(true,std::memory_order_release);
    stopper.join();
    STTNET_CHECK(completed.load(std::memory_order_relaxed)==1);
}

void testReactorWakeupsAreCoalesced()
{
    TestServer server;
    const int wakeFD=eventfd(0,EFD_NONBLOCK|EFD_CLOEXEC);
    STTNET_CHECK(wakeFD>=0);
    server.installWorkerEventFD(wakeFD);
    server.notifyReactor();
    server.notifyReactor();
    uint64_t value=0;
    STTNET_CHECK(::read(wakeFD,&value,sizeof(value))==static_cast<ssize_t>(sizeof(value)));
    STTNET_CHECK(value==1);
    const auto metrics=server.getMetrics();
    STTNET_CHECK(metrics.reactor_wakeups==1);
    STTNET_CHECK(metrics.reactor_wakeups_coalesced==1);
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

    STTNET_CHECK(handler.sendData(std::string("hello"))==5);
    const char binary[]={'\0','x','\0'};
    STTNET_CHECK(handler.sendData(binary,sizeof(binary),false)==3);
    STTNET_CHECK(writes.size()==2);
    STTNET_CHECK(writes[0]=="hello");
    STTNET_CHECK(writes[1].size()==3);
    STTNET_CHECK(writes[1][0]=='\0'&&writes[1][1]=='x'&&writes[1][2]=='\0');
    handler.close();
    STTNET_CHECK(closeRequested);
    STTNET_CHECK(handler.getFD()==-1);
}

void testHttpBinaryResponseAvoidsIntermediateBodyCopySemantics()
{
    stt::network::HttpServerFDHandler handler;
    handler.setFD(43,nullptr,true);
    std::string wire;
    handler.setTransportFunctions(
        [&wire](std::string data) {
            wire=std::move(data);
            return static_cast<int>(wire.size());
        },
        [] {}
    );
    const char body[]={'a','\0','b'};
    STTNET_CHECK(handler.sendBack(body,sizeof(body),"X-Test: yes","200 OK","",16));
    const std::string separator="\r\n\r\n";
    const size_t bodyOffset=wire.find(separator);
    STTNET_CHECK(bodyOffset!=std::string::npos);
    STTNET_CHECK(wire.find("Content-Length: 3\r\n")!=std::string::npos);
    STTNET_CHECK(wire.substr(bodyOffset+separator.size())==std::string(body,sizeof(body)));
}

void testPerConnectionWriteHighWaterMark()
{
    TestServer server;
    auto state=std::make_shared<stt::network::ConnectionWriteState>();
    state->fd=9;
    state->connection_obj_fd=77;
    state->max_queued_bytes=8;

    STTNET_CHECK(server.enqueueWrite(state,"1234")==4);
    STTNET_CHECK(server.enqueueWrite(state,"5678")==4);
    STTNET_CHECK(server.enqueueWrite(state,"x")==-101);
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        STTNET_CHECK(state->queued_bytes==8);
        STTNET_CHECK(state->queue.size()==2);
        STTNET_CHECK(state->overflowed);
        STTNET_CHECK(state->close_requested);
    }
    const auto metrics=server.getMetrics();
    STTNET_CHECK(metrics.queued_write_bytes==8);
    STTNET_CHECK(metrics.pending_write_bytes==8);
    STTNET_CHECK(metrics.peak_pending_write_bytes==8);
    STTNET_CHECK(metrics.write_overflows==1);
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
                STTNET_CHECK(server.enqueueWrite(state,std::to_string(producer)+":"+std::to_string(write)+"\n")>0);
        });
    }
    for(auto &producer:producers)
        producer.join();

    std::lock_guard<std::mutex> lock(state->mutex);
    STTNET_CHECK(state->queue.size()==producerCount*writesPerProducer);
    std::size_t actualBytes=0;
    for(const auto &entry:state->queue)
        actualBytes+=entry.size();
    STTNET_CHECK(actualBytes==state->queued_bytes);
    STTNET_CHECK(!state->overflowed);
}

void testWriteFlushPreservesOrderAndBackpressure()
{
    int sockets[2]={-1,-1};
    STTNET_CHECK(::socketpair(AF_UNIX,SOCK_STREAM,0,sockets)==0);
    const int flags=fcntl(sockets[0],F_GETFL,0);
    STTNET_CHECK(flags>=0&&fcntl(sockets[0],F_SETFL,flags|O_NONBLOCK)==0);

    TestServer server;
    auto state=std::make_shared<stt::network::ConnectionWriteState>();
    state->fd=sockets[0];
    state->connection_obj_fd=79;
    state->max_queued_bytes=2UL*1024UL*1024UL;
    stt::network::TcpFDInf connection;
    connection.fd=sockets[0];
    connection.write_state=state;

    STTNET_CHECK(server.enqueueWrite(state,"alpha")==5);
    STTNET_CHECK(server.enqueueWrite(state,"beta")==4);
    STTNET_CHECK(server.flushConnectionWrites(connection)==TestServer::WriteFlushResult::Drained);
    char ordered[9];
    STTNET_CHECK(::recv(sockets[1],ordered,sizeof(ordered),MSG_WAITALL)==9);
    STTNET_CHECK(std::string(ordered,sizeof(ordered))=="alphabeta");
    {
        const auto metrics=server.getMetrics();
        STTNET_CHECK(metrics.batched_write_syscalls==1);
        STTNET_CHECK(metrics.batched_write_buffers==2);
        STTNET_CHECK(metrics.write_syscalls==1);
    }

    int sendBuffer=1024;
    STTNET_CHECK(setsockopt(sockets[0],SOL_SOCKET,SO_SNDBUF,&sendBuffer,sizeof(sendBuffer))==0);
    const std::string largePayload(1024UL*1024UL,'x');
    STTNET_CHECK(server.enqueueWrite(state,largePayload)==static_cast<int>(largePayload.size()));
    const auto result=server.flushConnectionWrites(connection);
    STTNET_CHECK(result==TestServer::WriteFlushResult::WaitWrite||
           result==TestServer::WriteFlushResult::Reschedule);
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        STTNET_CHECK(state->queued_bytes>0);
        STTNET_CHECK(state->queued_bytes<largePayload.size());
    }
    ::close(sockets[0]);
    ::close(sockets[1]);
}

void testSocketBlockingModeTransitions()
{
    int sockets[2]={-1,-1};
    STTNET_CHECK(::socketpair(AF_UNIX,SOCK_STREAM,0,sockets)==0);
    stt::network::TcpFDHandler handler;
    handler.setFD(sockets[0],nullptr,false);
    handler.unblockSet();
    STTNET_CHECK((fcntl(sockets[0],F_GETFL,0)&O_NONBLOCK)!=0);
    handler.blockSet();
    STTNET_CHECK((fcntl(sockets[0],F_GETFL,0)&O_NONBLOCK)==0);
    handler.close(false);
    ::close(sockets[0]);
    ::close(sockets[1]);
}

void testTcpClientBlockingDnsAndConnectionMetadata()
{
    const int listener=::socket(AF_INET,SOCK_STREAM,0);
    STTNET_CHECK(listener>=0);
    int reuse=1;
    STTNET_CHECK(setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse))==0);
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
        STTNET_CHECK(false);
    }
    socklen_t addressSize=sizeof(address);
    STTNET_CHECK(getsockname(listener,reinterpret_cast<sockaddr*>(&address),&addressSize)==0);
    STTNET_CHECK(::listen(listener,1)==0);
    std::thread peer([listener] {
        const int connection=::accept(listener,nullptr,nullptr);
        STTNET_CHECK(connection>=0);
        char data[4]{};
        STTNET_CHECK(::recv(connection,data,sizeof(data),MSG_WAITALL)==4);
        STTNET_CHECK(std::string(data,sizeof(data))=="ping");
        ::close(connection);
    });

    stt::network::TcpClient client;
    const int port=ntohs(address.sin_port);
    STTNET_CHECK(client.connect("localhost",port));
    STTNET_CHECK((fcntl(client.getFD(),F_GETFL,0)&O_NONBLOCK)==0);
    STTNET_CHECK(client.getServerIP()=="localhost");
    STTNET_CHECK(client.getServerPort()==port);
    STTNET_CHECK(client.sendData("ping")==4);
    peer.join();
    ::close(listener);
}

#ifdef __linux__
int connectLoopback(const int port)
{
    const int connection=::socket(AF_INET,SOCK_STREAM|SOCK_CLOEXEC,0);
    STTNET_CHECK(connection>=0);
    sockaddr_in address{};
    address.sin_family=AF_INET;
    address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    address.sin_port=htons(static_cast<uint16_t>(port));
    STTNET_CHECK(::connect(connection,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0);
    return connection;
}

void testServerUsesRealConnectionCountAndEphemeralPort()
{
    TestServer server(1,64,1024,false);
    stt::network::ServerSocketOptions options;
    options.reuse_port=false;
    options.tcp_no_delay=true;
    options.listen_backlog=128;
    server.setSocketOptions(options);
    STTNET_CHECK(server.startListen(0,2));
    const int port=server.getListenPort();
    STTNET_CHECK(port>0&&port<=65535);

    const int first=connectLoopback(port);
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while(server.getMetrics().active_connections!=1&&std::chrono::steady_clock::now()<deadline)
        std::this_thread::yield();
    STTNET_CHECK(server.getMetrics().active_connections==1);

    const int second=connectLoopback(port);
    deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while(server.getMetrics().rejected_connections!=1&&std::chrono::steady_clock::now()<deadline)
        std::this_thread::yield();
    STTNET_CHECK(server.getMetrics().rejected_connections==1);
    STTNET_CHECK(server.getMetrics().active_connections==1);

    ::close(first);
    ::close(second);
    STTNET_CHECK(server.close());
    STTNET_CHECK(server.getListenPort()==-1);
}

void testIdleTimeoutUsesIncrementalCandidates()
{
    TestServer server(8,64,1024,true,20,1,100,1,100,2,1);
    stt::network::ServerSocketOptions options;
    options.reuse_port=false;
    server.setSocketOptions(options);
    STTNET_CHECK(server.startListen(0,1));
    const int peer=connectLoopback(server.getListenPort());
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(server.getMetrics().idle_timeout_closes==0&&std::chrono::steady_clock::now()<deadline)
        std::this_thread::yield();
    const auto metrics=server.getMetrics();
    STTNET_CHECK(metrics.idle_timeout_checks>=1);
    STTNET_CHECK(metrics.idle_timeout_closes==1);
    ::close(peer);
    STTNET_CHECK(server.close());
}

void testGracefulShutdownFlushesInFlightWorkerResponse()
{
    stt::network::HttpServer server(64,64,1024,false);
    server.setGracefulShutdownTimeout(3000);
    std::atomic<bool> workerStarted{false};
    server.setFunction("/slow",[&](stt::network::HttpServerFDHandler &handler,
                                    stt::network::HttpRequestInformation &request) {
        server.putTask([&workerStarted](stt::network::HttpServerFDHandler &workerHandler,
                                        stt::network::HttpRequestInformation &) {
            workerStarted.store(true,std::memory_order_release);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            return workerHandler.sendBack("done")?1:-2;
        },handler,request);
        return 0;
    });
    STTNET_CHECK(server.startListen(0,1));
    const int peer=connectLoopback(server.getListenPort());
    const std::string request="GET /slow HTTP/1.1\r\nHost: localhost\r\n\r\n";
    STTNET_CHECK(::send(peer,request.data(),request.size(),MSG_NOSIGNAL)==static_cast<ssize_t>(request.size()));
    const auto startDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while(!workerStarted.load(std::memory_order_acquire)&&std::chrono::steady_clock::now()<startDeadline)
        std::this_thread::yield();
    STTNET_CHECK(workerStarted.load(std::memory_order_acquire));

    std::atomic<bool> closeResult{false};
    std::thread closer([&] {closeResult.store(server.close(),std::memory_order_release);});
    timeval timeout{3,0};
    STTNET_CHECK(setsockopt(peer,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout))==0);
    std::string response;
    char buffer[512];
    ssize_t received=0;
    while((received=::recv(peer,buffer,sizeof(buffer),0))>0)
        response.append(buffer,static_cast<size_t>(received));
    closer.join();
    ::close(peer);
    STTNET_CHECK(closeResult.load(std::memory_order_acquire));
    STTNET_CHECK(response.find("\r\n\r\ndone")!=std::string::npos);
    STTNET_CHECK(server.getMetrics().graceful_shutdown_timeouts==0);
}

void testExternalConnectionCloseIsRoutedThroughReactor()
{
    TestServer server(16,64,1024,false);
    std::atomic<int> acceptedFD{-1};
    server.setGlobalSolveFunction([&acceptedFD](stt::network::TcpFDHandler &,
                                                stt::network::TcpInformation &information) {
        acceptedFD.store(information.fd,std::memory_order_release);
        return true;
    });
    STTNET_CHECK(server.startListen(0,1));
    const int peer=connectLoopback(server.getListenPort());
    const char payload='x';
    STTNET_CHECK(::send(peer,&payload,1,MSG_NOSIGNAL)==1);
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while(acceptedFD.load(std::memory_order_acquire)<0&&std::chrono::steady_clock::now()<deadline)
        std::this_thread::yield();
    STTNET_CHECK(acceptedFD.load(std::memory_order_acquire)>=0);
    STTNET_CHECK(server.close(acceptedFD.load(std::memory_order_acquire)));
    const auto closeDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while(server.getMetrics().active_connections!=0&&std::chrono::steady_clock::now()<closeDeadline)
        std::this_thread::yield();
    STTNET_CHECK(server.getMetrics().active_connections==0);
    ::close(peer);
    STTNET_CHECK(server.close());
}

void testServerCanRestartAfterStopListen()
{
    TestServer server(16,64,1024,false);
    stt::network::ServerSocketOptions options;
    options.reuse_port=false;
    server.setSocketOptions(options);
    STTNET_CHECK(server.startListen(0,1));
    const int firstPort=server.getListenPort();
    STTNET_CHECK(firstPort>0);
    STTNET_CHECK(server.stopListen());
    STTNET_CHECK(server.getListenPort()==-1);
    STTNET_CHECK(server.startListen(0,1));
    STTNET_CHECK(server.getListenPort()>0);
    STTNET_CHECK(server.close());
}

void testEpollSingleOwnsAndJoinsListenerThread()
{
    const int watched=eventfd(0,EFD_NONBLOCK|EFD_CLOEXEC);
    STTNET_CHECK(watched>=0);
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
    STTNET_CHECK(::write(watched,&one,sizeof(one))==static_cast<ssize_t>(sizeof(one)));
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while(listener.isListen()&&std::chrono::steady_clock::now()<deadline)
        std::this_thread::yield();
    STTNET_CHECK(callbacks.load(std::memory_order_relaxed)==1);
    STTNET_CHECK(listener.endListen());

    listener.startListen(watched);
    STTNET_CHECK(listener.endListen());
    ::close(watched);
}
#endif

} // namespace

int main()
{
    testQueueSupportsNonDefaultConstructibleValues();
    testQueueCopyFailureDoesNotPoisonRing();
    testMPSCQueueUnderContention();
    testWorkerPoolDrainsAndRejectsAfterStop();
    testWorkerPoolRejectsZeroWorkers();
    testWorkerPoolBoundsPendingTasks();
    testWorkerPoolCanDiscardPendingTasks();
    testReactorWakeupsAreCoalesced();
    testServerHandlerDelegatesTransportOperations();
    testHttpBinaryResponseAvoidsIntermediateBodyCopySemantics();
    testPerConnectionWriteHighWaterMark();
    testConcurrentPerConnectionEnqueue();
    testWriteFlushPreservesOrderAndBackpressure();
    testSocketBlockingModeTransitions();
    testTcpClientBlockingDnsAndConnectionMetadata();
#ifdef __linux__
    testServerUsesRealConnectionCountAndEphemeralPort();
    testIdleTimeoutUsesIncrementalCandidates();
    testGracefulShutdownFlushesInFlightWorkerResponse();
    testExternalConnectionCloseIsRoutedThroughReactor();
    testServerCanRestartAfterStopListen();
    testEpollSingleOwnsAndJoinsListenerThread();
#endif
    std::cout<<"all concurrency tests passed\n";
    return 0;
}
