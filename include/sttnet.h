/**
* @mainpage STTNet：简单、可嵌入的 C++17 网络框架
* @author StephenTaam(1356597983@qq.com)
* @version 0.7.0
* @date 2026-07-13
*
* STTNet 用少量 API 提供 Linux epoll TCP、HTTP/1.1、WebSocket、TLS、WorkerPool、
* 背压和优雅退出。最常见的服务只需要三步：创建 Server、注册回调、开始监听。
*
* @section integration 引入项目
* 安装后的 CMake 用户只需链接规范目标：
* @code{.cmake}
* find_package(STTNet 0.7 CONFIG REQUIRED)
* target_link_libraries(my_server PRIVATE STTNet::sttnet)
* @endcode
* 也可使用 `add_subdirectory` 或 FetchContent；全部方式都使用 `<sttnet.h>` 和
* `STTNet::sttnet`。完整说明见 `docs/GETTING_STARTED_Chinese.md`。
*
* @section quick_http 20 行启动 HTTP 服务
* @code{.cpp}
* #include <sttnet.h>
* using namespace stt::network;
* using stt::system::ServerSetting;
*
* int main()
* {
*     if(!ServerSetting::blockTerminationSignals()) return 1;
*     HttpServer server;
*     server.setFunction("/ping",[](HttpServerFDHandler &client,
*                                    HttpRequestInformation &) {
*         return client.sendText("pong") ? 1 : -2;
*     });
*     if(!server.startListen(8080)) return 2;
*     ServerSetting::waitForTerminationSignal(); // kill -15 或 Ctrl-C
*     return server.close() ? 0 : 3;
* }
* @endcode
*
* 测试：`curl http://127.0.0.1:8080/ping`。默认路由 key 就是 HTTP path，
* 因此这个最小示例不需要额外配置解析函数。
*
* @section http_helpers 常用 HTTP 操作
* @code{.cpp}
* auto contentType=request.headerValue("content-type");
* auto body=request.bodyView();
* client.sendText("created", "201 Created");
* Json::Value value;
* value["ok"]=true;
* client.sendJson(value);
* client.redirect("/login");
* @endcode
*
* @section quick_websocket 最小 WebSocket Echo 服务
* @code{.cpp}
* #include <sttnet.h>
* using namespace stt::network;
* using stt::system::ServerSetting;
*
* int main()
* {
*     if(!ServerSetting::blockTerminationSignals()) return 1;
*     WebSocketServer server;
*     server.setGlobalSolveFunction([](WebSocketServerFDHandler &client,
*                                      WebSocketFDInformation &message) {
*         return client.sendMessage(message.message); // 原样回显
*     });
*     if(!server.startListen(5050)) return 2;
*     ServerSetting::waitForTerminationSignal();
*     return server.close() ? 0 : 3;
* }
* @endcode
*
* @section quick_options 常用生产配置
* @code{.cpp}
* ServerSocketOptions options;
* options.tcp_no_delay=true;
* options.keep_alive=true;
* options.listen_backlog=4096;
* server.setSocketOptions(options);
* server.setMaxPendingWriteBytes(4 * 1024 * 1024);
* server.setGracefulShutdownTimeout(5000);
* // 普通 HTTPS/WSS：server.setTLS("server.crt", "server.key");
* @endcode
*
* @note 必须在创建 Worker/Reactor 线程前调用 blockTerminationSignals()。
*       不要在异步 signal handler 中 delete Server；SIGKILL (`kill -9`) 无法优雅处理。
*
* @defgroup stt STTNet 模块
* STTNet 的文件、时间、数据、网络、安全与系统命名空间。
*/
#ifndef PUBLIC_H
#define PUBLIC_H 1
#include<jsoncpp/json/json.h>
#include<string_view>
#include<string>
#include<atomic>
#include<iostream>
#include<unistd.h>
#include<sys/stat.h>
#include<fstream>
#include<fcntl.h>
#include<sstream>
#include<mutex>
#include<chrono>
#include<iomanip>
#include<random>
#include<cmath>
#include<thread>
#include<openssl/sha.h>
#include<netdb.h>
#include<arpa/inet.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<cstring>
#include<openssl/bio.h>
#include<openssl/evp.h>
#include<openssl/buffer.h>
#include<functional>
#include<list>
#include<queue>
#include<sys/epoll.h> 
#include<condition_variable>
#include <regex>
#include<unordered_map>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include<openssl/crypto.h>
#include<signal.h>
#include<sys/ipc.h>
#include<sys/sem.h>
#include<sys/wait.h>
#include<sys/shm.h>
#include<type_traits>
#include<charconv>
#include<any>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <cstddef>
#include <cstdint>
#include <new>
#include <vector>
#include <memory>
#include <limits>
#include <poll.h>
#include <exception>
#include <cstdlib>
#include <cstdio>
#include <deque>
#include <algorithm>
#include <array>
#include <unordered_set>
#include <cctype>
#include <sys/uio.h>
#include <netinet/tcp.h>
#ifdef __linux__
#include <sys/prctl.h>
#include <sys/syscall.h>
#endif
/**
* @namespace stt
*/

namespace stt
{

    /** STTNet semantic version exposed to build-time and runtime diagnostics. */
    inline constexpr int version_major=0;
    inline constexpr int version_minor=7;
    inline constexpr int version_patch=0;
    inline constexpr std::string_view version="0.7.0";

    namespace system
    {
        class WorkerPool;

/**
 * @brief Lock-free bounded MPSC queue (Multi-Producer Single-Consumer)
 *        无锁有界多生产者单消费者队列（环形缓冲）
 *
 * - Multiple threads may push concurrently.
 * - Only ONE thread may pop.
 *
 * - 多个线程可以同时 push
 * - 仅允许一个线程 pop（通常是 reactor 线程）
 *
 * Design:
 * - Fixed-size ring buffer (power-of-two capacity)
 * - Per-slot sequence number to coordinate producers/consumer
 *
 * 特点：
 * - 零 malloc/零 free（不为每个元素分配 Node）
 * - cache 友好
 * - push/pop 只用原子 + 轻量自旋
 *
 * IMPORTANT:
 *  ❗ Capacity must be a power of two.
 *  ❗ pop() must be called by only one thread.
 *
 * 重要：
 *  ❗ 容量必须是 2 的幂
 *  ❗ pop 只能由一个线程调用
 */
template <typename T>
class MPSCQueue {
public:
    explicit MPSCQueue(std::size_t capacity_pow2)
        : capacity_(capacity_pow2),
          mask_(capacity_pow2 - 1),
          buffer_(capacity_pow2),
          head_(0),
          tail_(0)
    {
        static_assert(std::is_nothrow_move_constructible_v<T>,
                      "MPSCQueue requires a nothrow-move-constructible value type");
        // capacity must be power of two
        if (capacity_ < 2 || (capacity_ & mask_) != 0) {
            // You can replace with your own assert/log
            throw std::invalid_argument("MPSCQueue capacity must be power of two and >= 2");
        }

        // Initialize per-slot sequence
        for (std::size_t i = 0; i < capacity_; ++i) {
            buffer_[i].seq.store(i, std::memory_order_relaxed);
        }
    }

    MPSCQueue(const MPSCQueue&) = delete;
    MPSCQueue& operator=(const MPSCQueue&) = delete;

    ~MPSCQueue() {
        // Drain remaining items to call destructors if needed
        while (discard_one()) {}
    }

    /**
     * @brief Try push (non-blocking). Returns false if queue is full.
     *        尝试入队（非阻塞），队列满则返回 false
     */
    bool push(T&& v) noexcept(std::is_nothrow_move_constructible_v<T>) {
        return emplace_impl(std::move(v));
    }

    bool push(const T& v) {
        // Copy before reserving a ring slot. If the copy throws, producer
        // progress and the per-slot sequence state remain untouched.
        T copy(v);
        return emplace_impl(std::move(copy));
    }

    /**
     * @brief Try pop (single consumer). Returns false if empty.
     *        尝试出队（单消费者），空则返回 false
     */
    bool pop(T& out) noexcept(std::is_nothrow_move_assignable_v<T> &&
                              std::is_nothrow_move_constructible_v<T>)
    {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        Slot& slot = buffer_[head & mask_];
        const std::size_t seq = slot.seq.load(std::memory_order_acquire);
        const std::intptr_t dif = static_cast<std::intptr_t>(seq) - static_cast<std::intptr_t>(head + 1);

        if (dif != 0) {
            // seq != head+1 => empty
            return false;
        }

        // Move out
        out = std::move(*slot.ptr());

        // Destroy in-place
        slot.destroy();

        // Mark slot as free for producers:
        // seq = head + capacity
        slot.seq.store(head + capacity_, std::memory_order_release);

        head_.store(head + 1, std::memory_order_relaxed);
        return true;
    }

    /**
     * @brief Approximate size (may be inaccurate under concurrency)
     *        近似长度（并发下可能不精确）
     */
    std::size_t approx_size() const noexcept {
        const std::size_t t = tail_.load(std::memory_order_acquire);
        const std::size_t h = head_.load(std::memory_order_acquire);
        return (t >= h) ? (t - h) : 0;
    }

    /**
     * @brief 判断队列是否可能包含数据。
     * @note 该结果用于唤醒/调度提示，不应用作并发正确性的唯一依据；真正消费仍以 pop() 为准。
     */
    bool possibly_nonempty() const noexcept {
        return tail_.load(std::memory_order_acquire) !=
               head_.load(std::memory_order_acquire);
    }

private:
    struct Slot {
        std::atomic<std::size_t> seq;
        typename std::aligned_storage<sizeof(T), alignof(T)>::type storage;
        bool has_value = false;

        T* ptr() noexcept { return reinterpret_cast<T*>(&storage); }
        const T* ptr() const noexcept { return reinterpret_cast<const T*>(&storage); }

        template <class U>
        void construct(U&& v) noexcept(std::is_nothrow_constructible_v<T, U&&>) {
            ::new (static_cast<void*>(&storage)) T(std::forward<U>(v));
            has_value = true;
        }

        void destroy() noexcept {
            if (has_value) {
                ptr()->~T();
                has_value = false;
            }
        }
    };

    template <class U>
    bool emplace_impl(U&& v) noexcept(std::is_nothrow_constructible_v<T, U&&>) {
        std::size_t pos = tail_.load(std::memory_order_relaxed);

        for (;;) {
            Slot& slot = buffer_[pos & mask_];
            const std::size_t seq = slot.seq.load(std::memory_order_acquire);
            const std::intptr_t dif = static_cast<std::intptr_t>(seq) - static_cast<std::intptr_t>(pos);

            if (dif == 0) {
                // slot is free for this pos
                if (tail_.compare_exchange_weak(
                        pos, pos + 1,
                        std::memory_order_relaxed,
                        std::memory_order_relaxed))
                {
                    // We own this slot now
                    slot.construct(std::forward<U>(v));
                    // Publish to consumer: seq = pos+1 means "ready"
                    slot.seq.store(pos + 1, std::memory_order_release);
                    return true;
                }
                // CAS failed: pos updated with current tail; retry
            } else if (dif < 0) {
                // slot seq < pos => queue is full (producer wrapped)
                return false;
            } else {
                // Another producer is ahead; move pos forward
                pos = tail_.load(std::memory_order_relaxed);
            }
        }
    }

    bool discard_one() noexcept {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        Slot& slot = buffer_[head & mask_];
        const std::size_t seq = slot.seq.load(std::memory_order_acquire);
        if (static_cast<std::intptr_t>(seq) - static_cast<std::intptr_t>(head + 1) != 0)
            return false;
        slot.destroy();
        slot.seq.store(head + capacity_, std::memory_order_release);
        head_.store(head + 1, std::memory_order_relaxed);
        return true;
    }

private:
    const std::size_t capacity_;
    const std::size_t mask_;
    std::vector<Slot> buffer_;

    // Single consumer only
    alignas(64) std::atomic<std::size_t> head_;

    // Multi-producer
    alignas(64) std::atomic<std::size_t> tail_;
};


    }
    /**
    * @namespace stt::file
    * @brief 文件相关：文件读写，日志等
    * @ingroup stt
    */
    namespace file
    {
    /**
    * @brief 提供文件操作的静态函数工具类
    */
    class FileTool
    {
    public:
        /**
        * @brief 新建一个目录
        * @param ddir 目录路径，可以绝对路径也可以相对路径
        * @param mode 位掩码表示新建目录的权限（默认为0775 即rwx rwx r-x）
        * @return true 操作成功
        * @return false 操作失败
        */
        static bool createDir(const std::string & ddir,const mode_t &mode=0775); 
        /**
        * @brief 复制文件
        * @param sourceFile 源文件路径，可以绝对路径也可以相对路径
        * @param objectFile 目标文件路径，可以绝对路径也可以相对路径
        * @return true 操作成功
        * @return false 操作失败
        * @note 不会自动创造不存在的路径
        */
        static bool copy(const std::string &sourceFile,const std::string &objectFile);
        /**
        * @brief 新建一个文件
        * @param filePath 文件路径，可以绝对路径也可以相对路径
        * @param mode 位掩码表示新建文件的权限（默认为0666 即rw- rw- rw-）
        * @return true 操作成功
        * @return false 操作失败
        * @note 会自动创造原本不存在的路径
        */
        static bool createFile(const std::string &filePath,const mode_t &mode=0666);
        /**
        * @brief 获取文件大小
        * @param fileName 文件名字（可以填绝对路径也可以填相对路径）
        * @return >=0 返回文件大小
        * @return -1 获取文件大小失败
        */
        static size_t get_file_size(const std::string &fileName);
    };
    
    /**
    * @brief 记录文件和线程关系的结构体
    * @note 用于在后续类中使用，保证线程安全
    */
    struct FileThreadLock
    {
        /**
        * @brief 文件路径
        */
        std::string loc;
        /**
        * @brief 记录文件正在被多少个线程使用
        */
        int threads;
        /**
        * @brief 此文件的锁
        */
        std::mutex lock;
        /**
        * @brief 这个结构体的构造函数
        * @param loc 传入路径构造结构体对象
        * @param threads 传入文件被多少个线程使用的数量构造结构体对象
        */
        FileThreadLock(const std::string &loc,const int &threads):loc(loc),threads(threads){};
    };

    /**
    * @brief 读写磁盘文件的类
    * @note 1、本类的同一个对象能确保同步性和线程安全
    * @note 2、如果用本类的对象操作同一个文件也能保证同步性。但注意打开的时候统一这个文件的路径，要么用绝对路径，要么用相对路径。
    * @note 3、这个类操作文件的时候有两种模式：1，读取磁盘+操作内存的数据+保存到磁盘；这三步分开做，适合自定义复杂操作的场景。 2，把三步操作合并成一步操作完成
    */
    class File:private FileTool
    {
    protected:
        static std::mutex l1;
        static std::unordered_map<std::string,FileThreadLock> fl2;
    protected:
        std::mutex che;
        
    private:
        void lockfl2();
        void unlockfl2();
        bool memoryLockOwnedByCurrentThread();
        void markMemoryLockOwned();
        void releaseMemoryLocks();
    private:
        std::ifstream fin;
        std::vector<std::string> data;
        std::vector<std::string> backUp;
        char *data_binary=nullptr;
        char *backUp_binary=nullptr;
        size_t size1=0;
        size_t size2=0;
        int multiple=0;
        size_t multiple_backup=0;
        size_t malloced=0;
        std::mutex fl1;
        std::mutex memoryOwnerMutex;
        std::thread::id memoryLockOwner{};
        bool memoryLocked=false;
        
        std::ofstream fout;
        std::string fileName;
	    
        std::string fileNameTemp;
        std::atomic<bool> flag{false};
        bool binary;
        mode_t mode;
        size_t size=0;
        uint64_t totalLines=0;
    private:
        void toMemory();
        bool toDisk();
    public:
        /**
        * @brief 打开文件
        * @param fileName 打开文件的名字（可以用绝对路径或者相对路径）
        * @param create true：当文件不存在的时候创建文件（以及目录） false：当文件不存在的时候不创建文件 （默认为true）
        * @param multiple 当>=1的时候启用二进制打开文件，这个值为需要操作文件所需的预定的文件空间大小和原文件大小的比值  当<1的时候采用文本模式打开文件 （默认为0 文本模式打开）
        * @param size 手动填入所需的预定文件空间的大小，当这个参数非0时，不会采用multiple的倍数预留值而是直接用这个预定大小 （单位字节）（默认为0）
        * @param mode 如果create为true，且文件不存在，用位掩码表示新建文件的权限（默认为0666 rw- rw- rw-）
        * @return true:打开成功
        * @return false:打开失败
        * @note 任何操作都需要先打开文件
        * @note 二进制模式下写入超过预留空间会安全返回 false；空文件如需写入，应通过 size 指定容量。
        *
        * 示例代码 1： 文本模式打开和程序同一路径下名为"test"的文件
        *
        * @code
        * File f; 
        * f.openFile("test");
        * @endcode
        *
        * 示例代码 2： 二进制模式下打开和程序同一路径下名为"test"的文件,计划只读
        *
        * @code
        * File f; 
        * f.openFile("test",true,1); //只读说明预留空间是原来的一倍（和原来一样）即可 
        * @endcode
        *
        * 示例代码 3： 二进制模式下打开和程序同一路径下名为"test"的文件,预计会写入数据，写入后大小不大于原来文件大小的两倍
        *
        * @code
        * File f; 
        * f.openFile("test",true,2); //留出两倍的空间
        * @endcode
        *
        * 示例代码 4： 二进制模式下打开和程序同一路径下名为"test"的文件,预计会写入数据，写入大小不大于1024字节，原文件大小为0
        *
        * @code
        * File f; 
        * f.openFile("test",true,1,1024);//由于文件大小为0，代表倍数的第三个参数multiple填入多少都是无效的，需要手动指定大小
        * @endcode
        */
        bool openFile(const std::string &fileName,const bool &create=true,const int &multiple=0,const size_t &size=0,const mode_t &mode=0666);
        /** 关闭文件
        * @brief 关闭已打开了的文件
        * @param del true:删除文件 false：不删除文件 （默认为false）
        * @return true:操作成功或已经关闭 false：操作失败
        * @note 可重复调用；若其他线程正在进行内存事务，会等待事务结束后再关闭。
        */
        bool closeFile(const bool &del=false);
        /**
        * @brief 析构函数
        * @note 默认对象生命周期结束时关闭文件，不删除文件。
        */
        ~File(){closeFile(false);}
        /**
        * @brief 判断对象是否打开了文件
        * @return true：对象打开了文件 false：对象没有打开文件
        */
        bool isOpen(){return flag.load(std::memory_order_acquire);}
        /**
        * @brief 判断对象是否以二进制模式打开文件
        * @return true：对象以二进制模式打开文件  false：对象不以二进制模式打开文件
        */
        bool isBinary(){return binary;}
        /**
        * @brief 获取打开的文件名字
        * @return 返回一个打开的文件的名字的字符串
        */
        std::string getFileName(){return fileName;}
        /**
        * @brief 获取打开的文件的行数
        * @return 返回打开的文件的行数
        * @warning 1，只有在文本模式下打开才可能返回正确的值
        * @warning 2，获取的是上一次读入内存的时候的文件的行数
        * @warning 3，如果没有把文件读入过内存，返回的值不会是正确的
        * @note 这个函数也许只有在操作内存数据的时候有作用
        */
        uint64_t getFileLine(){return totalLines;}
        /**
        * @brief 获取二进制打开的文件的大小
        * @return 返回二进制打开的文件的大小
        * @warning 1，只有在二进制模式下打开才可能返回正确的值
        * @warning 2，获取的是上一次读入内存的时候的文件的大小
        * @warning 3，如果没有把文件读入过内存，返回的值不会是正确的
        * @note 这个函数也许只有在操作内存数据的时候有作用
        */
        size_t getFileSize(){return size;}
        /**
        * @brief 获取二进制打开的文件在内存中的大小
        * @return 返回二进制打开的文件在内存中的大小
        * @warning 1，只有在二进制模式下打开才可能返回正确的值
        * @warning 2，获取的是此时内存中操作的文件的大小
        * @warning 3，如果没有把文件读入过内存，返回的值不会是正确的
        * @note 这个函数只有在操作内存数据的时候有作用
        */
        size_t getSize1(){return size1;}
    public:
        /**
        * @brief 把数据从磁盘读入内存
        * @return true 操作成功
        * @return false 操作失败
        * @note 成功后必须由同一线程调用 unlockMemory()；同一线程重复加锁会返回 false。
        */
        bool lockMemory();
        /**
        * @brief 把数据从内存写入磁盘
        * @param rec false 不回退操作，把操作结果保存入磁盘； true 回退操作，不保存。（默认为false 不需要回退）。
        * @return true 操作成功
        * @return false 操作失败
        * @note 如果写入磁盘失败会自动回退操作
        * @note 只有成功调用 lockMemory() 的线程可以解锁；其他线程调用会返回 false。
        */     
        bool unlockMemory(const bool &rec=false);
    public:
        /**
        * @name 操作内存中的数据的函数-文本模式
        * @{
        */
        /**
        * @brief 查找行
        * @note 1、查找文件导入内存的数据中存在目标字符串的首行
        * @note 2、行数从1开始
        * @param targetString 需要查找的字符串
        * @param linePos 从指定行开始寻找（默认从第一行开始）
        * @return >=1 返回匹配的行数
        * @return -1 查找失败
        */
        int findC(const std::string &targetString,const int linePos=1);
        /**
        * @brief 插入行
        * @note 1、在文件导入内存的数据中插入一行
        * @note 2、行数从1开始
        * @param data 需要插入的数据
        * @param linePos 在指定行插入（默认从末尾插入）
        * @return true 插入成功
        * @return false 插入失败
        */
        bool appendLineC(const std::string &data,const int &linePos=0);
        /**
        * @brief 删除行
        * @note 1、删除文件导入内存的数据中的一行
        * @note 2、行数从1开始
        * @param linePos 需要删除的行
        * @return true 删除成功
        * @return false 删除失败
        */
        bool deleteLineC(const int &linePos=0);
        /**
        * @brief 删除全部
        * @note 删除文件导入内存的数据中的全部
        * @return true 删除成功
        * @return false 删除失败
        */
        bool deleteAllC();
        /**
        * @brief 修改行
        * @note 1、修改文件导入内存的数据中的一行
        * @note 2、行数从1开始
        * @param data 覆盖到指定行的数据
        * @param linePos 需要覆盖的行（默认最后一行）
        * @return true 修改成功
        * @return false 修改失败
        */
        bool chgLineC(const std::string &data,const int &linePos=0);
        /**
        * @brief 读取单行
        * @note 1、读取文件导入内存的数据中的单行
        * @note 2、行数从1开始
        * @param data 接收数据的字符串容器
        * @param linePos 读取的行
        * @return true 读取成功
        * @return false 读取失败
        */
        bool readLineC(std::string &data,const int linePos);
        /**
        * @brief 读取行
        * @note 1、连续读取文件导入内存的数据中的n行
        * @note 2、行数从1开始
        * @param data 接收数据的字符串容器
        * @param linePos 读取的起始位置
        * @param num 读取的行的数量
        * @return 字符串参数data的引用
        */
        std::string& readC(std::string &data,const int &linePos,const int &num);
        /**
        * @brief 读取全部
        * @note 读取文件导入内存的数据中的全部
        * @param data 接收数据的字符串容器
        * @return 字符串参数data的引用
        */
        std::string& readAllC(std::string &data);
        /** @} */
        /**
        * @name 操作内存中的数据的函数-二进制模式
        * @{
        */
        /**
        * @brief 读取数据块
        * @note 1、读取文件导入内存的数据中的一块数据
        * @note 2、数据字节单元从0开始
        * @param data 接收数据的容器
        * @param pos 数据起始位置
        * @param size 数据块大小
        * @return bool 读取成功
        * @return false 读取失败
        */
        bool readC(char *data,const size_t &pos,const size_t &size);
        /**
        * @brief 写数据块
        * @note 1、往文件导入内存的数据中写入一块数据
        * @note 2、数据字节单元从0开始
        * @param data 装着写入数据的容器
        * @param pos 写入位置
        * @param size 写入数据块大小
        * @return bool 写入成功
        * @return false 写入失败
        */
        bool writeC(const char *data,const size_t &pos,const size_t &size);
        /**
        * @brief 格式化数据
        * @note 把文件导入内存的数据全部删除
        */
        bool formatC();
        /** @} */
    public:
        /**
        * @name 直接操作磁盘数据的函数-文本模式
        * @{
        */
        /**
        * @brief 查找行
        * @note 1、查找文件中存在目标字符串的首行
        * @note 2、行数从1开始
        * @param targetString 需要查找的字符串
        * @param linePos 从指定行开始寻找（默认从第一行开始）
        * @return >=1 返回匹配的行数
        * @return -1 查找失败
        */
        int find(const std::string &targetString,const int linePos=1);
        /**
        * @brief 插入行
        * @note 1、在文件中插入一行
        * @note 2、行数从1开始
        * @param data 需要插入的数据
        * @param linePos 在指定行插入（默认从末尾插入）
        * @return true 插入成功
        * @return false 插入失败
        */
        bool appendLine(const std::string &data,const int &linePos=0);
        /**
        * @brief 删除行
        * @note 1、删除文件中的一行
        * @note 2、行数从1开始
        * @param linePos 需要删除的行
        * @return true 删除成功
        * @return false 删除失败
        */
        bool deleteLine(const int &linePos=0);
        /**
        * @brief 删除全部
        * @note 删除文件中的全部行
        * @return true 删除成功
        * @return false 删除失败
        */
        bool deleteAll();
        /**
        * @brief 修改行
        * @note 1、修改文件中的一行
        * @note 2、行数从1开始
        * @param data 覆盖到指定行的数据
        * @param linePos 需要覆盖的行（默认最后一行）
        * @return true 修改成功
        * @return false 修改失败
        */
        bool chgLine(const std::string &data,const int &linePos=0);
        /**
        * @brief 读取单行
        * @note 1、读取文件中的单行
        * @note 2、行数从1开始
        * @param data 接收数据的字符串容器
        * @param linePos 读取的行
        * @return true 读取成功
        * @return false 读取失败
        */
        bool readLine(std::string &data,const int linePos);
        /**
        * @brief 读取行
        * @note 1、连续读取文件中的n行
        * @note 2、行数从1开始
        * @param data 接收数据的字符串容器
        * @param linePos 读取的起始位置
        * @param num 读取的行的数量
        * @return 字符串参数data的引用
        */
        std::string& read(std::string &data,const int &linePos,const int &num);
        /**
        * @brief 读取全部
        * @note 读取文件中的全部
        * @param data 接收数据的字符串容器
        * @return 字符串参数data的引用
        */
        std::string& readAll(std::string &data);
        /** @} */
        /**
        * @name 直接操作磁盘数据的函数-二进制模式
        * @{
        */
        /**
        * @brief 读取数据块
        * @note 1、读取文件中的一块数据
        * @note 2、数据字节单元从0开始
        * @param data 接收数据的容器
        * @param pos 数据起始位置
        * @param size 数据块大小
        * @return bool 读取成功
        * @return false 读取失败
        */
        bool read(char *data,const size_t &pos,const size_t &size);
        /**
        * @brief 写数据块
        * @note 1、往文件中写入一块数据
        * @note 2、数据字节单元从0开始
        * @param data 装着写入数据的容器
        * @param pos 写入位置
        * @param size 写入数据块大小
        * @return bool 写入成功
        * @return false 写入失败
        */
        bool write(const char *data,const size_t &pos,const size_t &size);
        /**
        * @brief 格式化数据
        * @note 把文件的数据全部删除
        */
        void format();
        /** @} */
    };
    }
    /**
    * @namespace stt::time
    * @brief 时间相关操作，基础时间工具
    * @ingroup stt
    */
    namespace time
    {
    /**
    * @brief 表示时间间隔的结构体，支持天、小时、分钟、秒和毫秒粒度。
    * @note 1、提供了对时间间隔的基本操作，如加减运算、比较操作、单位转换等。
    * @note 2、本结构体并不表示绝对时间点，仅用于表示两个时间点之间的差值。
    * @note 3、内部实现采用多个字段组合，而非统一的时间戳，以提高可读性和可控性。
    */
    struct Duration
    {
        /**
        * @brief 天
        */
        long long day;
        /**
        * @brief 时
        */
        int hour;
        /**
        * @brief 分
        */
        int min;
        /**
        * @brief 秒
        */
        int sec;
        /**
        * @brief 毫秒
        */
        int msec;
        /**
        * @brief 构造函数，传入天，时，分，秒，毫秒
        */
        Duration(long long a,int b,int c,int d,int e):day(a),hour(b),min(c),sec(d),msec(e){}
        Duration()=default;
        /**
        * @brief 判断当前时间间隔是否大于另一个时间间隔。
        * @param b 要比较的另一个 Duration 实例。
        * @return 如果当前对象大于参数 b，返回 true，否则返回 false。
        */
        bool operator>(const Duration &b)
        {
            long long total;
            total=day*24*60*60*1000+hour*60*60*1000+min*60*1000+sec*1000+msec;
            long long totalB;
            totalB=b.day*24*60*60*1000+b.hour*60*60*1000+b.min*60*1000+b.sec*1000+b.msec;
            if(total>totalB)
                return true;
            else
                return false;
        }
        /**
        * @brief 判断当前时间间隔是否小于另一个时间间隔。
        * @param b 要比较的另一个 Duration 实例。
        * @return 如果当前对象小于参数 b，返回 true，否则返回 false。
        */
        bool operator<(const Duration &b)
        {
            long long total;
            total=day*24*60*60*1000+hour*60*60*1000+min*60*1000+sec*1000+msec;
            long long totalB;
            totalB=b.day*24*60*60*1000+b.hour*60*60*1000+b.min*60*1000+b.sec*1000+b.msec;
            if(total<totalB)
                return true;
            else
                return false;
        }
        /**
        * @brief 判断当前时间间隔是否等于另一个时间间隔。
        * @param b 要比较的另一个 Duration 实例。
        * @return 如果当前对象等于参数 b，返回 true，否则返回 false。
        */
        bool operator==(const Duration &b)
        {
            long long total;
            total=day*24*60*60*1000+hour*60*60*1000+min*60*1000+sec*1000+msec;
            long long totalB;
            totalB=b.day*24*60*60*1000+b.hour*60*60*1000+b.min*60*1000+b.sec*1000+b.msec;
            if(total==totalB)
                return true;
            else
                return false;
        }
        /**
        * @brief 判断当前时间间隔是否大于等于另一个时间间隔。
        * @param b 要比较的另一个 Duration 实例。
        * @return 如果当前对象大于等于参数 b，返回 true，否则返回 false。
        */
        bool operator>=(const Duration &b)
        {
            long long total;
            total=day*24*60*60*1000+hour*60*60*1000+min*60*1000+sec*1000+msec;
            long long totalB;
            totalB=b.day*24*60*60*1000+b.hour*60*60*1000+b.min*60*1000+b.sec*1000+b.msec;
            if(total>=totalB)
                return true;
            else
                return false;
        }
        /**
        * @brief 判断当前时间间隔是否小于等于另一个时间间隔。
        * @param b 要比较的另一个 Duration 实例。
        * @return 如果当前对象小于等于参数 b，返回 true，否则返回 false。
        */
        bool operator<=(const Duration &b)
        {
            long long total;
            total=day*24*60*60*1000+hour*60*60*1000+min*60*1000+sec*1000+msec;
            long long totalB;
            totalB=b.day*24*60*60*1000+b.hour*60*60*1000+b.min*60*1000+b.sec*1000+b.msec;
            if(total<=totalB)
                return true;
            else
                return false;
        }
        /**
        * @brief 将两个时间间隔相加。
        * @param b 要相加的另一个 Duration。
        * @return 相加后的 Duration。
        */
        Duration operator+(const Duration &b)
        {
            long long dayy=day;
            int hourr=hour;
            int minn=min;
            int secc=sec;
            int msecc=msec;

            msecc+=b.msec;
            secc+=b.sec;
            minn+=b.min;
            hourr+=b.hour;
            dayy+=b.day;

            if(msecc/1000!=0)
            {
                secc+=msecc/1000;
                msecc=msecc%1000;
            }

            if(secc/60!=0)
            {
                minn+=secc/60;
                secc=secc%60;
            }

            if(minn/60!=0)
            {
                hourr+=minn/60;
                minn=minn%60;
            }

            if(hourr/24!=0)
            {
                dayy+=hourr/24;
                hourr=hourr%24;
            }
            return Duration(dayy,hourr,minn,secc,msecc);
        }
        /**
        * @brief 计算两个时间间隔的差值（当前对象减去参数 b）。
        * @param b 要减去的另一个 Duration。
        * @return 差值 Duration。
        */
        Duration operator-(const Duration &b)
        {
            long long dayy=day;
            int hourr=hour;
            int minn=min;
            int secc=sec;
            int msecc=msec;

            msecc=dayy*24*60*60*1000+hourr*60*60*1000+minn*60*1000+secc*1000+msecc-b.day*24*60*60*1000-b.hour*60*60*1000-b.min*60*1000-b.sec*1000-b.msec;
            secc=0;
            minn=0;
            hourr=0;
            dayy=0;

            if(msecc/1000!=0)
            {
                secc+=msecc/1000;
                msecc=msecc%1000;
            }

            if(secc/60!=0)
            {
                minn+=secc/60;
                secc=secc%60;
            }

            if(minn/60!=0)
            {
                hourr+=minn/60;
                minn=minn%60;
            }

            if(hourr/24!=0)
            {
                dayy+=hourr/24;
                hourr=hourr%24;
            }
            return Duration(dayy,hourr,minn,secc,msecc);
        }
        
        /**
        * @brief 将当前时间间隔转换为以“天”为单位的浮点数表示。
        */
        double convertToDay()
        { 
            long long total;
            total=hour*60*60*1000+min*60*1000+sec*1000+msec; 
            double k=day+total/86400000.0000;
            return k;
        }
        /**
        * @brief 将当前时间间隔转换为以“小时”为单位的浮点数表示。
        */
        double convertToHour()
        {
            long long total;
            total=min*60*1000+sec*1000+msec; 
            double k=day*24+hour+total/36000000.0000;
            return k;
        }
        /**
        * @brief 将当前时间间隔转换为以“分钟”为单位的浮点数表示。
        */
        double convertToMin()
        {
            long long total;
            total=sec*1000+msec; 
            double k=day*24*60+hour*60+min+total/60000.0000;
            return k;
        }
        /**
        * @brief 将当前时间间隔转换为以“秒”为单位的浮点数表示。
        */
        double convertToSec()
        {
            long long total;
            total=msec; 
            double k=day*24*60*60+hour*60*60+min*60+sec+total/1000.0000;
            return k;
        }
        /**
         * @brief 将当前时间间隔转换为总毫秒数。
        */
        long long convertToMsec()
        {
            long long total;
            total=day*24*60*60*1000+hour*60*60*1000+min*60*1000+sec*1000+msec;
            return total;
        }
        /**
        * @brief 从给定的毫秒数恢复为标准的天-时-分-秒-毫秒格式。
        * @param t 要恢复的毫秒值。
        * @return 转换后的 Duration。
        */
        Duration recoverForm(const long long &t)
        {
            msec=t;
            sec=0;
            min=0;
            hour=0;
            day=0;

            if(msec/1000!=0)
            {
                sec+=msec/1000;
                msec=msec%1000;
            }

            if(sec/60!=0)
            {
                min+=sec/60;
                sec=sec%60;
            }

            if(min/60!=0)
            {
                hour+=min/60;
                min=min%60;
            }

            if(hour/24!=0)
            {
                day+=hour/24;
                hour=hour%24;
            }
            return Duration(day,hour,min,sec,msec);
        }
    };
    /**
    * @brief 将 Duration 对象以可读格式输出到流中。
    *
    * 该函数用于将 Duration 的各个字段（天、小时、分钟、秒、毫秒）格式化后输出到给定的输出流中。
    *
    * @param os 输出流（如 std::cout）。
    * @param a 要输出的 Duration 对象。
    * @return 输出流的引用，用于链式输出。
    *
    * @note 通常输出格式为类似于 "1d 02:03:04.005" 的人类可读格式（具体取决于实现）。
    */
    std::ostream& operator<<(std::ostream &os,const Duration &a);

    using Milliseconds = std::chrono::duration<uint64_t,std::milli>;
    using Seconds=std::chrono::duration<uint64_t>;
    /**
    * @brief 定义ISO8086A这个宏为"yyyy-mm-ddThh:mi:ss"
    */
    #define ISO8086A "yyyy-mm-ddThh:mi:ss"
    /**
    * @brief 定义ISO8086B这个宏为"yyyy-mm-ddThh:mi:ss.sss"
    */
    #define ISO8086B "yyyy-mm-ddThh:mi:ss.sss"


    /**
    * @brief 时间操作、运算、计时的类
    * @brief 精确到毫秒
    * @warning 只有在1970+-292年内的是确保准确的
    * @bug 只有1970+-292年内确保准确,待优化
    */
    class DateTime
    {
    private:
        static Duration& dTOD(const Milliseconds& d1,Duration &D1);
        static Milliseconds& DTOd(const Duration &D1,Milliseconds& d1);
        static std::string &toPGtimeFormat();
        static std::chrono::system_clock::time_point strToTimePoint(const std::string &timeStr,const std::string &format=ISO8086A);
        static std::string& timePointToStr(const std::chrono::system_clock::time_point &tp,std::string &timeStr,const std::string &format=ISO8086A);
    public:
        /**
        * @brief 获取当前时间
        * @note 获取当前时间，返回字符串
        * @param timeStr 接收时间的字符串容器
        * @param format 指定时间字符串的格式 yyyy年 mm月 dd日 hh时 mi分 ss秒 sss毫秒 （默认格式为'yyyy-mm-ddThh:mi:ss',即ISO08086A标准）
        * @return 返回timeStr的引用
        */
        static std::string& getTime(std::string &timeStr,const std::string &format=ISO8086A);
        /**
        * @brief 转化时间字符串的格式
        * @note 传入时间字符串的引用修改原字符串
        * @param timeStr 原时间字符串
        * @param oldFormat 原时间字符串格式 （yyyy年 mm月 dd日 hh时 mi分 ss秒 sss毫秒）
        * @param newFormat 新的时间格式 （默认格式为'yyyy-mm-ddThh:mi:ss',即ISO08086A标准）
        * @return true转化成功false 转化失败
        */
        static bool convertFormat(std::string &timeStr,const std::string &oldFormat,const std::string &newFormat=ISO8086A);
        /**
        * @brief 计算两个用字符串表示的时间相减的差值
        * @param time1 被减的时间
        * @param time2 减去的时间
        * @param result 一个接收结果的Duration容器
        * @param format1 time1的时间字符串格式 yyyy年 mm月 dd日 hh时 mi分 ss秒 sss毫秒 （默认格式为'yyyy-mm-ddThh:mi:ss',即ISO08086A标准）
        * @param format2 time2的时间字符串格式 yyyy年 mm月 dd日 hh时 mi分 ss秒 sss毫秒 （默认格式为'yyyy-mm-ddThh:mi:ss',即ISO08086A标准）
        * @return result的引用
        */
        static Duration& calculateTime(const std::string &time1,const std::string &time2,Duration &result,const std::string &format1=ISO8086A,const std::string &format2=ISO8086A);
        /**
        * @brief 一个用字符串表示的时间加上或者减去一段时间
        * @param time1 待运算的时间字符串
        * @param time2 参与运算的用Duration表示的一段时间
        * @param result 接收用字符串表示的运算结果的string容器
        * @param am 填入+：加法运算 填入-：减法运算
        * @param format1 time1的格式 yyyy年 mm月 dd日 hh时 mi分 ss秒 sss毫秒 （默认格式为'yyyy-mm-ddThh:mi:ss',即ISO08086A标准）
        * @param format2 result的格式 yyyy年 mm月 dd日 hh时 mi分 ss秒 sss毫秒 （默认格式为'yyyy-mm-ddThh:mi:ss',即ISO08086A标准）
        * @return result的引用
        */
        static std::string& calculateTime(const std::string &time1,const Duration &time2,std::string &result,const std::string &am,const std::string &format1=ISO8086A,const std::string &format2=ISO8086A);
        /**
        * @brief 比较两个时间字符串表示的时间的大小
        * @note 时间越往后越大
        * @param time1 参与比较的第一个字符串
        * @param time2 参与比较的第二个字符串
        * @param format1 time1的字符串格式 yyyy年 mm月 dd日 hh时 mi分 ss秒 sss毫秒 （默认格式为'yyyy-mm-ddThh:mi:ss',即ISO08086A标准）
        * @param format2 time2的字符串格式 yyyy年 mm月 dd日 hh时 mi分 ss秒 sss毫秒 （默认格式为'yyyy-mm-ddThh:mi:ss',即ISO08086A标准）
        * @return true：time1>=time2  false: time1<time2
        */
        static bool compareTime(const std::string &time1,const std::string &time2,const std::string &format1=ISO8086A,const std::string &format2=ISO8086A);
    private:
        Duration dt{-1,-1,-1,-1,-1};
        bool flag=false;
        std::chrono::steady_clock::time_point start;
        std::chrono::steady_clock::time_point end;
    public:
        /**
        * @brief 开始计时
        * @return ture：开始成功  false：开始失败
        */
        bool startTiming();
        /**
        * @brief 计时过程中检查时间
        * @return 返回一个Duration记录时间到目前流逝长度
        */
        Duration checkTime();
        /**
        * @brief 停止计时
        * @return 返回一个Duration记录时间
        * @note 对象会保存记录上一次计时的时间
        */
        Duration endTiming();
    public:
        /**
        * @brief 获取上一次计时的时间
        * @return 返回一个Duration记录时间
        */
        Duration getDt(){return dt;}
        /**
        * @brief 返回本对象计时状态
        * @return true：对象正在计时  false：对象没有开始计时
        */
        bool isStart(){return flag;}
    };
    }
    namespace file
    {
    /**
    * @brief 日志文件操作类
    * @note 此类的读写日志是线程安全的，因为继承了File类
    * @note 异步日志 会单独开一个线程进行写入操作，可以多个线程同时操作日志
    */
    class LogFile:private time::DateTime,protected File
    {
    private:
        std::string timeFormat;
        std::string contentFormat;
        std::atomic<bool> consumerGuard{true};
        std::atomic<bool> logWakePending{false};
        std::atomic<uint64_t> droppedLogs{0};
        std::mutex queueMutex;
        std::condition_variable queueCV;
        system::MPSCQueue<std::string> logQueue;
        std::thread consumerThread;
    public:
        /**
         * @brief 构造函数，初始化消费者线程
         * @param logQueue_cap 异步日志队列容量（必须为 2 的幂）。
//
// 所有线程产生的日志会先进入该无锁队列，再由独立 logger 线程批量写入文件。
// 日志系统不在主业务热路径上，允许在过载时丢弃，以保护核心服务性能。
//
// 选型原则：
//   logQueue_cap >= 峰值日志速率 × logger 最坏暂停时间
//
// 建议值（经验）：
//   - 默认：8192   (~8k)
//   - 高频日志：16384 (~16k)
//
// 当队列已满时：日志将丢弃，框架不会阻塞调用线程。

//


         */
        LogFile(const size_t &logQueue_cap=8192):logQueue(logQueue_cap)
        {
        consumerGuard=true;
        consumerThread = std::thread([this]()->void
        {
            std::string content;
            content.reserve(1024);
            std::string time;
            time.reserve(128);
            while (true)
            {
                while(this->logQueue.pop(content))//非空则执行
                {       
                    getTime(time,timeFormat);
                    time+=contentFormat;
                    time+=content;
                    this->appendLine(time);
                }
                this->logWakePending.store(false,std::memory_order_release);
                if(this->logQueue.possibly_nonempty())
                {
                    this->logWakePending.store(true,std::memory_order_release);
                    continue;
                }
                if (!this->consumerGuard.load(std::memory_order_acquire))
                    break;
                std::unique_lock<std::mutex> lock(this->queueMutex);
                this->queueCV.wait(lock,[this] {
                    return !this->consumerGuard.load(std::memory_order_acquire)||
                           this->logWakePending.load(std::memory_order_acquire);
                });
            }
        });
        }
        /**
        * @brief 打开一个日志文件
        * @note 不存在则创建（连带目录），默认新建的目录的权限为rwx rwx r-x，默认新建的日志文件权限为rw-，rw-，r--
        * @param fileName 日志文件名（可以用绝对路径也可以用相对路径）
        * @param timeFormat 日志文件中的时间格式 yyyy年 mm月 dd日 hh时 mi分 ss秒 sss毫秒 （默认格式为'yyyy-mm-ddThh:mi:ss',即ISO08086A标准）
        * @param contentFormat 日志文件中时间和记录之间的填充格式（默认为"   " 即四个空格）
        * @return true：打开成功  false：打开失败
        */
        bool openFile(const std::string &fileName,const std::string &timeFormat=ISO8086A,const std::string &contentFormat="   ");
        /**
        * @brief 获取对象是否打开日志文件的状态
        * @return true：打开了  false：没打开
        */
        bool isOpen(){return File::isOpen();}
        /**
        * @brief 获取对象打开的文件名
        * @return 返回对象打开的文件名
        */
        std::string getFileName(){return File::getFileName();}
        /**
        * @brief 关闭对象打开的日志文件
        * @param del true：关闭并且删除日志文件   false：只关闭不删除日志文件 （默认为false 即只关闭不删除日志文件）
        * @return true：关闭成功  false：关闭失败
        */
        bool closeFile(const bool &del=false);
        /**
        * @brief 写一行日志
        * @param data 需要写入的日志内容
        */
        void writeLog(const std::string &data);
        /**
        * @brief 返回日志队列满时累计丢弃的日志条数。
        * @return 从 LogFile 构造以来的累计丢弃数；线程安全且无锁。
        */
        uint64_t getDroppedLogCount() const noexcept{return droppedLogs.load(std::memory_order_relaxed);}
        /**
        * @brief 清空所有日志
        * @return true：写入成功  false：写入失败
        */
        bool clearLog();
        /**
        * @brief 删除指定时间区间内的日志
        * @param date1 时间区间的第一个参数 （默认缺省为无限小）
        * @param date2 时间区间的第二个参数  (默认缺省为无限大)
        * @note 区间为[date1,date2)
        * @return true：删除成功  false：删除失败
        */
        bool deleteLogByTime(const std::string &date1="1",const std::string &date2="2");
        /**
         * @brief 析构函数 写完日志 关闭消费者线程
         */
        ~LogFile();
    };
    }
    /**
    * @namespace stt::data
    * @brief 数据处理
    * @ingroup stt
    */
    namespace data
    {
        /**
        * @brief 负责加密，解密和哈希
        */
        class CryptoUtil
        {
        public:
            /**
            * @brief  AES-256-CBC模式对称加密函数
            * @param before 加密前的数据容器
            * @param length 数据前的数据长度
            * @param passwd 密钥
            * @param iv iv向量
            * @param after 密文的数据容器
            * @return true：加密成功  false：加密失败
            * @note  AES-256-CBC模式下 密钥为32字节 iv向量为16字节
            */
            static bool encryptSymmetric(const unsigned char *before,const size_t &length,const unsigned char *passwd,const unsigned char *iv,unsigned char *after);
            /**
            * @brief AES-256-CBC模式对称解密函数
            * @param before 密文的数据容器
            * @param length 密文的数据长度
            * @param passwd 密钥
            * @param iv iv向量
            * @param after 解密后的数据容器
            * @return true：解密成功  false：解密失败
            * @note  AES-256-CBC模式下 密钥为32字节 iv向量为16字节
            */
            static bool decryptSymmetric(const unsigned char *before,const size_t &length,const unsigned char *passwd,const unsigned char *iv,unsigned char *after);
            /**
            * @brief 计算输入字符串的 SHA-1 哈希值（原始二进制形式）。
            *
            * 使用 OpenSSL 的 SHA1 函数对输入字符串进行哈希处理，结果以 20 字节的原始二进制形式存储在 result 中。
            * 注意：result 可能包含不可打印字符，不适合直接输出或写入文本。
            *
            * @param ori_str 输入的原始字符串。
            * @param result  用于存放输出的 SHA-1 哈希值（二进制形式），长度为 20 字节。
            * @return 返回 result 的引用。
            *
            * @note 该函数适用于后续加密、签名等处理（例如用于 HMAC 的输入）。
            */
            static std::string& sha1(const std::string &ori_str,std::string &result);
            /**
            * @brief 计算输入字符串的 SHA-1 哈希值，并以十六进制字符串形式返回。
            *
            * 使用 OpenSSL 的 SHA1 函数对输入字符串进行哈希处理，结果以 40 个字符的十六进制格式表示存储在 result 中。
            * 每个字节转换为两个十六进制字符，可直接用于打印、日志记录、存储等可读场景。
            *
            * @param ori_str 输入的原始字符串。
            * @param result  用于存放输出的 SHA-1 哈希值（40 字节的十六进制字符串）。
            * @return 返回 result 的引用。
            *
            * @note 适用于哈希显示、唯一标识、日志校验等场景。与 sha1 的主要区别是输出格式。
            */
	        static std::string& sha11(const std::string &ori_str,std::string &result);
        };
        /**
        * @brief 负责二进制数据，字符串之间的转化
        */
        class BitUtil
        {
        public:
            /**
            * @brief 将单个字符转换为其对应的 8 位二进制字符串。
            *
            * @param input 输入的字符。
            * @param result 用于保存输出的二进制字符串（例如 'A' -> "01000001"）。
            * @return 返回 result 的引用。
            */
            static std::string& bitOutput(char input,std::string &result);
            /**
            * @brief 将字符串中的每个字符依次转换为二进制位，并拼接为一个整体字符串。
            *
            * @param input 输入字符串。
            * @param result 保存输出的连续位字符串（长度为 input.size() * 8）。
            * @return 返回 result 的引用。
            */
	        static std::string& bitOutput(const std::string &input,std::string &result);
            /**
            * @brief 获取字符 input 的从左向右第 pos 位（二进制）并返回 '1' 或 '0'。
            *
            * @param input 输入字符。
            * @param pos 位位置（1~8，1 是最高位）。
            * @param result 返回的位字符：'1' 或 '0'。
            * @return 返回 result 的引用。
            */
	        static char& bitOutput_bit(char input,const int pos,char &result);
            /**
            * @brief 将 "01" 字符串（二进制字符串）转换为无符号整数。
            *
            * @param input 输入二进制字符串（如 "1011"）。
            * @param result 输出结果数值。
            * @return 返回 result 的引用。
            */
	        static unsigned long& bitStrToNumber(const std::string &input,unsigned long &result);
            /**
            * @brief 将字符串转换为二进制，再转换为对应数值。
            *
            * @param input 任意原始数据字符串。
            * @param result 返回转换后的数值。
            * @return 返回 result 的引用。
            *
            * @note 实际先调用 bitOutput 得到位串，再通过 bitStrToNumber 转换成整数。
            */
	        static unsigned long& bitToNumber(const std::string &input,unsigned long &result);
            /**
            * @brief 将最多 8 位的 "01" 字符串压缩成 1 个字节（char）。
            *
            * @param input 二进制字符串，最多 8 位。
            * @param result 输出压缩后的字节。
            * @return 返回 result 的引用。
            */
	        static char& toBit(const std::string &input,char &result);
            /**
            * @brief 将任意长度的 "01" 字符串压缩为二进制数据，每 8 位为一个字节。
             *
            * @param input 输入位串（长度应为 8 的倍数）。
            * @param result 返回压缩后的二进制数据（每个字符代表 1 个字节）。
            * @return 返回 result 的引用。
            */
	        static std::string& toBit(const std::string &input,std::string &result);
        };
        /**
        * @brief 随机数，字符串生成相关
        */
        class RandomUtil
        {
        public:
            /**
            * @brief 生成一个随机整数
            * @param a 生成随机数的范围下限
            * @param b 生成随机数的范围上限
            * @note 生成a-b范围的一个随机数
            * @return 返回生成的一个随机数
            */
            static long getRandomNumber(const long &a,const long &b);
            /**
            * @brief 生成一个规定长度的“Base64 字符集内的伪随机字符串”，并在末尾用 '=' 补齐至符合 Base64 字符串格式
            * @param str 保存生成字符串的容器
            * @param length 需要生成的字符串的长度
            * @return 返回str的引用
            */
            static std::string& getRandomStr_base64(std::string &str,const int &length);
            /**
            * @brief 生成一个 32 位（4 字节）的随机掩码。
            *
            * 该函数先随机生成一个由 '0' 和 '1' 组成的 32 位字符串（例如："010110..."），
            * 然后通过内部的 `BitUtil::toBit()` 函数将其转换为对应的 4 字节二进制数据。
            *
            * 转换结果通过 mask 参数返回，通常用于生成数据包掩码、加密掩码、位图掩码等。
            *
            * @param mask 用于存放最终生成的 4 字节掩码（二进制字符串形式）。
            * @return 返回 mask 的引用。
            *
            * @note 内部依赖函数 `BitUtil::toBit(const std::string&, std::string&)`，用于将 32 位二进制字符串压缩为 4 字节。
            */
            static std::string& generateMask_4(std::string &mask);
        };
        /**
        * @brief 负责大小端字节序转换
        */
        class NetworkOrderUtil
        {
        public:
            /**
            * @brief 将 64 位无符号整数的字节序反转（大端 <-> 小端）。
            *
            * 该函数模拟 `htonl`/`ntohl` 的 64 位版本，即按字节交换实现高低位互换。
            * 原地修改参数并返回引用。
            *
            * @param data 输入的 64 位无符号整数，按字节反转后返回。
            * @return 反转后的 data 引用。
            *
            * @note 该实现不依赖平台库函数，适用于不确定机器端序的场景。
            */
            static unsigned long& htonl_ntohl_64(unsigned long &data);//64位无符号数转化为大/小端序（网络字节序）
        };
        
        /**
        * @brief 负责浮点数精度处理
        */
        class PrecisionUtil
        {
        public:
        
            /**
            * @brief 将浮点数格式化为指定小数位数的字符串表示。
            * 
            * @param number 输入的浮点数。
            * @param bit 保留的小数位数。
            * @param str 用于存储格式化后的字符串。
            * @return 格式化结果字符串的引用。
            */
	        static std::string& getPreciesFloat(const float &number,const int &bit,std::string &str);
            /**
            * @brief 将 float 数值保留指定位数的小数，并直接修改原值。
            *
            * @param number 待处理的 float 变量（将被修改）。
            * @param bit 保留的小数位数。
            * @return 修改后的 float 引用。
            */
            static float& getPreciesFloat(float &number,const int &bit);
            /**
            * @brief 将双精度浮点数格式化为指定小数位数的字符串表示。
            *
            * @param number 输入的 double 数值。
            * @param bit 保留的小数位数。
            * @param str 用于存储格式化后的字符串。
            * @return 格式化结果字符串的引用。
            */
            static std::string& getPreciesDouble(const double &number,const int &bit,std::string &str);
            /**
            * @brief 将 double 数值保留指定位数的小数，并直接修改原值。
            *
            * @param number 待处理的 double 变量（将被修改）。
            * @param bit 保留的小数位数。
            * @return 修改后的 double 引用。
            */
            static double& getPreciesDouble(double &number,const int &bit);
            /**
            * @brief 根据数值动态调整小数精度，保留指定数量的有效数字。
            *
            * 对于小于 1 的小数，会先确定其最前位有效数字所处的位置，
            * 然后在此基础上保留 bit 位有效数字。
            * 修改后的数值将四舍五入保留合适的小数位，并写回原变量。
            *
            * @param number 待处理的 float 数值（将被修改）。
            * @param bit 希望保留的有效数字位数。
            * @return 修改后的 float 引用。
            */
            static float& getValidFloat(float &number,const int &bit);
        };
        /**
        * @brief 负责Http字符串和URL解析
        * 包括从 URL 或请求报文中提取参数、IP、端口、请求头字段等功能。
        */
        class HttpStringUtil
        {
        public:
            /**
            * @brief 从原始字符串中提取两个标记之间的子串。
            *
            * 提取从 a 到 b 之间的内容（不包含 a 和 b），可指定起始搜索位置。
            * 若 a 或 b 为空字符串，则分别表示从头或到尾。
            * 若a找不到，则默认从头开始
            * 若b找不到，则默认到尾
            *
            * @param ori_str 原始字符串。
            * @param str 存储提取结果的字符串。
            * @param a 起始标记字符串。
            * @param b 终止标记字符串。
            * @param pos 搜索起始位置。
            * @return 返回b在ori_str中的位置(可能返回string::npos,如果b找不到或者b为"")
            * @note 如果找不到，结果字符串为""
            */       
	        static size_t get_split_str(const std::string_view& ori_str,std::string_view &str,const std::string_view &a,const std::string_view &b,const size_t &pos=0);
            /**
            * @brief 从 URL 查询参数中提取指定 key 的值。
            *
            * @note url不需要完整也行 例如从 `?id=123&name=abc`提取 `id` 的值和从`http://xxxx/?id=123&name=abc`提取 `id` 的值是一样的。
            *
            * @param ori_str 原始 URL 字符串。
            * @param str 存储提取结果的字符串。
            * @param name 参数名（key）。
            * @return 引用，指向结果字符串。
            */
	        static std::string_view& get_value_str(const std::string_view& ori_str,std::string_view &str,const std::string& name);
            /**
            * @brief 从 HTTP 请求头中提取指定字段的值。
            *
            * @param ori_str 原始 HTTP 请求头字符串。
            * @param str 提取结果。
            * @param name 请求头字段名（如 "Host"）。
            * @return 引用，指向结果字符串。
            */
	        static std::string_view& get_value_header(const std::string_view& ori_str,std::string_view &str,const std::string& name);
            /**
            * @brief 提取 URL 中 path 和 query 部分。
            *
            * 例如从 `http://abc.com/path?query=123` 或者从/path?query=123 提取 `/path`。
            *
            * @param ori_str 原始 URL。
            * @param str 返回 path 部分。
            * @return 引用，指向结果字符串。
            */
	        static std::string_view& get_location_str(const std::string_view& ori_str,std::string_view &str);
            /**
            * @brief 提取 URL 的 path 部分（不含 query）。
            *
            * 与 `get_location_str` 类似，但保留 path 之后的所有内容（如参数）。
            *
            * @param url URL。
            * @param locPara 返回 path+参数部分。
            * @return 引用，指向结果字符串。
            */
            static std::string_view& getLocPara(const std::string_view &url,std::string_view &locPara);
            /**
            * @brief 获取 URL 中的查询参数字符串（包括 ?）。
            * @note 可以是完整url 也可以是不太完整的，比如/path?id=123&name=abc
            *
            * @param url URL。
            * @param para 返回参数部分（形如 "?id=123&name=abc"）。
            * @return 引用，指向结果字符串。
            */
            static std::string_view& getPara(const std::string_view &url,std::string_view &para);


            /**
            * @brief 从原始字符串中提取两个标记之间的子串。
            *
            * 提取从 a 到 b 之间的内容（不包含 a 和 b），可指定起始搜索位置。
            * 若 a 或 b 为空字符串，则分别表示从头或到尾。
            * 若a找不到，则默认从头开始
            * 若b找不到，则默认到尾
            *
            * @param ori_str 原始字符串。
            * @param str 存储提取结果的字符串。
            * @param a 起始标记字符串。
            * @param b 终止标记字符串。
            * @param pos 搜索起始位置。
            * @return 返回b在ori_str中的位置(可能返回string::npos,如果b找不到或者b为"")
            * @note 如果找不到，结果字符串为""
            */       
	        static size_t get_split_str(const std::string_view& ori_str,std::string &str,const std::string_view &a,const std::string_view &b,const size_t &pos=0);
            /**
            * @brief 从 URL 查询参数中提取指定 key 的值。
            *
            * @note url不需要完整也行 例如从 `?id=123&name=abc`提取 `id` 的值和从`http://xxxx/?id=123&name=abc`提取 `id` 的值是一样的。
            *
            * @param ori_str 原始 URL 字符串。
            * @param str 存储提取结果的字符串。
            * @param name 参数名（key）。
            * @return 引用，指向结果字符串。
            */
	        static std::string& get_value_str(const std::string& ori_str,std::string &str,const std::string& name);
            /**
            * @brief 从 HTTP 请求头中提取指定字段的值。
            *
            * @param ori_str 原始 HTTP 请求头字符串。
            * @param str 提取结果。
            * @param name 请求头字段名（如 "Host"）。
            * @return 引用，指向结果字符串。
            */
	        static std::string& get_value_header(const std::string& ori_str,std::string &str,const std::string& name);
            /**
            * @brief 提取 URL 中 path 和 query 部分。
            *
            * 例如从 `http://abc.com/path?query=123` 或者从/path?query=123 提取 `/path`。
            *
            * @param ori_str 原始 URL。
            * @param str 返回 path 部分。
            * @return 引用，指向结果字符串。
            */
	        static std::string& get_location_str(const std::string& ori_str,std::string &str);
            /**
            * @brief 提取 URL 的 path 部分（不含 query）。
            *
            * 与 `get_location_str` 类似，但保留 path 之后的所有内容（如参数）。
            *
            * @param url URL。
            * @param locPara 返回 path+参数部分。
            * @return 引用，指向结果字符串。
            */
            static std::string& getLocPara(const std::string &url,std::string &locPara);
            /**
            * @brief 获取 URL 中的查询参数字符串（包括 ?）。
            * @note 可以是完整url 也可以是不太完整的，比如/path?id=123&name=abc
            *
            * @param url URL。
            * @param para 返回参数部分（形如 "?id=123&name=abc"）。
            * @return 引用，指向结果字符串。
            */
            static std::string& getPara(const std::string &url,std::string &para);
            /**
            * @brief 从 URL 中提取主机 IP 或域名。
            *
            * 例如从 `http://127.0.0.1:8080/` 提取 `127.0.0.1`。
            *
            * @warning url必须完整而且显式指明端口
            * @param url 完整 URL。
            * @param IP 存储提取的 IP 或域名。
            * @return 引用，指向结果字符串。
            */
            static std::string& getIP(const std::string &url,std::string &IP);
            /**
             * @brief 从 URL 中提取端口号。
            *
             * 例如从 `http://127.0.0.1:8080/` 中提取 8080。
            *
            * @warning 必须是完整的url，而且必须显式指定端口 还有路径 就算路径没有 也要写个/
            * @param url 完整 URL。
            * @param port 存储解析出的端口号。
            * @return 引用，指向端口号。
            */
            static int& getPort(const std::string &url,int &port);
            /**
            * @brief 创建一个 HTTP 请求头字段字符串。
            *
            * 该函数构造格式为 `字段名: 字段值\r\n` 的字符串。
            *
            * @param first 第一个字段名。
            * @param second 第一个字段值。
            * @return 构造的单条 HTTP 请求头字符串。
            */
            static std::string createHeader(const std::string& first,const std::string& second);
            /**
            * @brief 递归构造多个 HTTP 请求头字段。
            *
            * 支持多个字段名和值的构造，用法为：
            * @code
            * std::string headers = createHeader("Host", "example.com", "Connection", "keep-alive,"Content-Type","charset=UTF-8");
            * @endcode
            * 最终生成：
            * @code
            * Host: example.com\r\n
            * Connection: keep-alive\r\n
            * Content-Type: charset=UTF-8\r\n
            * @endcode
            *
            * @tparam Args 其余参数类型，需以 (字段名, 字段值) 成对传入。
            * @param first 当前字段名。
            * @param second 当前字段值。
            * @param args 后续的字段名和值（必须为偶数个）。
            * @return 构造的完整 HTTP 请求头字符串。
            */
	        template<class... Args>
	        static std::string createHeader(const std::string& first,const std::string& second,Args... args)
	        {
		        std::string cf=first+": "+second+"\r\n"+createHeader(args...);
		        return cf;
	        }
        };
        /**
        * @brief 负责websocket协议有关字符串的操作
        */
        class WebsocketStringUtil
        {
        public:
            /**
            * @brief 生成 WebSocket 握手响应中的 Sec-WebSocket-Accept 字段值。
            *
            * 该函数基于客户端提供的 Sec-WebSocket-Key，拼接 WebSocket 指定的魔法 GUID，
            * 再进行 SHA-1 哈希与 Base64 编码，得到握手响应所需的 accept 字符串。
            *
            * @param str 输入为客户端提供的 Sec-WebSocket-Key，会被原地修改为结果字符串（Sec-WebSocket-Accept）。
            * @return 引用返回修改后的 str。
            *
            * @note 实现参考 RFC 6455 第 4.2.2 节握手过程。
            */
            static std::string& transfer_websocket_key(std::string &str);
        };
        /**
        * @brief 负责字符串和数字的转化
        */
        class NumberStringConvertUtil
        {
        public:
            /**
            * @brief string转化为int类型
            * @note 不会抛出异常
            * @param ori_str 原始string类型数据
            * @param result 存放结果的int容器
            * @param i 如果转化失败，则把 result 赋值为 i（默认为 -1）。
            * @return result的引用
            */
	        static int& toInt(const std::string_view&ori_str,int &result,const int &i=-1);
            /**
            * @brief 16进制数字的字符串表示转化为10进制int类型数字
            * @param ori_str 16进制数字的字符串表示
            * @param result 保存转化结果的int容器
            * @param i 转换失败时写入 result 的默认值（默认为 -1）。
            * @return 返回result的引用
            */
            static int& str16toInt(const std::string_view&ori_str,int &result,const int &i=-1);
            /**
            * @brief string转化为long类型
            * @note 不会抛出异常
            * @param ori_str 原始string类型数据
            * @param result 存放结果的long容器
            * @param i 如果转化失败 则把result赋值为i(默认为-1)
            * @return result的引用
            */
	        static long& toLong(const std::string_view&ori_str,long &result,const long &i=-1);
            /**
            * @brief string转化为float类型
            * @note 不会抛出异常
            * @param ori_str 原始string类型数据
            * @param result 存放结果的float容器
            * @param i 如果转化失败 则把result赋值为i(默认为-1)
            * @return result的引用
            */
	        static float& toFloat(const std::string&ori_str,float &result,const float &i=-1);
            /**
            * @brief string转化为double类型
            * @note 不会抛出异常
            * @param ori_str 原始string类型数据
            * @param result 存放结果的double容器
            * @param i 如果转化失败 则把result赋值为i(默认为-1)
            * @return result的引用
            */
	        static double& toDouble(const std::string&ori_str,double &result,const double &i=-1);
            /**
            * @brief string转化为bool类型
            * @note 不会抛出异常，true或者True，TRUE返回true，否则返回false
            * @param ori_str 原始string类型数据
            * @param result 存放结果的bool容器
            * @return result的引用
            */
	        static bool& toBool(const std::string_view&ori_str,bool &result);
            /**
            * @brief 将普通字符串转化为对应的十六进制表示字符串（hex string）。
             *
            * 例如输入 "ABC" 会被转化为 "414243"，每个字符转换为其 ASCII 的十六进制形式。
            *
            * @param ori_str 输入的原始字符串（可以包含任意字符，包括不可见字符）。
            * @param result 存放转换后结果的字符串引用。
            * @return 转换后的十六进制字符串 result 的引用。
            * @bug 有bug待修复
            */
            static std::string& strto16(const std::string &ori_str,std::string &result);//字符串转化为16进制字符串     (暂不需要)(待修复)
        };
        /**
        * @brief 数据编码解码，掩码处理等
        */
        class EncodingUtil
        {
        public:
            /**
            * @brief 对字符串进行 Base64 编码。
            *
            * 使用 OpenSSL 的 BIO 接口对给定字符串进行 Base64 编码，编码过程中不会插入换行符。
            *
            * @param input 要编码的原始字符串（可以包含任意二进制数据）。
            * @return 编码后的 Base64 字符串。
            */
            static std::string base64_encode(const std::string &input);
            /**
            * @brief 对 Base64 编码的字符串进行解码。
            *
            * 使用 OpenSSL 的 BIO 接口对 Base64 字符串进行解码。该函数不接受带换行符的 Base64 字符串。
            *
            * @param input Base64 编码的字符串。
            * @return 解码后的原始字符串。
            */
	        static std::string base64_decode(const std::string &input);
            /**
            * @brief 生成 WebSocket 握手响应中的 Sec-WebSocket-Accept 字段值。
            *
            * 该函数基于客户端提供的 Sec-WebSocket-Key，拼接 WebSocket 指定的魔法 GUID，
            * 再进行 SHA-1 哈希与 Base64 编码，得到握手响应所需的 accept 字符串。
            *
            * @param str 输入为客户端提供的 Sec-WebSocket-Key，会被原地修改为结果字符串（Sec-WebSocket-Accept）。
            * @return 引用返回修改后的 str。
            *
            * @note 实现参考 RFC 6455 第 4.2.2 节握手过程。
            */
	        static std::string& transfer_websocket_key(std::string &str);
            /**
            * @brief 生成一个 32 位（4 字节）的随机掩码。
            *
            * 该函数先随机生成一个由 '0' 和 '1' 组成的 32 位字符串（例如："010110..."），
            * 然后通过内部的 `BitUtil::toBit()` 函数将其转换为对应的 4 字节二进制数据。
            *
            * 转换结果通过 mask 参数返回，通常用于生成数据包掩码、加密掩码、位图掩码等。
            *
            * @param mask 用于存放最终生成的 4 字节掩码（二进制字符串形式）。
            * @return 返回 mask 的引用。
            *
            * @note 内部依赖函数 `BitUtil::toBit(const std::string&, std::string&)`，用于将 32 位二进制字符串压缩为 4 字节。
            */
            static std::string& generateMask_4(std::string &mask);
            /**
            * @brief 使用给定的 4 字节掩码对字符串进行异或操作（XOR Masking）。
            *
            * 此函数对输入字符串 data 的每个字节，按顺序与 mask 中的 4 字节循环异或。
            * 该操作是可逆的，可用于加密或解密 WebSocket 中的掩码数据帧。
            *
            * @param data 要进行异或处理的数据字符串，处理结果会直接修改该字符串。
            * @param mask 用作掩码的字符串，通常应为至少 4 字节。
            * @return 引用，指向处理后的 data 字符串。
            */
	        static std::string& maskCalculate(std::string &data,const std::string &mask);
        };
        /**
        * @brief json数据操作类
        */
        class JsonHelper
        {
            public:
            /**
            * @brief 提取 JSON 字符串中指定字段的值或嵌套结构。
            * 
            * @param oriStr 原始 JSON 字符串。
            * @param result 存储返回值的字符串引用。
            * @param type 类型模式，可选值：value, arrayvalue。分别为普通json对象里面的值，数组对象里面的值。
            * @param name JSON 中的键名（仅适用于 value）。
            * @param num 数组索引位置(从0开始)（仅适用于 arrayvalue）。
            * @return -1:提取失败
            *          0:返回非json对象
            *          1:返回json对象
            */
	        static int getValue(const std::string &oriStr,std::string& result,const std::string &type="value",const std::string &name="a",const int &num=0);

            /**
            * @brief 将 Json::Value 序列化为紧凑 JSON 文本。
            * @param val JSON 值。
            * @return 合法 JSON 文本；字符串值也会包含 JSON 引号。
            */
            static std::string toString(const Json::Value &val);
            /**
            * @brief 解析 JSON 字符串为 Json::Value。
            * @param str 输入的 JSON 字符串。
            * @return Json::Value 解析后的 JSON 对象或数组。
            */
	        static Json::Value toJsonArray(const std::string & str);
            /**
            * @brief 创建仅包含一个键值对的 JSON 字符串。
            * 
            * @tparam T1 键的类型，通常为 std::string。
            * @tparam T2 值的类型，可为任意可被 Json::Value 接收的类型。
            * @param first 键。
            * @param second 值。
            * @return std::string JSON 字符串。
            */
            template<class T1,class T2>
	        static std::string createJson(T1 first,T2 second)
	        {
		        Json::Value root;
		        //root[first]=second;
                if constexpr (std::is_integral_v<T2>) {
                    root[first] = Json::Value(static_cast<Json::Int64>(second));
                } else {
                    root[first] = second;
                }
		        Json::StreamWriterBuilder builder;
		        std::string jsonString=Json::writeString(builder,root);
		        return jsonString;
	        }
            /**
            * @brief 创建多个键值对组成的 JSON 字符串（递归变参模板）。
            * 
            * @tparam T1 第一个键的类型。
            * @tparam T2 第一个值的类型。
            * @tparam Args 其余成对出现的键值参数类型。
            * @param first 第一个键。
            * @param second 第一个值。
            * @param args 其余键值参数。
            * @return std::string 拼接完成的 JSON 字符串。
            */
             template<class T1,class T2,class... Args>
	        static std::string createJson(T1 first,T2 second,Args... args)
	        {
		        Json::Value root;
		        //root[first]=second;
                if constexpr (std::is_integral_v<T2>) {
                    root[first] = Json::Value(static_cast<Json::Int64>(second));
                } else {
                    root[first] = second;
                }
		        std::string kk=createJson(args...);
		        Json::StreamWriterBuilder builder;
		        std::string jsonString=Json::writeString(builder,root);
		        jsonString=jsonString.erase(jsonString.length()-2);
		        kk=kk.substr(1);
		        return jsonString+","+kk;

	        }
            /**
            * @brief 创建只包含一个元素的 JSON 数组字符串。
            * 
            * @tparam T 任意类型。
            * @param first 第一个元素。
            * @return std::string JSON 数组字符串。
            */
	        template<class T>
	        static std::string createArray(T first)
	        {
		        Json::Value root(Json::arrayValue);
		        root.append(first);
		        Json::StreamWriterBuilder builder;
		        std::string jsonString=Json::writeString(builder,root);
		        return jsonString;
	        }
            /**
            * @brief 创建多个元素组成的 JSON 数组字符串（递归变参模板）。
            * 
            * @tparam T 第一个元素类型。
            * @tparam Args 其余元素类型。
            * @param first 第一个元素。
            * @param args 其余元素。
            * @return std::string 拼接完成的 JSON 数组字符串。
            */
            template<class T,class... Args>
	        static std::string createArray(T first,Args... args)
	        {
		        Json::Value root(Json::arrayValue);
		        root.append(first);
		        std::string kk=createArray(args...);
		        Json::StreamWriterBuilder builder;
		        std::string jsonString=Json::writeString(builder,root);
		        jsonString=jsonString.erase(jsonString.length()-2);
		        kk=kk.substr(1);
		        return jsonString+","+kk;

	        }
            /**
            * @brief 将两个 JSON 字符串拼接为一个有效的 JSON（适用于对象或数组拼接）。
            * @param a 第一个 JSON 字符串。
            * @param b 第二个 JSON 字符串。
            * @return std::string 拼接后的 JSON 字符串。
            */
            static std::string jsonAdd(const std::string &a,const std::string &b);
             /**
            * @brief 将格式化后的 JSON 字符串去除缩进、空格等变成紧凑格式。
            * @param a 输入的 JSON 字符串。
            * @param b 存储格式化结果的引用。
            * @return std::string& 紧凑格式的字符串引用。
            */
            static std::string& jsonFormatify(const std::string &a,std::string &b);
            /**
            * @brief 将 JSON 字符串中的 Unicode 转义序列转换为 UTF-8 字符。
            * @param input 含有 Unicode 编码的 JSON 字符串。
            * @param output 存储转换后字符串的引用。
            * @return std::string& 返回转换后的字符串引用。
            */
            static std::string& jsonToUTF8(const std::string &input,std::string &output);
        };
    }

    /**
    * @brief 涉及信息安全的api
    */
    namespace security
    {
    /**
     * @enum RateLimitType
     * @brief 限流算法类型（策略）。
     *
     * 每种策略的“语义”不同，选择时请按业务/攻击模型匹配：
     *
     * @par 1) Cooldown（连续触发惩罚 / 冷却限流）
     * - @b 规则：在 secs 时间内累计达到 times 次后，后续请求全部拒绝，直到“安静”满 secs 后才恢复。
     * - @b 特性：对持续刷/爆破非常狠；对正常用户的“短时间突发”不友好（可能需要完全停一段时间才能恢复）。
     * - @b 适用：
     *   - 建连防爆破（connect）
     *   - 登录失败次数惩罚（fail-based 可用同思路扩展）
     *   - 防脚本快速重试
     * - @b 不适用：
     *   - 正常会出现突发但希望“自然恢复”的接口（例如页面加载产生的突发 HTTP 请求）
     *
     * @par 2) FixedWindow（固定窗口计数）
     * - @b 规则：以某个窗口起点为基准，每 secs 秒为一个窗口；窗口内最多 times 次，超出拒绝；窗口结束时计数清零。
     * - @b 特性：实现简单、开销小；存在“窗口边界突刺”（在窗口交界处可能短时间放行较多）。
     * - @b 适用：
     *   - 低风险、要求简单的限流
     *   - 统计口径清晰的“每 N 秒最多 M 次”
     * - @b 不适用：
     *   - 对抗性强的场景（容易被边界行为利用）
     *
     * @par 3) SlidingWindow（滑动窗口 / 时间戳队列）
     * - @b 规则：在任意时刻 now，统计 (now-secs, now] 区间内发生的次数；若 >= times 则拒绝。
     * - @b 特性：比 FixedWindow 更公平、无窗口边界突刺；需要维护时间戳队列（开销略高）。
     * - @b 适用：
     *   - 对外 HTTP API、用户操作等希望公平且稳定的限流
     *   - path 级别的关键接口（如 /login /register）
     * - @b 不适用：
     *   - 极高 QPS 且每个 key(times) 很大、又不愿意付出队列内存/维护成本的场景（可用近似桶）
     *
     * @par 4) TokenBucket（令牌桶）
     * - @b 规则（常见语义）：令牌按固定速率补充；每次请求消耗 1 令牌；无令牌则拒绝。
     *   在本类接口里通过 (times, secs) 表达：平均速率约为 times/secs，桶容量通常与 times 同级（实现可按需要定义）。
     * - @b 特性：允许突发（桶里有存量时可瞬间放行一批），同时限制长期平均速率；工程上最常用。
     * - @b 适用：
     *   - 请求流量整形（允许短突发但控制长期平均）
     *   - IM 消息、HTTP 请求等“既不想太硬也不想太松”的场景
     * - @b 不适用：
     *   - 你想要“打满后必须停一段时间”的强惩罚需求（那是 Cooldown）
     */
        enum class RateLimitType 
        {
            Cooldown,      // 连续触发惩罚：达到上限后进入冷却，需要安静 secs 才恢复
            FixedWindow,   // 固定窗口计数：每 secs 一个窗口，窗口内最多 times 次
            SlidingWindow, // 滑动窗口：统计最近 secs 内的次数（队列时间戳）
            TokenBucket    // 令牌桶：允许突发 + 控制长期平均速率（常用于 API/消息）
        };
    /**
 * @struct RateState
 * @brief 单一限流器的运行状态（可复用于多种限流策略）。
 *
 * @details
 * 该结构用于记录某个限流维度（IP / fd / path 等）的实时状态，
 * 会被不同限流算法复用：
 *
 * - Cooldown / FixedWindow：
 *   - 使用 counter + lastTime
 * - SlidingWindow：
 *   - 使用 history（时间戳队列）
 * - TokenBucket：
 *   - 使用 tokens + lastRefill
 *
 * @note
 * - violations 用于记录“被拒绝”的次数，是防御升级（DROP / CLOSE / 封禁）的依据
 * - RateState 本身不做任何决策，只负责记录状态
 */
        struct RateState
        {
            // 通用
            int counter = 0;
            int violations = 0;   // 新增：触发限流的次数
            std::chrono::steady_clock::time_point lastTime{};

            // SlidingWindow
            std::deque<std::chrono::steady_clock::time_point> history;

            // TokenBucket
            double tokens = 0.0;
            std::chrono::steady_clock::time_point lastRefill{};
        };
        /**
 * @struct ConnectionState
 * @brief 单个连接（fd）的安全与限流状态。
 *
 * @details
 * 每个成功通过 allowConnect 的 fd 都会拥有一个 ConnectionState，
 * 用于实现“连接级（fd 级）”防御：
 *
 * - requestRate：该连接的整体请求速率限制
 * - pathRate：该连接在不同 path 下的额外限流状态
 * - lastActivity：该连接最后一次活动时间（用于僵尸连接检测）
 *
 * @note
 * - 一个 IP 可以对应多个 ConnectionState
 * - 断连时必须清理对应的 ConnectionState
 */
        struct ConnectionState
        {
            int fd = -1;
            RateState requestRate;
            std::unordered_map<std::string, RateState> pathRate;
            std::chrono::steady_clock::time_point lastActivity{};
        };
        /**
 * @struct IPInformation
 * @brief 单个 IP 的安全状态与连接集合。
 *
 * @details
 * IPInformation 是 ConnectionLimiter 的核心状态单元，
 * 用于实现“IP 级 + fd 级”的分层防御模型：
 *
 * - activeConnections：当前 IP 已建立的并发连接数
 * - connectRate：IP 级建连速率限制状态
 * - badScore：IP 风险评分（多次恶意行为会累积）
 * - conns：该 IP 下所有活动连接（fd -> ConnectionState）
 *
 * @par 防御语义
 * - activeConnections / connectRate：
 *   - 用于防止建连洪水、爆破
 * - badScore：
 *   - 用于升级防御策略（断连 / 临时封禁 IP）
 *
 * @note
 * - IP 的黑名单（封禁时间）通常由 ConnectionLimiter 统一维护
 * - badScore 可随时间衰减或在封禁后重置
 */
        struct IPInformation
        {
            int activeConnections = 0;
            RateState connectRate;
            int badScore = 0; //IP 风险评分（用于升级惩罚）
            std::unordered_map<int, ConnectionState> conns; // fd -> state
        };
        /**
 * @enum DefenseDecision
 * @brief 安全裁决结果（由 ConnectionLimiter 返回）。
 *
 * @details
 * 所有连接 / 请求在进入业务处理前，都会经过 ConnectionLimiter 的判定，
 * 并返回以下三种裁决之一：
 *
 * - ALLOW (0)：
 *   - 正常通过，继续处理
 * - DROP (1)：
 *   - 无视该请求（不回应、不处理）
 *   - 主要用于 request 阶段的轻度防御
 * - CLOSE (2)：
 *   - 立即断开连接
 *   - 可伴随 IP 风险升级或临时封禁
 *
 * @note
 * - connect 阶段通常只使用 ALLOW / CLOSE
 * - DROP 主要用于 request 阶段（fd 已存在时）
 */
        enum DefenseDecision : int
        {
            ALLOW = 0,  // 正常通过
            DROP  = 1,  // 不回应（丢弃）
            CLOSE = 2   // 断连（可伴随封 IP）
        };
    
        /**
 * @class ConnectionLimiter
 * @brief 统一的连接与请求安全裁决器（IP 级 + fd 级，多策略限流 + 黑名单）。
 *
 * @details
 * ConnectionLimiter 是一个“安全门（Security Gate）”，
 * 所有连接建立与请求处理在进入业务逻辑前，都必须经过该类的裁决。
 *
 * 本类不直接执行业务行为（如 close / send / sleep），
 * 而是返回一个 @ref DefenseDecision 结果，由外层统一执行。
 *
 * ---
 * @section design_overview 设计概览
 *
 * 本类实现的是一个分层防御模型：
 *
 * - IP 级防御：
 *   - 并发连接数限制（maxConnections）
 *   - 建连速率限制（connectRate）
 *   - IP 风险评分（badScore）
 *   - 临时黑名单（blacklist，带 TTL）
 *
 * - fd 级防御：
 *   - 请求速率限制（requestRate）
 *   - path 级额外限流（pathRate）
 *   - 连接活动时间记录（lastActivity）
 *
 * ---
 * @section defense_decision 防御裁决语义
 *
 * allowConnect / allowRequest 的返回值为 @ref DefenseDecision "DefenseDecision"：
 *
 * - ALLOW (0)：
 *   - 允许继续处理
 *
 * - DROP (1)：
 *   - 无视本次请求（不回应、不处理）
 *   - 主要用于 request 阶段的轻度防御
 *
 * - CLOSE (2)：
 *   - 立即断开连接
 *   - 可伴随 IP 风险升级或临时封禁
 *
 * @note
 * - connect 阶段由于 TCP 连接已建立，通常只使用 ALLOW / CLOSE
 * - DROP 主要用于 request 阶段（fd 已存在时）
 *
 * ---
 * @section rate_limit 策略与限流
 *
 * 本类支持多种限流策略（见 @ref RateLimitType "RateLimitType"）：
 *
 * - Cooldown
 * - FixedWindow
 * - SlidingWindow
 * - TokenBucket
 *
 * 不同维度使用不同策略：
 *
 * - 建连速率：connectStrategy（默认 Cooldown）
 * - fd 级请求速率：requestStrategy（默认 SlidingWindow）
 * - path 级请求速率：pathStrategy（默认 SlidingWindow）
 *
 * ---
 * @section thread_safety 线程安全
 *
 * @warning
 * 本类本身不包含锁：
 * - table / pathConfig / blacklist 的并发安全
 *   需由上层保证（如事件循环线程、外部互斥锁等）。
 *
 * ---
 * @section lifecycle 生命周期说明
 *
 * - allowConnect：
 *   - 判断 IP 是否允许进入应用层
 *   - 在 ALLOW 时登记 fd
 *
 * - allowRequest：
 *   - 对已登记 fd 进行请求裁决
 *
 * - clearIP：
 *   - 在连接关闭时调用，回收 fd 与并发计数
 *
 * - connectionDetect：
 *   - 用于检测并清理长时间无活动的僵尸连接
 */
        class ConnectionLimiter
        {
        public:
        /**
         * @brief 构造函数。
         *
         * @param maxConn 同一 IP 允许的最大并发连接数（activeConnections 上限）。
         * @param idleTimeout 连接僵尸检测超时时间（秒）。若 < 0 表示不做僵尸检测。
         */
            ConnectionLimiter(const int& maxConn = 20, const int& idleTimeout = 60) : maxConnections(maxConn),connectionTimeout(idleTimeout){}

            // ========== 策略设置 ==========
        /**
         * @brief 设置“连接速率限流”所使用的策略。
         *
         * @param type 策略类型，见 @ref RateLimitType 说明。
         * @note 不调用本函数时，默认策略为 @ref RateLimitType::Cooldown 。
         */
            void setConnectStrategy(const RateLimitType &type);
        /**
         * @brief 设置“IP 级请求限流”所使用的策略。
         *
         * @param type 策略类型，见 @ref RateLimitType 说明。
         * @note 不调用本函数时，默认策略为 @ref RateLimitType::SlidingWindow 。
         */
            void setRequestStrategy(const RateLimitType &type);
        /**
         * @brief 设置“path 级请求限流”所使用的策略。
         *
         * @param type 策略类型，见 @ref RateLimitType 说明。
         * @note 不调用本函数时，默认策略为 @ref RateLimitType::SlidingWindow 。
         */
            void setPathStrategy(const RateLimitType &type);

            // ========== Path 配置 ==========
        /**
         * @brief 设置某个路径的额外限流规则（path 级）。
         *
         * @param path 需要额外限流的路径，例如 "/login"、"/register"。
         * @param times 在 secs 秒内允许的最大请求次数。
         * @param secs 统计窗口长度（秒）。
         *
         * @note setPathLimit 配置的是 @b 额外规则：
         * - allowRequest 调用时传入的 (times, secs) 仍然会作为 IP 级规则先执行；
         * - 若 path 命中此配置，则再执行 path 级规则；
         * - 两者为 AND 关系：任何一层失败即拒绝。
         */
            void setPathLimit(const std::string &path, const int &times, const int &secs);

            // ========== 核心判断 ==========
        /**
     * @brief 对新建立的连接进行安全裁决（IP 级）。
     *
     * @param ip 对端 IP 地址。
     * @param fd 新 accept 得到的文件描述符。
     * @param times 在 secs 秒内允许的最大建连次数。
     * @param secs 建连速率统计窗口（秒）。
     *
     * @return DefenseDecision
     * - ALLOW：允许该连接进入应用层（fd 将被登记）
     * - CLOSE：拒绝并应立即断开连接
     *
     * @note
     * - connect 阶段通常不使用 DROP
     * - 若命中黑名单或高风险状态，将直接返回 CLOSE
     */
            DefenseDecision allowConnect(const std::string &ip, const int &fd,const int &times, const int &secs);
        /**
     * @brief 对已建立连接的一次请求进行安全裁决。
     *
     * @param ip 对端 IP 地址。
     * @param fd 当前请求对应的文件描述符。
     * @param path 请求路径（用于 path 级限流）。
     * @param times fd 级请求速率上限。
     * @param secs 请求速率统计窗口（秒）。
     *
     * @return DefenseDecision
     * - ALLOW：正常处理请求
     * - DROP：无视本次请求（不回应）
     * - CLOSE：断开连接
     */
            DefenseDecision allowRequest(const std::string &ip,const int &fd,const std::string_view &path,const int &times,const int &secs);

            // ========== 生命周期 ==========
        /**
     * @brief 在连接断开时回收对应 fd 的状态。
     *
     * @param ip 对端 IP 地址。
     * @param fd 已关闭的文件描述符。
     *
     * @note
     * - 必须在 close(fd) 后调用
     * - 用于维护 activeConnections 与内部状态一致性
     */
            void clearIP(const std::string &ip,const int &fd);
        /**
     * @brief 检测并清理僵尸连接（fd 级）。
     *
     * @param ip 对端 IP 地址。
     * @param fd 待检测的文件描述符。
     *
     * @return true  该连接被判定为僵尸并已清理
     * @return false 未超时或不存在
     *
     * @note
     * - “活动”指 allowConnect / allowRequest 更新 lastActivity
     * - 建议由外部定时器触发调用，避免热路径扫描
     */
            bool connectionDetect(const std::string &ip,const int &fd);
        /**
 * @brief 立即将指定 IP 加入黑名单（直接封禁）。
 *
 * @details
 * 该接口用于在检测到“明确恶意行为”时，绕过评分与渐进惩罚，
 * 直接对 IP 进行封禁（写入黑名单）。
 *
 * 封禁语义说明：
 * - 若 IP 当前不在黑名单中：直接加入；
 * - 若 IP 已在黑名单中：@b 刷新（覆盖）封禁到期时间；
 * - 若 banSeconds < 0：表示永久封禁（使用 time_point::max）。
 *
 * @param ip 需要封禁的 IP 地址。
 * @param banSeconds 封禁时长（秒）：
 *   - > 0 ：封禁 banSeconds 秒（短期封禁）
 *   - = 0 ：不做任何操作
 *   - < 0 ：永久封禁
 * @param reasonCN 封禁原因（中文，用于日志）。
 * @param reasonEN 封禁原因（英文，用于日志）。
 *
 * @note
 * - 本函数 @b 不会 立即断开已有连接；
 *   外层逻辑应在返回 CLOSE 后自行 close(fd)。
 * - 使用 steady_clock，不受系统时间调整影响。
 * - 该接口是安全裁决的“终态动作”，应谨慎调用。
  * @note
 * - 若 IP 已被封禁且原到期时间晚于本次封禁时间，
 *   将保留更长的封禁（不会缩短）。

 */
        void banIP(const std::string &ip,int banSeconds,const std::string &reasonCN,const std::string &reasonEN);
        /**
 * @brief 手动解除某个 IP 的黑名单。
 */
    void unbanIP(const std::string &ip);
    /**
    * @brief 判断某ip是否被封禁
    */
    bool isBanned(const std::string &ip) const;


        private:
            // 核心判定
            bool allow(RateState &st,const RateLimitType &type,const int &times,const int &secs,const std::chrono::steady_clock::time_point &now);

        private:
            int maxConnections;
            int connectionTimeout;

            RateLimitType connectStrategy = RateLimitType::Cooldown;
            RateLimitType requestStrategy = RateLimitType::SlidingWindow;
            RateLimitType pathStrategy    = RateLimitType::SlidingWindow;

            std::unordered_map<std::string, IPInformation> table;
            std::unordered_map<std::string, std::pair<int,int>> pathConfig;
            // IP -> 解封时间
            std::unordered_map<std::string,std::chrono::steady_clock::time_point> blacklist;
            inline void logSecurity(const std::string &msgCN,const std::string &msgEN);
        };

        
        
    }

    /**
    * @namespace stt::network
    * @brief 网络框架，协议，通信，io多路复用相关
    * @ingroup stt
    */
    namespace network
    {
    /**
    * @brief tcp协议的套接字操作类
    */
    class TcpFDHandler
    {
    protected:
        int fd=-1;
        bool flag1=false;
        bool flag2=false;
        SSL *ssl=nullptr;
        int sec=-1;
        std::function<int(std::string)> queuedSendFunction;
        std::function<void()> queuedCloseFunction;
    public:
        /**
        * @brief 如果sendData的block=true，如果发送过程中连接断开，这个标志位会置为true
        */
        bool flag3=false;
    public:
        /**
        * @brief 传入套接字初始化对象
        * @note 可以选择设置套接字的阻塞和非阻塞，SO_REUSEADDR模式（只可能在服务端socket用到），还能选择该套接字加密后的SSL句柄
        * @param fd 套接字
        * @param ssl fd经过TLS加密后的SSL句柄（如果没有可以填nullptr）
        * @param flag1 true：启用非阻塞模式  false：启用阻塞模式 （默认为false，即启用阻塞模式）
        * @param flag2 true：启用SO_REUSEADDR模式  false：不启用SO_REUSEADDR模式 （默认为false，即不启用SO_REUSEADDR模式）
        * @param sec 阻塞超时时间 阻塞超过这个时间就不会再阻塞了 默认为-1 即无限等待
        */
        void setFD(const int &fd,SSL *ssl,const bool &flag1=false,const bool &flag2=false,const int &sec=-1);
        /**
        * @brief 由服务端 Reactor 注入异步发送和关闭通道。
        * @param sendFunction 接收完整数据并返回已接收入队字节数的函数。
        * @param closeFunction 请求 Reactor 关闭连接的函数。
        * @note 主要供 TcpServer 及派生类构造回调 handler；普通客户端不需要调用。
        * @note 注入后，sendData() 只把数据提交到有界发送队列，实际 socket/SSL 写入由 Reactor 完成。
        */
        void setTransportFunctions(std::function<int(std::string)> sendFunction,std::function<void()> closeFunction={})
        {
            queuedSendFunction=std::move(sendFunction);
            queuedCloseFunction=std::move(closeFunction);
        }
        /**
        * @brief 获取该对象的套接字
        * @return 返回该对象的套接字
        */
        int getFD(){return fd;}
        /**
        * @brief 获取该对象的加密SSL句柄
        * @return 返回加密SSL句柄。如果没有加密SSL句柄，返回nullptr。
        */
        SSL *getSSL(){return ssl;}
        /**
        * @brief 关闭对象
        * @param cle true：关闭对象并且关闭原对象句柄的套接字的链接   false：仅关闭对象  （默认为ture，关闭对象也关闭连接）
        */
        void close(const bool &cle=true);
        /**
        * @brief 设置对象中的套接字为阻塞模式
        * @param sec 阻塞超时时间 阻塞超过这个时间就不会再阻塞了 默认为-1 即无限等待
        */
        void blockSet(const int &sec = -1);
        /**
        * @brief 设置对象中的套接字为非阻塞模式
        */
        void unblockSet();
        /**
        * @brief 设置对象中的套接字为SO_REUSEADDR模式
        */
        bool multiUseSet();
        /**
        * @brief 判断对象是否有套接字绑定
        * @return true：对象绑定了套接字  false：对象没有绑定套接字
        */
        bool isConnect(){if(fd==-1)return false;else return true;}
    public:
        /**
        * @brief 向已连接的套接字发送字符串数据。
        *
        * @param data 要发送的数据内容（std::string 类型）。
        * @param block 是否以阻塞模式发送（默认 true）。
        *              - true：会阻塞直到全部数据发送成功除非出错了（无论 socket 是阻塞或非阻塞）；如果需要判断是否连接断开了可以检查flag3标志位判断。
        *              - false：阻塞与否取决于套接字状态。
        * 
        * @return
        * - 返回值 > 0：成功发送的字节数；
        * - 返回值 = 0：连接已关闭（block=false）或者发送成功的字节数为0（block=true）；
        * - 返回值 < 0（只可能在block=false的情况下）：发送失败；
        *   - -99：对象未绑定 socket；
        *   - -100：非阻塞模式下，发送缓冲区已满。
        *   - -101：服务端异步发送队列已关闭或超过每连接高水位。
        *
        * @note 若 block 为 true，会持续阻塞直到全部数据发送完毕除非出错了（无论 socket 是阻塞或非阻塞），返回值一定>=0,适合希望确保完整发送的场景。如果需要判断是否连接断开了可以检查flag3标志位判断。
        * 若 block 为 false，阻塞与否取决于套接字状态。返回值可能小于 希望发送的长度，需手动处理剩余数据。
        * @note 当 handler 由 TcpServer 回调提供时，成功返回表示数据已被有界队列接受，并不表示已经写入内核或对端；
        *       block 参数在该模式下不会让 Worker 线程直接写 socket/SSL。
        */
        int sendData(const std::string &data,const bool &block=true);
        /**
        * @brief 向已连接的套接字发送指定长度的二进制数据。
        *
        * @param data 指向要发送的数据缓冲区。
        * @param length 数据长度（字节）。
        * @param block 是否以阻塞模式发送（默认 true）。
        *              - true：会阻塞直到全部数据发送成功除非出错了（无论 socket 是阻塞或非阻塞）；如果需要判断是否连接断开了可以检查flag3标志位判断。
        *              - false：阻塞与否取决于套接字状态。
        * 
        * @return
        * - 返回值 > 0：成功发送的字节数；
        * - 返回值 = 0：连接已关闭（block=false）或者发送成功的字节数为0（block=true）；
        * - 返回值 < 0（只可能在block=false的情况下）：发送失败；
        *   - -99：对象未绑定 socket；
        *   - -100：非阻塞模式下，发送缓冲区已满。
        *   - -101：服务端异步发送队列已关闭或超过每连接高水位。
        *
        * @note 若 block 为 true，会持续阻塞直到全部数据发送完毕除非出错了（无论 socket 是阻塞或非阻塞），返回值一定>=0,适合希望确保完整发送的场景。如果需要判断是否连接断开了可以检查flag3标志位判断。
        * 若 block 为 false，阻塞与否取决于套接字状态。返回值可能小于 length，需手动处理剩余数据。
        * @note 当 handler 由 TcpServer 回调提供时，成功返回表示数据已被有界队列接受，并不表示已经写入内核或对端。
        */
        int sendData(const char *data,const uint64_t &length,const bool &block=true);
        /**
        * @brief 从已连接的套接字中阻塞接收指定长度的数据到字符串
        *
        * @param data 接收数据的数据容器（string类型）
        * @param length 接收长度
        * @param sec 等待时间 单位为秒 -1为无限阻塞等待 （默认为2s）
        * @return
        * - 返回值 > 0：成功接收的字节数；
        * - 返回值 = 0：连接已关闭；
        * - 返回值 < 0：接收失败；
        *   - -99：对象未绑定 socket；
        *   - -100：超时
        *
        * @note 如果没有接收完指定的数据大小，一定阻塞到超时或者出错
        */
        int recvDataByLength(std::string &data,const uint64_t &length,const int &sec=2);
        /**
        * @brief 从已连接的套接字中阻塞接收指定长度的数据到char*容器
        *
        * @param data 接收数据的数据容器（char*类型）
        * @param length 接收长度
        * @param sec 等待时间 单位为秒 -1为无限阻塞等待 （默认为2s）
        * @return
        * - 返回值 > 0：成功接收的字节数；
        * - 返回值 = 0：连接已关闭；
        * - 返回值 < 0：接收失败；
        *   - -99：对象未绑定 socket；
        *   - -100：超时
        *
        * @note 如果没有接收完指定的数据大小，一定阻塞到超时或者出错
        */
        int recvDataByLength(char *data,const uint64_t &length,const int &sec=2);
        /**
        * @brief 从已连接的套接字中接收一次数据到string字符串容器
        * @param data 接收数据的数据容器（string类型）
        * @param length 最大接收长度
        * @return 
        * - 返回值 > 0：成功接收的字节数；
        * - 返回值 = 0：连接已关闭；
        * - 返回值 < 0：接收失败；
        *   - -99：对象未绑定 socket；
        *   - -100：非阻塞模式且没有数据
        * @note 接收是否会阻塞根据fd的阻塞情况决定
        */
        int recvData(std::string &data,const uint64_t &length);
        /**
        * @brief 从已连接的套接字中接收一次数据到char*容器
        * @param data 接收数据的数据容器（char*类型）
        * @param length 最大接收长度
        * @return 
        * - 返回值 > 0：成功接收的字节数；
        * - 返回值 = 0：连接已关闭；
        * - 返回值 < 0：接收失败；
        *   - -99：对象未绑定 socket；
        *   - -100：非阻塞模式且没有数据
        * @note 接收是否会阻塞根据fd的阻塞情况决定
        */
        int recvData(char *data,const uint64_t &length);
    };

    /**
    * @brief tcp协议客户端操作类
    * @note 默认是阻塞模式的
    */
    class TcpClient:public TcpFDHandler
    {
    private:
        std::string serverIP="";
        int serverPort=-1;
        bool flag=false;
        bool TLS;
        SSL_CTX *ctx=nullptr;
        const char *ca;
        const char *cert;
        const char *key;
        const char *passwd;
    private:
        bool createFD();
        void closeAndUnCreate();
        bool initCTX(const char *ca,const char *cert="",const char *key="",const char *passwd="");
    public:
        /**
        * @brief TcpClient类的构造函数
        * @param TLS true：启用TLS加密  false：不启用TLS加密 （默认为false不启用）
        * @param ca CA 根证书文件路径；留空时使用操作系统默认信任库。
        * @param cert 客户端证书路径（可选 默认空）
        * @param key 客户端私钥路径（可选 默认空）
        * @param passwd 私钥解密密码（可选 默认空）
        * @note 
        * - TLS 最低版本为 1.2，并同时验证证书链和连接目标主机名。
        * - ca 留空时使用系统默认根证书；指定时使用给定 CA 文件。
        * -如果服务端要求客户端身份认证（双向 TLS/SSL），你需要提供一个有效的客户端证书。
        */
        TcpClient(const bool &TLS=false,const char *ca="",const char *cert="",const char *key="",const char *passwd="");
        /**
        * @brief 向服务端发起tcp连接
        * @param ip 服务端ip
        * @param port 服务端端口
        * @return true：DNS、TCP（以及可选 TLS）连接成功；false：失败。
        * @note ip 也可以是域名。默认创建的是阻塞 socket；TLS 模式会设置 SNI 并校验证书主机名。
        */
        bool connect(const std::string &ip,const int &port);        
        /**
        * @brief 重新或第一次设置TLS加密参数
        * @note 设置了的TLS参数伴随整个生命周期，除非调用这个函数重新设置
        * @param TLS true：启用TLS加密  false：不启用TLS加密 （默认为false不启用）
        * @param ca CA 根证书文件路径；留空时使用操作系统默认信任库。
        * @param cert 客户端证书路径（可选 默认空）
        * @param key 客户端私钥路径（可选 默认空）
        * @param passwd 私钥解密密码（可选 默认空）
        * @note 
        * - TLS 最低版本为 1.2，并同时验证证书链和连接目标主机名。
        * - ca 留空时使用系统默认根证书；指定时使用给定 CA 文件。
        * -如果服务端要求客户端身份认证（双向 TLS/SSL），你需要提供一个有效的客户端证书。
        */
        void resetCTX(const bool &TLS=false,const char *ca="",const char *cert="",const char *key="",const char *passwd="");
        /**
        * @brief 如果对象有套接字连接，关闭和释放这个连接和套接字，并且重新新建一个套接字。
        * @return true：关闭成功，新建套接字成功   false：关闭成功，新建套接字失败
        */
        bool close();
        /**
        * @brief TcpClient的析构函数，会关闭释放套接字和其连接
        */
        ~TcpClient(){closeAndUnCreate();}
    public:
        /**
        * @brief 返回已连接的服务端的ip
        * return 已连接的服务端的ip
        */
        std::string getServerIP(){return serverIP;}
        /**
        * @brief 返回已连接的客户端的端口
        * return 已连接的服务端的端口
        */
        int getServerPort(){return serverPort;}
        /**
        * @brief 返回对象的连接状态
        * @return  true：已连接  false：未连接
        */
        bool isConnect(){return flag;}
    };  

    /**
    * @brief Http/Https客户端操作类
    * @note 
    * -请求头都会自动带上Connection: keep-alive
    * -如果需要重新设置TLS/Https加密的证书，目前需要销毁对象后重新构造
    * -如果没用传入套接字的函数，底层TCP默认是阻塞的
    */
    class HttpClient:private TcpClient
    {
    private:
        bool flag=false;
    public:
        /**
        * @brief HttpClient类的构造函数
        * @param TLS true：启用Https加密  false：不启用Https加密 （默认为false不启用）
        * @param ca CA 根证书文件路径；留空时使用操作系统默认信任库。
        * @param cert 客户端证书路径（可选 默认空）
        * @param key 客户端私钥路径（可选 默认空）
        * @param passwd 私钥解密密码（可选 默认空）
        * @note 
        * - TLS 最低版本为 1.2，并同时验证证书链和连接目标主机名。
        * - ca 留空时使用系统默认根证书；指定时使用给定 CA 文件。
        * -如果服务端要求客户端身份认证（双向 TLS/SSL），你需要提供一个有效的客户端证书。
        */
        HttpClient(const bool &TLS=false,const char *ca="",const char *cert="",const char *key="",const char *passwd=""):TcpClient(TLS,ca,cert,key,passwd){}
    public:
        /**
        * @brief 发送一个GET请求到服务器
        * @note 返回结果请调用isReturn函数判断；如果有返回结果，返回头和返回体存在header和body这两个全局变量中。
        * @note 如果使用了TLS，会自动采用https协议，否则是自动采用http协议
        * @note 默认是阻塞的
        * @param url http/https的完整url（注意需要显式指定端口和路径） 如：https://google.com 要写成https://google.com:443/ (补全:443和/)
        * @param header Http 请求头；如果不是用 createHeader 生成，末尾应包含 CRLF。
        * @param header1 HTTP 请求头附加项；末尾不需要加入 CRLF（默认 keep-alive）。
        * @param sec 阻塞超时时间(s) 阻塞超过这个时间就不会再阻塞了 默认为-1 即无限等待
        * @return true：请求发送成功   false：请求发送失败  注意：这不代表是否返回，只说明了发送成功
        * @warning 需要http/https协议的完整url（注意需要显式指定端口和路径（就算路径不需要也要填入/）） 如：https://google.com 要写成https://google.com:443/ (补全:443和/)
        */
        bool getRequest(const std::string &url,const std::string &header="",const std::string &header1="Connection: keep-alive",const int &sec=-1);
        /**
        * @brief 发送一个POST请求到服务器
        * @note 返回结果请调用isReturn函数判断；如果有返回结果，返回头和返回体存在header和body这两个全局变量中。
        * @note 如果使用了TLS，会自动采用https协议，否则是自动采用http协议
        * @note 默认是阻塞的
        * @param url http/https的完整url（注意需要显式指定端口和路径） 如：https://google.com 要写成https://google.com:443/ (补全:443和/)
        * @param body http请求体
        * @param header Http 请求头；如果不是用 createHeader 生成，末尾应包含 CRLF。
        * @param header1 HTTP 请求头附加项；末尾不需要加入 CRLF（默认 keep-alive）。
        * @param sec 阻塞超时时间(s) 阻塞超过这个时间就不会再阻塞了 默认为-1 即无限等待
        * @return true：请求发送成功   false：请求发送失败  注意：这不代表是否返回，只说明了发送成功
        * @warning 需要http/https协议的完整url（注意需要显式指定端口和路径（就算路径不需要也要填入/）） 如：https://google.com 要写成https://google.com:443/ (补全:443和/)
        */
        bool postRequest(const std::string &url,const std::string &body="",const std::string &header="",const std::string &header1="Connection: keep-alive",const int &sec=-1);
        /**
        * @brief 从tcp套接字发送一个GET请求到服务器
        * @note 返回结果请调用isReturn函数判断；如果有返回结果，返回头和返回体存在header和body这两个全局变量中。
        * @note 如果填入的ssl不为nullptr，会自动采用https协议，否则是自动采用http协议
        * @note 调用的时候会阻塞，改变原有fd的状态，注意备份和恢复
        * @param fd tcp套接字
        * @param ssl TLS加密套接字
        * @param url http/https的完整url（注意需要显式指定端口和路径） 如：https://google.com 要写成https://google.com:443/ (补全:443和/)
        * @param header Http 请求头；如果不是用 createHeader 生成，末尾应包含 CRLF。
        * @param header1 HTTP 请求头附加项；末尾不需要加入 CRLF（默认 keep-alive）。
        * @param sec 阻塞超时时间(s) 阻塞超过这个时间就不会再阻塞了 默认为2s
        * @return true：请求发送成功   false：请求发送失败  注意：这不代表是否返回，只说明了发送成功
        * @warning 需要http/https协议的完整url（注意需要显式指定端口和路径（就算路径不需要也要填入/）） 如：https://google.com 要写成https://google.com:443/ (补全:443和/)
        */
        bool getRequestFromFD(const int &fd,SSL *ssl,const std::string &url,const std::string &header="",const std::string &header1="Connection: keep-alive",const int &sec=2);
        /**
        * @brief 发送一个POST请求到服务器
        * @note 返回结果请调用isReturn函数判断；如果有返回结果，返回头和返回体存在header和body这两个全局变量中。
        * @note 如果填入的ssl不为nullptr，会自动采用https协议，否则是自动采用http协议
        * @note 调用的时候会阻塞，改变原有fd的状态，注意备份和恢复
        * @param fd tcp套接字
        * @param ssl TLS加密套接字
        * @param url http/https的完整url（注意需要显式指定端口和路径） 如：https://google.com 要写成https://google.com:443/ (补全:443和/)
        * @param body http请求体
        * @param header Http 请求头；如果不是用 createHeader 生成，末尾应包含 CRLF。
        * @param header1 HTTP 请求头附加项；末尾不需要加入 CRLF（默认 keep-alive）。
        * @param sec 阻塞超时时间(s) 阻塞超过这个时间就不会再阻塞了 默认为-1 即无限等待
        * @return true：请求发送成功   false：请求发送失败  注意：这不代表是否返回，只说明了发送成功
        * @warning 需要http/https协议的完整url（注意需要显式指定端口和路径（就算路径不需要也要填入/）） 如：https://google.com 要写成https://google.com:443/ (补全:443和/)
        */
        bool postRequestFromFD(const int &fd,SSL *ssl,const std::string &url,const std::string &body="",const std::string &header="",const std::string &header1="Connection: keep-alive",const int &sec=2);
    public:
        /**
        * @brief 获取服务器返回响应状态
        * @return true：服务器返回响应成功  false：服务器返回响应失败
        */
        bool isReturn(){return flag;}
        /**
        * @brief 服务器返回响应头
        */
        std::string header="";
        /**
        * @brief 服务器返回响应体
        */
        std::string body="";
    };
    
    /**
    * @brief 用独立、可 join 的 Reactor 线程监听单个 Linux 文件描述符。
    * @note 对象拥有监听线程；停止或析构会通过 eventfd 立即唤醒 epoll，不再依赖轮询等待。
    */
    class EpollSingle
    {
    private:
        int fd=-1;
        bool flag=true;
        std::function<bool(const int &fd)> fc=[](const int &)->bool
        {return true;};
        std::function<void(const int &fd)> fcEnd=[](const int &)->void
        {};
        std::function<bool(const int &fd)> fcTimeOut=[](const int &)->bool
        {return true;};
        std::atomic<bool> flag1{true};
        std::atomic<bool> flag2{false};
        time::Duration dt{0,20,0,0,0};
        std::atomic<bool> flag3{false};
        time::Duration t;
        std::thread listenerThread;
        int controlFD=-1;
        std::mutex callbackMutex;
        std::mutex countdownMutex;
    private:
        void epolll();
    public:
        /**
        * @brief 开始监听
        * @param fd 需要监听的句柄
        * @param flag  true：水平触发    false：边缘触发
        * @param dt 填入监听超时时间
        * @note 从其他线程重复调用会先停止并 join 旧监听。不要在本对象自己的监听回调中重启或销毁对象；
        *       回调中需要停止时应调用 endListenWithSignal() 或返回 false。
        */
        void startListen(const int &fd,const bool &flag=true,const time::Duration &dt=time::Duration{0,0,20,0,0});
    public:
        /**
        * @brief 返回epoll监听状态
        * @return true：正在监听  false：没有监听
        */
        bool isListen(){return flag2.load(std::memory_order_acquire);}
        /**
        * @brief 设置epoll触发后的处理函数
        * 注册一个回调函数
        * @param fc 一个函数或函数对象，用于当有数据时上来的时候回调处理套接字fd
        * -参数：const int &fd - 要处理的套接字
        * -返回： bool - 返回true成功成功，返回false处理失败
        * @note 传入的函数应该有如下签名 bool func(const int &fd)
        * @note 如果处理失败了 会退出epoll监听（没有关闭套接字）
        */
        void setFunction(std::function<bool(const int &fd)> fc){std::lock_guard<std::mutex> lock(callbackMutex);this->fc=std::move(fc);}
        /**
        * @brief 设置epoll退出前的回调函数
        * 注册一个回调函数
        * @param fcEnd 一个函数或函数对象，用于处理epoll退出前的流程
        * -参数：const int &fd - 要处理的套接字
        * @note 传入的函数应该有如下签名 void func(const int &fd)
        */
        void setEndFunction(std::function<void(const int &fd)> fcEnd){std::lock_guard<std::mutex> lock(callbackMutex);this->fcEnd=std::move(fcEnd);};
        /**
        * @brief 设置epoll超时后出发的回调函数
        * 注册一个回调函数
        * @param fcTimeOut 一个函数或函数对象，用于当epoll超时的时候回调处理套接字fd
        * -参数：const int &fd - 要处理的套接字
        * -返回： bool - 返回true成功成功，返回false处理失败
        * @note 传入的函数应该有如下签名 bool func(const int &fd)
        * @note 如果处理失败了 会退出epoll监听（没有关闭套接字）
        */
        void setTimeOutFunction(std::function<bool(const int &fd)> fcTimeOut){std::lock_guard<std::mutex> lock(callbackMutex);this->fcTimeOut=std::move(fcTimeOut);};
        /**
        * @brief 结束epoll监听
        * 会阻塞直到epoll退出完成
        * @return true：处理结束  false：结束失败
        * @note 可重复调用。从监听回调线程调用时只请求停止，不会自 join。
        */
        bool endListen();
        /**
        * @brief 发送结束epoll的信号
        * @note 线程安全且不阻塞；使用 eventfd 唤醒 epoll，但不等待线程退出。
        */
        void endListenWithSignal();
        /**
        * @brief 开始退出epoll倒计时，直到套接字有新的消息
        * 如果套接字倒计时结束还没有新的消息，那么退出epoll
        * @param t 一个Duration对象 填入倒计时时长 （默认为10秒）
        */
        void waitAndQuit(const time::Duration &t=time::Duration{0,0,0,10,10})
        {
            std::lock_guard<std::mutex> lock(countdownMutex);
            this->t=t;
            flag3.store(true,std::memory_order_release);
        }
        /**
        * @brief EpollSingle的析构函数
        * 调用 endListen() 唤醒并 join 监听线程。
        */
        ~EpollSingle(){endListen();}
    };
    
    /**
    * @brief Websocket客户端操作的类
    * -如果需要重新设置TLS/Https加密的证书，目前需要销毁对象后重新构造
    * 底层TCP默认是阻塞的
    */
    class WebSocketClient:private TcpClient 
    {
    private:
        bool flag4=false;
        std::function<bool(const std::string &message,WebSocketClient &k)> fc=[](const std::string &message,WebSocketClient &)->bool
        {std::cout<<"收到: "<<message<<std::endl;return true;};
        std::string url;
        EpollSingle k;
        bool flag5=false;
    private:
        bool close1();
    public:
        /**
        * @brief WebSocketClient类的构造函数
        * @param TLS true：启用wss加密  false：不启用wss加密 （默认为false不启用）
        * @param ca CA 根证书文件路径；留空时使用操作系统默认信任库。
        * @param cert 客户端证书路径（可选 默认空）
        * @param key 客户端私钥路径（可选 默认空）
        * @param passwd 私钥解密密码（可选 默认空）
        * @note 
        * - TLS 最低版本为 1.2，并同时验证证书链和连接目标主机名。
        * - ca 留空时使用系统默认根证书；指定时使用给定 CA 文件。
        * -如果服务端要求客户端身份认证（双向 TLS/SSL），你需要提供一个有效的客户端证书。
        */
        WebSocketClient(const bool &TLS=false,const char *ca="",const char *cert="",const char *key="",const char *passwd=""):TcpClient(TLS,ca,cert,key,passwd){}
        /**
        * @brief 设置收到服务端消息后的回调函数
        * 注册一个回调函数
        * @param fc 一个函数或函数对象，用于收到服务端消息后处理逻辑
        * -参数：string &message - 要处理的套接字
                WebsocketClient &k - 当前对象的引用
        * -返回： bool - 返回true处理成功，返回false处理失败
        * @note 传入的函数应该有如下签名 bool func(const std::string &message,WebSocketClient &k)
        * @note 如果处理失败了 会直接关闭整个websocket连接
        */
        void setFunction(std::function<bool(const std::string &message,WebSocketClient &k)> fc){this->fc=fc;}
        /**
        * @brief 连接到websocket服务器
        * @param url ws/wss的完整url（注意需要显式指定端口和路径） 如：wss://google.com 要写成wss://google.com:443/ (补全:443和/)
        * @param min 心跳时间，单位分钟 （默认为20分钟）
        * @warning 需要ws/wss的完整url（注意需要显式指定端口和路径） 如：wss://google.com 要写成wss://google.com:443/ (补全:443和/) 
        */
        bool connect(const std::string &url,const int &min=20);
        /**
        * @brief 发送 WebSocket 消息
        * 
        * 根据 WebSocket 协议，封装并发送一条带掩码的数据帧（客户端必须使用掩码），
        * 支持根据 payload 长度自动选择帧格式：
        * - payload <= 125 字节：使用 1 字节长度
        * - 126 <= payload <= 65535：使用 2 字节扩展长度（附加 126 标记）
        * - payload > 65535：使用 8 字节扩展长度（附加 127 标记）
        * 
        * @param message 要发送的消息内容（已编码为文本或二进制）
        * @param type 指定消息类型的自定义字段（通常是 WebSocket 帧的 opcode）
        *        约定格式为 `"1000" + type`，其中：
        *        - "0001" 表示文本帧（Text Frame）
        *        - "0010" 表示二进制帧（Binary Frame）
        *        - "1000" 表示连接关闭（Close Frame）
        *        - "1001" 表示 Ping 帧
        *        - "1010" 表示 Pong 帧
        *        请根据内部约定使用，默认使用 text（文本帧）
        * 
        * @return true 发送成功
        * @return false 发送失败（可能因连接未建立或发送异常）
        */
        bool sendMessage(const std::string &message,const std::string &type="0001");
        /**
        * @brief 发送关闭帧并关闭 WebSocket 连接（简化方式）
        * 
         * 直接传入编码后的关闭 payload，其中前两字节为关闭码（big-endian），
         * 后续为 UTF-8 编码的关闭原因描述，用于简化调用。
        * 
        * @param closeCodeAndMessage 编码后的关闭帧 payload（2 字节关闭码 + 可选消息）
        * @param wait 是否等待底层监听线程/epoll事件处理后退出
        */
        void close(const std::string &closeCodeAndMessage,const bool &wait=true);
        /**
        * @brief 发送关闭帧并关闭 WebSocket 连接（标准方式）
        * 
        * 构建符合 RFC 6455 的关闭帧（opcode = 0x8），帧 payload 包含关闭码（2 字节）与可选关闭原因字符串。
        * 
        * @param code WebSocket 关闭码，常见包括：
        * - 1000：正常关闭（Normal Closure）
        * - 1001：终端离开（Going Away）
        * - 1002：协议错误（Protocol Error）
        * - 1003：不支持的数据类型（Unsupported Data）
        * - 1006：非正常关闭（无关闭帧，程序内部使用）
        * - 1008：策略违规（Policy Violation）
        * - 1011：服务器内部错误（Internal Error）
        * 
        * @param message 可选关闭原因，供调试或日志记录用
        * @param wait 是否等待底层监听线程/epoll事件处理后退出
        */
        void close(const short &code=1000,const std::string &message="bye",const bool &wait=true);       
    public:
        /**
        * @brief 返回连接状态
        * @return true：和服务器存在连接  false：没有连接
        */
        bool isConnect(){return flag4;}
        /**
        * @brief 如果连接到了服务器 返回url
        * @return 返回url
        */
        std::string getUrl(){return url;}
        /**
        * @brief 如果连接到了服务器 返回服务器ip
        * @return 返回服务器ip
        */
        std::string getServerIp(){return TcpClient::getServerIP();}
        /**
        * @brief 如果连接到了服务器，以字符串返回服务器端口。
        * @return 十进制端口字符串；未连接时为 "-1"。
        * @note 为保持源代码兼容保留字符串返回类型；新代码建议使用 getServerPortNumber()。
        */
        std::string getServerPort(){return std::to_string(TcpClient::getServerPort());}
        /**
        * @brief 如果连接到了服务器，以整数返回服务器端口。
        * @return 端口号；未连接时为 -1。
        */
        int getServerPortNumber(){return TcpClient::getServerPort();}
        /**
        * @brief WebSocketClient类的析构函数，销毁对象时候会优雅退出断开连接
        */    
        ~WebSocketClient();
    };

    /**
    * @brief 保存HTTP/HTTPS请求信息的结构体
    */
    struct HttpRequestInformation 
    {
        /**
        * @brief 底层的socket套接字
        */
        int fd=-1;
        /**
        * @brief 连接对象fd
        */
        uint64_t connection_obj_fd=0;
        /**
        * @brief 请求类型
        */
        std::string type;
        /**
        * @brief url中的路径和参数
        */
        std::string locPara;
        /**
        * @brief url中的路径
        */
        std::string loc;
        /**
        * @brief url中的参数
        */
        std::string para;
        /**
        * @brief 请求头
        */
        std::string header;
        /**
        * @brief 请求体
        */
        std::string body;
        /**
        * @brief 请求体（chunked）
        */
        std::string body_chunked;
        /**
        * @brief 所需的数据仓库
        */
        std::unordered_map<std::string,std::any> ctx;

        /**
        * @brief 按大小写不敏感方式读取请求头，不分配内存。
        * @param name 不带冒号的 header 名，例如 `content-type`。
        * @return 指向当前 `header` 字符串的视图；不存在时返回空视图。
        * @warning 修改或销毁本请求后，返回的视图失效。
        */
        std::string_view headerValue(std::string_view name) const noexcept;

        /**
        * @brief 统一读取 Content-Length 或 chunked 请求体。
        * @return 指向 `body` 或 `body_chunked` 的非拥有视图。
        */
        std::string_view bodyView() const noexcept;
    };
    
    struct TcpFDInf;
    /**
    * @brief 解析，响应Http/https请求的操作类
    * 仅传入套接字，然后使用这个类进行Http的操作
    */
    class HttpServerFDHandler:public TcpFDHandler
    {
    public:
        /**
        * @brief 初始化对象，传入套接字等参数
        * @param fd 套接字
        * @param ssl TLS加密的SSL句柄(默认为nullptr)
        * @param flag1 true：启用非阻塞模式  false：启用阻塞模式 （默认为false，即启用阻塞模式）
        * @param flag2 true：启用SO_REUSEADDR模式  false：不启用SO_REUSEADDR模式 （默认为true，即启用SO_REUSEADDR模式）
        */
        void setFD(const int &fd,SSL *ssl=nullptr,const bool &flag1=false,const bool &flag2=true){TcpFDHandler::setFD(fd,ssl,flag1,flag2);}
        /**
        * @brief 解析Http/Https请求
        * @param TcpInf 存放底层tcp处理套接字的信息
        * @param HttpInf 存放Http协议的信息
        * @param buffer_size 服务器定义的解析缓冲区的大小（单位为字节)
        * @param times 记录解析的次数，某些场景会用上
        * @param max_header_size HTTP 请求头最大字节数，默认 64 KiB，且不会超过 buffer_size。
        * @return -1:解析失败 0:还需要继续解析 1:解析完成
        * @note 支持 Content-Length、chunked、trailers 和 HTTP/1.1 流水线；字段名大小写不敏感。
        * @note 会拒绝重复 Content-Length、Content-Length 与 Transfer-Encoding 并存、无效 Host、
        *       非最小长度编码及越界报文，避免常见 request smuggling 输入。
        */
        int solveRequest(TcpFDInf &TcpInf,HttpRequestInformation &HttpInf,const unsigned long &buffer_size,const int &times=1,const unsigned long &max_header_size=64UL*1024UL);
        /**
        * @brief 发送Http/Https响应
        * @param data 装着响应体的数据的string容器
        * @param code Http响应状态码和状态说明 （默认是 200 OK）
        * @param header Http 响应头；如果不是用 createHeader 生成，末尾应包含 CRLF。
        * @param header1 HTTP 响应头附加项；末尾不需要加入 CRLF。
        * @return  true：发送响应成功  false：发送响应失败
        */
        bool sendBack(const std::string &data,const std::string &header="",const std::string &code="200 OK",const std::string &header1="");

        /**
        * @brief 发送文本响应，自动补充 Content-Type 和 Content-Length。
        * @param data 响应体。
        * @param code HTTP 状态，例如 `200 OK`。
        * @param contentType MIME 类型，默认 UTF-8 纯文本。
        * @param extraHeaders 额外响应头，可以是多行。
        */
        bool sendText(const std::string &data,const std::string &code="200 OK",
                      const std::string &contentType="text/plain; charset=utf-8",
                      const std::string &extraHeaders="");

        /**
        * @brief 序列化 Json::Value 并发送 application/json UTF-8 响应。
        */
        bool sendJson(const Json::Value &data,const std::string &code="200 OK",
                      const std::string &extraHeaders="");

        /**
        * @brief 发送 HTTP 重定向响应。
        * @param location Location 目标；包含 CR/LF 时会拒绝，避免响应头注入。
        * @param code 默认 `302 Found`，也可使用 `301 Moved Permanently`。
        */
        bool redirect(const std::string &location,const std::string &code="302 Found");
        /**
        * @brief 发送Http/Https响应
        * @param data 装着响应体的数据的char *容器
        * @param length char*容器中的数据长度
        * @param code Http响应状态码和状态说明 （默认是 200 OK）
        * @param header Http 响应头；如果不是用 createHeader 生成，末尾应包含 CRLF。
        * @param header1 HTTP 响应头附加项；末尾不需要加入 CRLF。
        * @param header_length 响应头部长度的预估值，仅用于减少内存扩容；不准确不会影响发送正确性（默认50）。
        * @warning header、code、header1 必须以 \0 结尾；data 按 length 读取，可以包含二进制零字节。
        * @return  true：发送响应成功  false：发送响应失败
        */
        bool sendBack(const char *data,const size_t &length,const char *header="\0",const char *code="200 OK\0",const char *header1="\0",const size_t &header_length=50);
    };
    /**
    * @brief 保存客户端WS/WSS请求信息的结构体
    */
    struct WebSocketFDInformation
    {
        /**
        * @brief 底层的socket套接字
        */
        int fd=-1;
        /**
        * @brief 连接对象fd
        */
        uint64_t connection_obj_fd=0;
        /**
        * @brief true:发送了关闭帧  false：没有发送关闭帧
        */
        bool closeflag=false;
        /**
        * @brief  握手阶段的Http/Https路径和参数
        */
        std::string locPara;
        /**
        * @brief  握手阶段的Http/Https请求头
        */
        std::string header;
        /**
        * @brief 发送心跳的时间（没有发送过就填0） (检查完又要清空为0)
        */
        time_t HBTime=0;
        /**
        * @brief 上次收到信息的时间
        */
        time_t response=0;
        /**
        * @brief 待接收的长度
        */
        size_t recv_length=0;
        /**
        * @brief 已经接收的长度
        */
        size_t have_recv_length=0;
        /**
        * @brief 消息类型
        */
        int message_type=0;
        /**
        * @brief 消息
        */
        std::string message="";
        /**
        * @brief fin的状态
        */
        bool fin=true;
        /** 正在接收的分片消息 opcode；0 表示当前没有分片消息。 */
        uint8_t fragmented_opcode=0;
        /** 分片消息的累计 payload，受服务器每连接接收上限约束。 */
        std::string fragmented_message;
        /**
        * @brief mask
        */
        std::string mask;
        /**
        * @brief 所需的数据仓库
        */
        std::unordered_map<std::string,std::any> ctx;
        /**
        * @brief 握手阶段保存的http信息
        */
        HttpRequestInformation httpinf;
    };

    /**
    * @brief 保存Tcp客户端的信息
    */
    struct TcpInformation
    {
        /**
        * @brief 套接字fd
        */
        int fd=-1;
        /**
        * @brief 连接对象fd
        */
        uint64_t connection_obj_fd=0;
        /**
        * @brief 裸数据
        */
        std::string data;
        /**
        * @brief 所需的数据仓库
        */
        std::unordered_map<std::string,std::any> ctx;
    };

    enum class TLSState : uint8_t {
    NONE = 0,        // 非 TLS 连接（普通 TCP）
    HANDSHAKING,    // TLS 握手中（SSL_accept 还没完成）
    ESTABLISHED,    // TLS 已建立，可以 SSL_read / SSL_write
    ERROR           // TLS 出错（可选）
    };

    /** @brief TLS 服务端对客户端证书的校验模式。 */
    enum class TLSClientAuthMode : uint8_t {
        None = 0, /**< 不请求客户端证书，适用于普通 HTTPS/WSS。 */
        Optional, /**< 请求并校验客户端证书，但允许客户端不提供。 */
        Required  /**< 必须提供并通过校验，适用于双向 TLS（mTLS）。 */
    };

    /**
    * @brief TcpServer 运行指标的无锁快照。
    * @note 所有计数均为进程内累计值；active_connections 与 pending_write_bytes 为当前值。
    *       获取快照不会阻塞 Reactor，可从任意线程调用。
    */
    struct ServerMetricsSnapshot
    {
        uint64_t accepted_connections=0;       /**< 成功接收的连接总数。 */
        uint64_t active_connections=0;         /**< 当前仍持有底层 fd 的连接数。 */
        uint64_t closed_connections=0;         /**< 已完成底层资源释放的连接总数。 */
        uint64_t rejected_connections=0;       /**< 因达到并发连接上限而拒绝的连接总数。 */
        uint64_t accept_errors=0;              /**< accept4 的非暂时性失败次数。 */
        uint64_t tls_handshake_failures=0;     /**< TLS 服务端握手失败次数。 */
        uint64_t parsed_http_requests=0;       /**< 成功完成边界校验和解析的 HTTP 请求数。 */
        uint64_t queued_write_bytes=0;         /**< 业务层成功提交到发送队列的累计字节数。 */
        uint64_t sent_bytes=0;                 /**< Reactor 成功写入 socket/TLS 的累计字节数。 */
        uint64_t pending_write_bytes=0;        /**< 所有连接当前尚未发送的队列字节数。 */
        uint64_t peak_pending_write_bytes=0;   /**< 所有连接待发字节数的历史峰值。 */
        uint64_t peak_pending_worker_tasks=0;  /**< Worker 等待任务数的历史峰值。 */
        uint64_t write_overflows=0;            /**< 超过每连接发送高水位的次数。 */
        uint64_t worker_queue_overflows=0;     /**< Worker 完成环形队列进入后备队列的次数。 */
        uint64_t send_ready_queue_overflows=0; /**< 发送通知环形队列进入后备队列的次数。 */
        uint64_t worker_task_rejections=0;     /**< Worker 任务队列达到上限或停止后拒绝的任务数。 */
        uint64_t reactor_wakeups=0;            /**< 实际写入 eventfd 的 Reactor 唤醒次数。 */
        uint64_t reactor_wakeups_coalesced=0;  /**< 被合并、未产生额外系统调用的唤醒次数。 */
        uint64_t write_syscalls=0;             /**< Reactor 发起的 socket/TLS 写调用次数。 */
        uint64_t batched_write_syscalls=0;     /**< 使用多个 iovec 合并发送的 sendmsg 调用次数。 */
        uint64_t batched_write_buffers=0;      /**< 被批量发送调用覆盖的发送队列数据块总数。 */
        uint64_t idle_timeout_checks=0;         /**< 增量空闲连接检查次数。 */
        uint64_t idle_timeout_closes=0;         /**< 因超过空闲超时而关闭的连接数。 */
        uint64_t graceful_shutdown_timeouts=0;  /**< 优雅排空超过配置时限、转为强制关闭的次数。 */
    };

    /**
    * @brief 新连接的常用 TCP 套接字调优参数。
    * @note 通过 TcpServer::setSocketOptions() 在 startListen() 前设置；0 字节缓冲表示沿用系统默认值。
    */
    struct ServerSocketOptions
    {
        bool tcp_no_delay=true;          /**< 禁用 Nagle，降低小响应和 WebSocket 消息延迟。 */
        bool keep_alive=false;           /**< 启用内核 SO_KEEPALIVE。 */
        bool reuse_port=true;            /**< 监听 socket 启用 SO_REUSEPORT；默认保持历史行为。 */
        int receive_buffer_bytes=0;      /**< SO_RCVBUF，0 表示系统默认。 */
        int send_buffer_bytes=0;         /**< SO_SNDBUF，0 表示系统默认。 */
        int keep_alive_idle_seconds=0;   /**< TCP_KEEPIDLE，0 表示系统默认。仅 keep_alive=true 时生效。 */
        int keep_alive_interval_seconds=0; /**< TCP_KEEPINTVL，0 表示系统默认。 */
        int keep_alive_probe_count=0;    /**< TCP_KEEPCNT，0 表示系统默认。 */
        int defer_accept_seconds=0;      /**< TCP_DEFER_ACCEPT，0 表示禁用/系统默认。 */
        int fast_open_queue=0;           /**< TCP_FASTOPEN 服务端队列长度，0 表示不主动配置。 */
        int listen_backlog=0;            /**< listen backlog，0 表示按最大连接数自动计算（128..4096）。 */
    };

    /**
    * @brief 单个服务端连接的有界异步发送状态。
    * @note 由 TcpServer 创建和管理；公开定义是为了让 handler 安全持有共享状态，业务代码不应直接修改字段。
    */
    struct ConnectionWriteState
    {
        std::mutex mutex;                    /**< 保护本结构中其余可变字段。 */
        std::deque<std::string> queue;        /**< 保持业务提交顺序的完整数据块。 */
        size_t front_offset=0;                /**< 队首数据块已写出的偏移。 */
        size_t queued_bytes=0;                /**< 当前尚未写出的总字节数。 */
        size_t max_queued_bytes=4UL*1024UL*1024UL; /**< 本连接待发高水位。 */
        int fd=-1;                            /**< 底层文件描述符。 */
        uint64_t connection_obj_fd=0;         /**< fd 复用保护代次。 */
        bool notification_pending=false;      /**< 是否已有 send-ready 通知在途。 */
        bool close_requested=false;           /**< 是否请求 Reactor 关闭连接。 */
        bool close_after_flush=false;          /**< 是否等待队列排空后关闭。 */
        bool overflowed=false;                 /**< 是否曾超过高水位。 */
        bool closed=false;                     /**< 状态是否已停止接受数据。 */
    };

    /** @brief 跨线程通知 Reactor 某连接有待发数据的轻量消息。 */
    struct SendReadyMessage
    {
        int fd=-1;                    /**< 底层文件描述符。 */
        uint64_t connection_obj_fd=0; /**< fd 复用保护代次。 */
    };

    /**
    * @brief 保存底层基础Tcp通道信息的结构体
    */
    struct TcpFDInf
    {
        /**
        * @brief 套接字fd
        */
        int fd=-1;
        /**
        * @brief 连接对象fd
        */
        uint64_t connection_obj_fd=0;
        /**
        * @brief 客户端ip
        */
        std::string ip;
        /**
        * @brief 客户端端口
        */
        std::string port;
        /**
         * @brief 记录当前处理状态机到第几步了
         */
        int FDStatus=-1;
        /**
        * @brief 等待处理的队列
        */
        std::queue<std::any> pendindQueue;
        /**
        * @brief 当前fd的接收状态，用于保存接收处理机逻辑
        */
        int status=0;
        /**
        * @brief 保存收到的客户端传来的数据
        */
        std::string_view data;
        /**
        * @brief 如果加密了，存放加密句柄
        */
        SSL* ssl=nullptr;
        /**
        * @brief tls状态
        */
        TLSState tls_state=TLSState::NONE;
        /**
        * @brief 接收空间指针
        */
        char *buffer=nullptr;
        /**
        * @brief 接收空间位置指针
        */
        unsigned long p_buffer_now=0;
        /** 当前已分配的接收缓冲容量；首次收到数据时才按需分配。 */
        unsigned long buffer_capacity=0;
        /** 正在 WorkerPool 执行的任务数。Reactor 在归零前不复用该 fd。 */
        size_t active_workers=0;
        /** 对端已断开，等待在途 worker 安全退出。 */
        bool closing=false;
        std::shared_ptr<ConnectionWriteState> write_state;
        bool write_interest=false;
        bool write_waiting_for_read=false;
    };

    /**
    * @brief 工作现场完成任务后压入完成队列的数据结构
    */
    struct WorkerMessage
    {
        /**
        * @brief 底层套接字
        */
        int fd=-1;
        /** 防止 fd 关闭后被新连接复用时串线。 */
        uint64_t connection_obj_fd=0;
        /**
        * @brief 返回值 -2:失败并且要求关闭连接 -1:失败但不需要关闭连接 1:成功
        */
        int ret=-2;
        /** Worker 中使用的请求副本，由 Reactor 验证连接代次后合并。 */
        std::shared_ptr<void> request;
    };
    
    
    /**
    * @brief Tcp服务端类
    * @note 默认底层实现是epoll边缘触发+套接字非阻塞模式
    */
    class TcpServer 
    {
    protected:
        system::MPSCQueue<WorkerMessage> finishQueue;
        system::MPSCQueue<SendReadyMessage> sendReadyQueue;
        stt::system::WorkerPool *workpool=nullptr;
        unsigned long buffer_size;
        unsigned long long  maxFD;
        security::ConnectionLimiter connectionLimiter;
        //std::unordered_map<int,TcpFDInf> clientfd;
        //std::mutex lc1;
        std::unordered_map<int,TcpFDInf> clientfd;
        std::atomic<bool> flag1{false};
        //std::queue<QueueFD> *fdQueue;
        //std::mutex *lq1;
        //std::condition_variable cv1;
        //std::condition_variable *cv;
        //int consumerNum;
        //std::mutex lco1;
        bool unblock;
        SSL_CTX *ctx=nullptr;
        bool TLS=false;
        mutable std::mutex tlsContextMutex;
        //std::unordered_map<int,SSL*> tlsfd;
        //std::mutex ltl1;
        bool security_open;
        //bool flag_detect;
        //bool flag_detect_status;
        std::atomic<int> workerEventFD{-1};
        std::atomic<bool> workerWakePending{false};
        std::recursive_mutex lifecycleMutex;
        std::thread reactorThread;
        std::mutex reactorStartupMutex;
        std::condition_variable reactorStartupCV;
        bool reactorStartupComplete=false;
        bool reactorStartupSuccess=false;
        std::atomic<bool> gracefulDrainRequested{false};
        std::mutex gracefulShutdownMutex;
        std::condition_variable gracefulShutdownCV;
        std::mutex overflowFinishMutex;
        std::deque<WorkerMessage> overflowFinishQueue;
        std::mutex overflowSendReadyMutex;
        std::deque<SendReadyMessage> overflowSendReadyQueue;
        std::mutex writeRegistryMutex;
        std::unordered_map<int,std::weak_ptr<ConnectionWriteState>> writeRegistry;
        std::deque<SendReadyMessage> bufferedReadQueue;
        std::deque<SendReadyMessage> timeoutCandidates;
        int serverType; // 1 tcp 2 http 3 websocket
        int connectionSecs;
        int connectionTimes;
        int requestSecs;
        int requestTimes;
        int checkFrequency;
        uint64_t connection_obj_fd;
        size_t maxPendingWriteBytes=4UL*1024UL*1024UL;
        size_t writeBudgetPerEvent=256UL*1024UL;
        size_t maxHttpHeaderBytes=64UL*1024UL;
        size_t maxPendingWorkerTasks=65536;
        size_t workerCompletionBudgetPerWake=2048;
        size_t sendReadyBudgetPerWake=256;
        size_t gracefulShutdownTimeoutMs=5000;
        ServerSocketOptions socketOptions;
        std::atomic<uint64_t> metricAcceptedConnections{0};
        std::atomic<uint64_t> metricActiveConnections{0};
        std::atomic<uint64_t> metricClosedConnections{0};
        std::atomic<uint64_t> metricRejectedConnections{0};
        std::atomic<uint64_t> metricAcceptErrors{0};
        std::atomic<uint64_t> metricTLSHandshakeFailures{0};
        std::atomic<uint64_t> metricParsedHttpRequests{0};
        std::atomic<uint64_t> metricQueuedWriteBytes{0};
        std::atomic<uint64_t> metricSentBytes{0};
        std::atomic<uint64_t> metricPendingWriteBytes{0};
        std::atomic<uint64_t> metricPeakPendingWriteBytes{0};
        std::atomic<uint64_t> metricPeakPendingWorkerTasks{0};
        std::atomic<uint64_t> metricWriteOverflows{0};
        std::atomic<uint64_t> metricWorkerQueueOverflows{0};
        std::atomic<uint64_t> metricSendReadyQueueOverflows{0};
        std::atomic<uint64_t> metricWorkerTaskRejections{0};
        std::atomic<uint64_t> metricReactorWakeups{0};
        std::atomic<uint64_t> metricReactorWakeupsCoalesced{0};
        std::atomic<uint64_t> metricWriteSyscalls{0};
        std::atomic<uint64_t> metricBatchedWriteSyscalls{0};
        std::atomic<uint64_t> metricBatchedWriteBuffers{0};
        std::atomic<uint64_t> metricIdleTimeoutChecks{0};
        std::atomic<uint64_t> metricIdleTimeoutCloses{0};
        std::atomic<uint64_t> metricGracefulShutdownTimeouts{0};
    private:
        std::function<void(const int &fd)> closeFun=[](const int &)->void
        {

        };
        std::function<void(TcpFDHandler &k,TcpInformation &inf)> securitySendBackFun=[](TcpFDHandler &,TcpInformation &)->void
        {};
        std::function<bool(TcpFDHandler &k,TcpInformation &inf)> globalSolveFun=[](TcpFDHandler &,TcpInformation &)->bool
        {return true;};
        std::unordered_map<std::string,std::vector<std::function<int(TcpFDHandler &k,TcpInformation &inf)>>> solveFun;
        std::function<int(TcpFDHandler &k,TcpInformation &inf)> parseKey=[](TcpFDHandler &,TcpInformation &inf)->int
        {inf.ctx["key"]=inf.data;return 1;};
        int fd=-1;
        std::atomic<int> port{-1};
        std::atomic<bool> flag{false};
        std::atomic<bool> flag2{true};
    private:
        void epolll(const int &evsNum,const int &listenFD);
        //virtual void consumer(const int &threadID);
        virtual void handler_netevent(const int &fd);
        virtual void handler_workerevent(WorkerMessage message);
        virtual void handleHeartbeat()=0;
        virtual void onConnectionClosed(const int &fd) {(void)fd;}
    protected:
        enum class WriteFlushResult { Drained, WaitWrite, WaitRead, Reschedule, Error };
        int enqueueWrite(const std::shared_ptr<ConnectionWriteState> &state,std::string data);
        void requestQueuedClose(const std::shared_ptr<ConnectionWriteState> &state);
        void requestCloseAfterFlush(const int &fd,const uint64_t expectedConnection=0);
        void publishSendReady(const std::shared_ptr<ConnectionWriteState> &state);
        void notifyReactor() noexcept;
        bool hasPendingReactorWork();
        void clearReactorQueues();
        void drainReactorWork(const int &epollFD);
        size_t drainWorkerResults(const size_t budget);
        size_t drainSendReady(const int &epollFD,const size_t budget);
        void applyAcceptedSocketOptions(const int &acceptedFD) const noexcept;
        void reportReactorStartup(const bool success);
        void advanceGracefulDrain();
        void prepareHandler(TcpFDHandler &handler,const int &fd);
        void prepareQueuedHandler(TcpFDHandler &handler,const int &fd);
        void prepareQueuedHandler(TcpFDHandler &handler,const int &fd,const uint64_t expectedConnection);
        WriteFlushResult flushConnectionWrites(TcpFDInf &connection);
        bool updateConnectionEvents(const int &epollFD,TcpFDInf &connection,const bool &wantWrite);
        void handleSendReady(const int &epollFD,SendReadyMessage message);
        void publishWorkerResult(WorkerMessage message);
        /** Reactor 内延续解析已在用户态缓冲区中的 HTTP 流水线请求。 */
        void scheduleBufferedRead(const int &fd,const uint64_t connection);
    public:
        /**
        * @brief 把一个任务放入工作线程池由工作线程完成
        * @note 应当把速度慢、会阻塞的io任务放入工作线程池
        * @warning 可执行对象必须严格按照规定的返回值返回
        * @param fun 放入工作线程池的可执行对象
        * -参数：TcpFDHandler &k - 和客户端连接的套接字的操作对象的引用
        *       TcpFDInf &inf - 客户端信息，保存数据，处理进度，状态机信息等
        * -返回值：-2:处理失败并且需要关闭连接 -1:处理失败但不需要关闭连接 1:处理成功
        * @param k 和客户端连接的套接字的操作对象的引用
        * @param inf 客户端信息的引用，保存数据，处理进度，状态机信息等
        * @note 本函数仅应从该连接的 Reactor 回调中调用。过载拒绝会按失败完成消息关闭连接，并计入 worker_task_rejections。
        */
        void putTask(const std::function<int(TcpFDHandler &k,TcpInformation &inf)> &fun,TcpFDHandler &k,TcpInformation &inf);
        /**
        * @brief 构造函数，默认是允许最大1000000个连接，每个连接接收缓冲区最大为256kb，启用安全模块。
        * @note 打开安全模块会对性能有影响
        * @param maxFD 服务对象的最大接受连接数 默认为1000000
        * @param buffer_size 同一个连接允许传输的最大数据量（单位为kb） 默认为256kb
        * @param finishQueue_cap Worker 完成队列（Worker → Reactor）的容量，必须为 2 的幂。
        * @note 建议容量不小于“峰值完成速率 × Reactor 最坏暂停时间”；环形队列满时进入带锁后备队列。
        * @param security_open true:开启安全模块 false：关闭安全模块 （默认为开启）
        * @param connectionNumLimit 同一个ip连接数目的上限（默认20）
        * @param connectionSecs   连接速率统计窗口长度（单位：秒）（默认1秒）
        * @param connectionTimes  在 connectionSecs 秒内允许的最大连接次数 （默认6次）
        * @param requestSecs 请求速率统计窗口长度（单位：秒）（默认1秒）
        * @param requestTimes 在秒requestSecs内允许的最大请求数量（默认40次）
        * @param checkFrequency 检查僵尸连接的频率（单位秒钟）  -1为不做检查 （默认为60秒）
        * @param connectionTimeout 连接多少秒内没有任何反应就视为僵尸连接 （单位为秒） -1为无限制 （默认60秒）
        */
        TcpServer(const unsigned long long &maxFD=1000000,const int &buffer_size=256,const size_t &finishQueue_cap=65536,const bool &security_open=true,
        const int &connectionNumLimit=20,const int &connectionSecs=1,const int &connectionTimes=6,const int &requestSecs=1,const int &requestTimes=40,
        const int &checkFrequency=60,const int &connectionTimeout=60):
        finishQueue(finishQueue_cap),sendReadyQueue(finishQueue_cap),buffer_size(buffer_size>0?static_cast<unsigned long>(buffer_size)*1024UL:256UL*1024UL),
        maxFD(maxFD>0?maxFD:1'000'000ULL),connectionLimiter(connectionNumLimit,connectionTimeout),security_open(security_open),
        serverType(1),connectionSecs(connectionSecs),connectionTimes(connectionTimes),requestSecs(requestSecs),requestTimes(requestTimes),
        checkFrequency(checkFrequency),connection_obj_fd(1){}
        /**
        * @brief 打开Tcp服务器监听程序
        * @param port 监听的端口
        * @param threads 消费者线程的数量 （默认为8）
        * @return true：打开监听程序成功 false：打开监听程序失败
        */
        bool startListen(const int &port,const int &threads=8);
        /**
        * @brief 以兼容模式启用双向 TLS（mTLS）。
        * @param cert 服务器证书链 PEM 文件。
        * @param key 与证书匹配的 PEM 私钥。
        * @param passwd 私钥密码；未加密时可传空字符串。
        * @param ca 用于验证客户端证书的 CA PEM 文件。
        * @return 配置加载成功返回 true。
        * @note 保留四参数版本的历史语义：客户端必须提供有效证书。最低协议版本为 TLS 1.2。
        *       可在运行时安全重载；新上下文只影响后续连接，现有 TLS 会话继续使用原上下文。
        */
        bool setTLS(const char *cert,const char *key,const char *passwd,const char *ca);
        /**
        * @brief 启用普通单向 TLS，适用于常见 HTTPS/WSS 服务。
        * @param cert 服务器证书链 PEM 文件。
        * @param key 与证书匹配的 PEM 私钥。
        * @param passwd 私钥密码；默认无密码。
        * @return 配置加载成功返回 true。
        */
        bool setTLS(const char *cert,const char *key,const char *passwd="");
        /**
        * @brief 启用 TLS 并显式选择客户端证书校验模式。
        * @param cert 服务器证书链 PEM 文件。
        * @param key 与证书匹配的 PEM 私钥。
        * @param passwd 私钥密码；未加密时可为空。
        * @param ca 客户端证书 CA；Optional/Required 模式必须提供。
        * @param clientAuth 客户端证书校验模式。
        * @return 配置加载成功返回 true。
        */
        bool setTLS(const char *cert,const char *key,const char *passwd,const char *ca,
                    TLSClientAuthMode clientAuth);
        /**
        * @brief 撤销TLS加密，ca证书等
        */
        void redrawTLS();
        /**
        * @brief 设置违反信息安全策略时候的返回函数
        * @note 违反信息安全策略时候的返回函数,调用完就关闭连接
        * @param fc 一个函数或函数对象，用于收到客户端消息后处理逻辑
        * -参数：TcpFDHandler &k - 和客户端连接的套接字的操作对象的引用
        *       TcpInformation &inf - 客户端信息，保存数据，处理进度，状态机信息等
        * -返回值：true：处理成功 false：处理失败 会关闭连接
        */
        void setSecuritySendBackFun(std::function<void(TcpFDHandler &k,TcpInformation &inf)> fc){this->securitySendBackFun=fc;}
        /**
        * @brief 设置全局备用函数
        * @note 找不到对应回调函数的时候会调用全局备用函数
        * @param fc 一个函数或函数对象，用于收到客户端消息后处理逻辑
        * -参数：TcpFDHandler &k - 和客户端连接的套接字的操作对象的引用
        *       TcpInformation &inf - 客户端信息，保存数据，处理进度，状态机信息等
        * -返回值：true：处理成功 false：处理失败 会关闭连接
        */
        void setGlobalSolveFunction(std::function<bool(TcpFDHandler &k,TcpInformation &inf)> fc){this->globalSolveFun=fc;}
        /**
        * @brief 设置key对应的收到客户端消息后的回调函数
        * @note 可以设置多个 ，框架会根据设置顺序依次执行回调函数；也可以设置扔入工作线程池处理的流程，注意设置不同的返回值即可。
        * @warning 可执行对象必须严格按照规定的返回值返回
        * @param key 找到对应回调函数的key
        * @param fc 一个函数或函数对象，用于收到客户端消息后处理逻辑
        * -参数：TcpFDHandler &k - 和客户端连接的套接字的操作对象的引用
        *       TcpFDInf &inf - 客户端信息，保存数据，处理进度，状态机信息等
        * -返回值：-2:处理失败并且需要关闭连接 -1:处理失败但不需要关闭连接 0:处理流程已经扔入工作线程池，需要等待处理完成 1:处理成功
        */
        void setFunction(const std::string &key,std::function<int(TcpFDHandler &k,TcpInformation &inf)> fc)
        {
            auto [it, inserted] = solveFun.try_emplace(key);
            it->second.push_back(std::move(fc));
        }
        /**
        * @brief 设置解析出key的回调函数
        * @note 根据传入的参数把key存入TcpInformation信息中的ctx哈希表。框架会根据key的值找到你注册的处理函数。
        * @warning 可执行对象必须严格按照规定的返回值返回
        * @param parseKeyFun 解析key的回调函数
        * -参数：TcpFDHandler &k - 和客户端连接的套接字的操作对象的引用
        *       TcpFDInf &inf - 客户端信息，保存数据，处理进度，状态机信息等
        * -返回值：-2:处理失败并且需要关闭连接 -1:处理失败但不需要关闭连接 0:处理流程已经扔入工作线程池，需要等待处理完成 1:处理成功
        */
        void setGetKeyFunction(std::function<int(TcpFDHandler &k,TcpInformation &inf)> parseKeyFun){this->parseKey=parseKeyFun;}
        /** 
        * @brief 停止监听
        * @warning 仅停止监听(但是套接字也已经无法接收了，它依赖于监听和消费者，所以这个函数没什么意义)
        * @warning 不要在 Reactor 同步回调中调用服务级 stopListen()/close()；应通知控制线程执行。连接级关闭请用 handler.close()。
        * @return true:停止成功  false：停止失败
        */
        bool stopListen();
        /**
        * @brief 关闭监听和所有已连接的套接字
        * @note 关闭监听和所有已经连接的套接字，已经注册的回调函数和tls不会删除和redraw
        * @note 会阻塞等待直到全部关闭完成
        * @note 默认先按 setGracefulShutdownTimeout() 配置排空在途 Worker 与发送队列，再强制回收。
        * @return true：关闭成功 false：关闭失败
        */
        virtual bool close();
        /**
        * @brief 关闭某个套接字的连接
        * @param fd 需要关闭的套接字
        * @note 线程安全：服务运行期间从 Reactor 外调用时只提交关闭请求，实际 fd/TLS 释放仍由 Reactor 完成。
        * @return true：关闭成功 false：关闭失败
        */
        virtual bool close(const int &fd);
        /**
         * @brief 设置“连接速率限流”所使用的策略。
         *
         * @param type 策略类型，见 @ref stt::security::RateLimitType 说明。
         * @note 不调用本函数时，默认策略为 @ref stt::security::RateLimitType::Cooldown 。
         */
        void setConnectStrategy(const stt::security::RateLimitType &type){this->connectionLimiter.setConnectStrategy(type);}
        /**
         * @brief 设置“IP 级请求限流”所使用的策略。
         *
         * @param type 策略类型，见 @ref stt::security::RateLimitType 说明。
         * @note 不调用本函数时，默认策略为 @ref stt::security::RateLimitType::SlidingWindow 。
         */
        void setRequestStrategy(const stt::security::RateLimitType &type){this->connectionLimiter.setRequestStrategy(type);}
        /**
         * @brief 设置“path 级请求限流”所使用的策略。
         * 
         * @param type 策略类型，见 @ref stt::security::RateLimitType 说明。
         * @note 不调用本函数时，默认策略为 @ref stt::security::RateLimitType::SlidingWindow 。
         * @note 这个path和parseKeyFun解析出来的key是一个东西
         */
        void setPathStrategy(const stt::security::RateLimitType &type){this->connectionLimiter.setPathStrategy(type);}
        /**
         * @brief 设置某个路径的额外限流规则（path 级）。
         *
         * @note 这个path和parseKeyFun解析出来的key是一个东西
         * @param path 需要额外限流的路径，例如 "/login"、"/register"。
         * @param times 在 secs 秒内允许的最大请求次数。
         * @param secs 统计窗口长度（秒）。
         *
         * @note setPathLimit 配置的是 @b 额外规则：
         * - allowRequest 调用时传入的 (times, secs) 仍然会作为 IP 级规则先执行；
         * - 若 path 命中此配置，则再执行 path 级规则；
         * - 两者为 AND 关系：任何一层失败即拒绝。
         */
        void setPathLimit(const std::string &path, const int &times, const int &secs){this->connectionLimiter.setPathLimit(path,times,secs);}
        /**
        * @brief 设置关闭tcp连接之后调用的函数
        */
        void setCloseFun(std::function<void(const int &fd)> closeFun){this->closeFun=closeFun;}
        /**
        * @brief 设置每连接待发送数据高水位。
        * @param bytes 最大待发送字节数，必须大于 0；默认 4 MiB。
        * @note 应在 startListen() 前设置。超过后发送返回 -101，并请求关闭慢客户端，防止无界内存增长。
        */
        void setMaxPendingWriteBytes(const size_t bytes){if(bytes>0)maxPendingWriteBytes=bytes;}
        /**
        * @brief 设置 Reactor 单次处理一个连接的最大发送字节数。
        * @param bytes 单次预算，必须大于 0；默认 256 KiB。
        * @note 应在 startListen() 前设置，用于限制大响应独占 Reactor，改善连接间公平性。
        */
        void setWriteBudgetPerEvent(const size_t bytes){if(bytes>0)writeBudgetPerEvent=bytes;}
        /**
        * @brief 设置 HTTP 请求和 WebSocket 握手头上限。
        * @param bytes 最大请求头字节数，最小接受值为 1024；默认 64 KiB。
        * @note 应在 startListen() 前设置，实际值还受服务器总接收缓冲上限约束。
        */
        void setMaxHttpHeaderBytes(const size_t bytes){if(bytes>=1024)maxHttpHeaderBytes=bytes;}
        /**
        * @brief 设置 WorkerPool 尚未开始执行的任务上限。
        * @param tasks 最大排队任务数，必须大于 0；默认 65536。
        * @note 应在 startListen() 前设置。达到上限时新任务会被拒绝并按失败结果返回 Reactor，避免突发流量耗尽内存。
        */
        void setMaxPendingWorkerTasks(const size_t tasks){if(tasks>0)maxPendingWorkerTasks=tasks;}
        /**
        * @brief 设置单次 Reactor 唤醒处理的跨线程消息预算。
        * @param workerCompletions 单次最多处理的 Worker 完成消息数。
        * @param sendReadyNotifications 单次最多处理的待发送连接通知数。
        * @note 两个值必须大于 0；默认分别为 2048 和 256。限制预算可防止完成队列在突发流量下长期饿死网络事件。
        */
        void setReactorMessageBudgets(const size_t workerCompletions,const size_t sendReadyNotifications)
        {
            if(workerCompletions>0)workerCompletionBudgetPerWake=workerCompletions;
            if(sendReadyNotifications>0)sendReadyBudgetPerWake=sendReadyNotifications;
        }
        /**
        * @brief 设置新接收 TCP 连接的常用套接字参数。
        * @param options TCP_NODELAY、keepalive、收发缓冲、deferred accept、Fast Open 及 backlog 配置。
        * @note 应在 startListen() 前设置；无效的缓冲区值会按 0（系统默认）处理。
        */
        void setSocketOptions(const ServerSocketOptions &options)
        {
            socketOptions=options;
            if(socketOptions.receive_buffer_bytes<0)socketOptions.receive_buffer_bytes=0;
            if(socketOptions.send_buffer_bytes<0)socketOptions.send_buffer_bytes=0;
            if(socketOptions.keep_alive_idle_seconds<0)socketOptions.keep_alive_idle_seconds=0;
            if(socketOptions.keep_alive_interval_seconds<0)socketOptions.keep_alive_interval_seconds=0;
            if(socketOptions.keep_alive_probe_count<0)socketOptions.keep_alive_probe_count=0;
            if(socketOptions.defer_accept_seconds<0)socketOptions.defer_accept_seconds=0;
            if(socketOptions.fast_open_queue<0)socketOptions.fast_open_queue=0;
            if(socketOptions.listen_backlog<0)socketOptions.listen_backlog=0;
        }
        /**
        * @brief 设置 close()/stopListen() 等待在途请求和发送队列排空的最长时间。
        * @param milliseconds 超时毫秒数；默认 5000。设置为 0 表示立即强制停止。
        * @note 超时后框架会丢弃未开始的 Worker 任务并强制关闭网络资源；已经运行的用户任务仍必须自行返回。
        *       超时次数可从 getMetrics() 获取。
        */
        void setGracefulShutdownTimeout(const size_t milliseconds){gracefulShutdownTimeoutMs=milliseconds;}
        /**
        * @brief 获取服务器运行指标快照。
        * @return 包含连接、TLS、HTTP、发送队列及溢出计数的值对象。
        * @note 线程安全、无锁；累计计数从服务器对象构造后开始，不会因 stopListen() 清零。
        */
        ServerMetricsSnapshot getMetrics() const noexcept
        {
            return {
                metricAcceptedConnections.load(std::memory_order_relaxed),
                metricActiveConnections.load(std::memory_order_relaxed),
                metricClosedConnections.load(std::memory_order_relaxed),
                metricRejectedConnections.load(std::memory_order_relaxed),
                metricAcceptErrors.load(std::memory_order_relaxed),
                metricTLSHandshakeFailures.load(std::memory_order_relaxed),
                metricParsedHttpRequests.load(std::memory_order_relaxed),
                metricQueuedWriteBytes.load(std::memory_order_relaxed),
                metricSentBytes.load(std::memory_order_relaxed),
                metricPendingWriteBytes.load(std::memory_order_relaxed),
                metricPeakPendingWriteBytes.load(std::memory_order_relaxed),
                metricPeakPendingWorkerTasks.load(std::memory_order_relaxed),
                metricWriteOverflows.load(std::memory_order_relaxed),
                metricWorkerQueueOverflows.load(std::memory_order_relaxed),
                metricSendReadyQueueOverflows.load(std::memory_order_relaxed),
                metricWorkerTaskRejections.load(std::memory_order_relaxed),
                metricReactorWakeups.load(std::memory_order_relaxed),
                metricReactorWakeupsCoalesced.load(std::memory_order_relaxed),
                metricWriteSyscalls.load(std::memory_order_relaxed),
                metricBatchedWriteSyscalls.load(std::memory_order_relaxed),
                metricBatchedWriteBuffers.load(std::memory_order_relaxed),
                metricIdleTimeoutChecks.load(std::memory_order_relaxed),
                metricIdleTimeoutCloses.load(std::memory_order_relaxed),
                metricGracefulShutdownTimeouts.load(std::memory_order_relaxed)
            };
        }
    public:
        /**
        * @brief 返回对象的监听状态
        * @return true:正在监听  false：没有在监听
        */
        bool isListen(){return flag.load(std::memory_order_acquire);}
        /**
        * @brief 获取当前实际监听端口。
        * @return 成功启动后返回实际端口；允许 startListen(0, ...) 由内核选择空闲端口。尚未启动时返回 -1。
        */
        int getListenPort() const noexcept{return port.load(std::memory_order_acquire);}
        /**
        * @brief 查询和服务端的连接，传入套接字，返回加密的SSL句柄
        * @warning 仅供诊断；服务运行期间不得在 Reactor 外调用 SSL_read/SSL_write/SSL_shutdown。
        *          发送数据应使用 handler API，否则会破坏 TLS 会话的单线程所有权。
        * @return 返回加密的SSL指针； 如果不存在此fd或者没有加密 返回nullptr
        */
        SSL* getSSL(const int &fd);
        /**
        * @brief TcpServer 类的析构函数
        * @note 会调用close函数关闭
        */
        virtual ~TcpServer(){close();redrawTLS();}
    };

    
    
    /**
    * @brief Http/HttpServer 服务端操作类
    * @note 支持http/1.0 1.1
    * @see @ref quick_http "HTTP 最小示例"
    */
    class HttpServer:public TcpServer
    {
    private:
        std::function<void(HttpServerFDHandler &k,HttpRequestInformation &inf)> securitySendBackFun=[](HttpServerFDHandler &,HttpRequestInformation &)->void
        {};
        std::vector<std::function<int(HttpServerFDHandler &k,HttpRequestInformation &inf)>> globalSolveFun;
        //std::function<bool(HttpServerFDHandler &k,HttpRequestInformation &inf)> globalSolveFun={};
        std::unordered_map<std::string,std::vector<std::function<int(HttpServerFDHandler &k,HttpRequestInformation &inf)>>> solveFun;
        std::function<int(HttpServerFDHandler &k,HttpRequestInformation &inf)> parseKey=[](HttpServerFDHandler &,HttpRequestInformation &inf)->int
        {inf.ctx["key"]=inf.loc;return 1;};
        //std::function<bool(const HttpRequestInformation &inf,HttpServerFDHandler &k)> fc;
        //HttpRequestInformation *HttpInf;
        std::unordered_map<int,HttpRequestInformation> httpinf;
    private:
        //void consumer(const int &threadID);
        //inline void handler(const int &fd);
        void handler_netevent(const int &fd) override;
        void handler_workerevent(WorkerMessage message) override;
        void handleHeartbeat() override {}
    public:
        using TcpServer::close;
        /**
        * @brief 把一个任务放入工作线程池由工作线程完成
        * @note 应当把速度慢、会阻塞的io任务放入工作线程池
        * @warning 可执行对象必须严格按照规定的返回值返回
        * @param fun 放入工作线程池的可执行对象
        * -参数：HttpServerFDHandler &k - 和客户端连接的套接字的操作对象的引用
        *       HttpRequestInformation &inf - 客户端信息，保存数据，处理进度，状态机信息等
        * -返回值：-2:处理失败并且需要关闭连接 -1:处理失败但不需要关闭连接 1:处理成功
        * @param k 和客户端连接的套接字的操作对象的引用
        * @param inf 客户端信息的引用，保存数据，处理进度，状态机信息等
        * @note 本函数仅应从该连接的 Reactor 回调中调用。过载拒绝会按失败完成消息关闭连接，并计入 worker_task_rejections。
        */
        void putTask(const std::function<int(HttpServerFDHandler &k,HttpRequestInformation &inf)> &fun,HttpServerFDHandler &k,HttpRequestInformation &inf);
         
        /**
        * @brief 构造函数，默认是允许最大1000000个连接，每个连接接收缓冲区最大为256kb，启用安全模块。
        * @note 打开安全模块会对性能有影响
        * @param maxFD 服务对象的最大接受连接数 默认为1000000
        * @param buffer_size 同一个连接允许传输的最大数据量（单位为kb） 默认为256kb
        * @param finishQueue_cap Worker 完成队列（Worker → Reactor）的容量，必须为 2 的幂。
        * @note 建议容量不小于“峰值完成速率 × Reactor 最坏暂停时间”；环形队列满时进入带锁后备队列。
        * @param security_open true:开启安全模块 false：关闭安全模块 （默认为开启）
        * @param connectionNumLimit 同一个ip连接数目的上限（默认10）
        * @param connectionSecs   连接速率统计窗口长度（单位：秒）（默认1秒）
        * @param connectionTimes  在 connectionSecs 秒内允许的最大连接次数 （默认3次）
        * @param requestSecs 请求速率统计窗口长度（单位：秒）（默认1秒）
        * @param requestTimes 在秒requestSecs内允许的最大请求数量（默认20次）
        * @param checkFrequency 检查僵尸连接的频率（单位：秒）  -1为不做检查 （默认为30秒）
        * @param connectionTimeout 连接多少秒内没有任何反应就视为僵尸连接 （单位为秒） -1为无限制 （默认30秒）
        */
        HttpServer(const unsigned long long &maxFD=1000000,const int &buffer_size=256,const size_t &finishQueue_cap=65536,const bool &security_open=true,
        const int &connectionNumLimit=10,const int &connectionSecs=1,const int &connectionTimes=3,const int &requestSecs=1,const int &requestTimes=20,
        const int &checkFrequency=30,const int &connectionTimeout=30):TcpServer(
              maxFD,
              buffer_size,
              finishQueue_cap,
              security_open,
              connectionNumLimit,
              connectionSecs,
              connectionTimes,
              requestSecs,
              requestTimes,
              checkFrequency,
              connectionTimeout
          ){serverType=2;}
        /**
        * @brief 设置违反信息安全策略时候的返回函数
        * @note 违反信息安全策略时候的返回函数,调用完就关闭连接
        * @param fc 一个函数或函数对象，用于收到客户端消息后处理逻辑
        * -参数：HttpServerFDHandler &k - 和客户端连接的套接字的操作对象的引用
        *       HttpRequestInformation &inf - 客户端信息，保存数据，处理进度，状态机信息等
        * -返回值：true：处理成功 false：处理失败 会关闭连接
        */
        void setSecuritySendBackFun(std::function<void(HttpServerFDHandler &k,HttpRequestInformation &inf)> fc){this->securitySendBackFun=fc;}
        /**
        * @brief 设置全局备用函数
        * @note 找不到对应回调函数的时候会调用全局备用函数.可以设置多个 ，框架会根据设置顺序依次执行回调函数；也可以设置扔入工作线程池处理的流程，注意设置不同的返回值即可。
        * @param fc 一个函数或函数对象，用于收到客户端消息后处理逻辑
        * -参数：HttpServerFDHandler &k - 和客户端连接的套接字的操作对象的引用
        *       HttpRequestInformation &inf - 客户端信息，保存数据，处理进度，状态机信息等
        * * -返回值：-2:处理失败并且需要关闭连接 -1:处理失败但不需要关闭连接 0:处理流程已经扔入工作线程池，需要等待处理完成 1:处理成功
        */
        void setGlobalSolveFunction(std::function<int(HttpServerFDHandler &k,HttpRequestInformation &inf)> fc){globalSolveFun.push_back(std::move(fc));}
        /**
        * @brief 设置key对应的收到客户端消息后的回调函数
        * @note 可以设置多个 ，框架会根据设置顺序依次执行回调函数；也可以设置扔入工作线程池处理的流程，注意设置不同的返回值即可。
        * @warning 可执行对象必须严格按照规定的返回值返回
        * @param key 找到对应回调函数的 key。
        * @param fc 一个函数或函数对象，用于收到客户端消息后处理逻辑
        * -参数：HttpServerFDHandler &k - 和客户端连接的套接字的操作对象的引用
        *       HttpRequestInformation &inf - 客户端信息，保存数据，处理进度，状态机信息等
        * -返回值：-2:处理失败并且需要关闭连接 -1:处理失败但不需要关闭连接 0:处理流程已经扔入工作线程池，需要等待处理完成 1:处理成功
        * @code httpserver->setFunction("/ping",[](HttpServerFDHandler &k,HttpRequestInformation &inf)->int
	                {
		                k.sendBack("pong");
		                return 1;
	                });
        * @endcode
        * @code httpserver->setFunction("/ping",[](HttpServerFDHandler &k,HttpRequestInformation &inf)->int
	                {
		                httpserver->putTask([](HttpServerFDHandler &k,HttpRequestInformation &inf)->int
		                {
			                k.sendBack("pong");
			                return 1;
		                },k,inf);
		                return 0;
	                });
        * @endcode
        */
        void setFunction(const std::string &key,std::function<int(HttpServerFDHandler &k,HttpRequestInformation &inf)> fc)
        {
            auto [it, inserted] = solveFun.try_emplace(key);
            it->second.push_back(std::move(fc));
        }
        /**
        * @brief 设置解析出key的回调函数
        * @note 根据传入的参数把key存入TcpInformation信息中的ctx哈希表。框架会根据key的值找到你注册的处理函数。
        * @warning 可执行对象必须严格按照规定的返回值返回
        * @param parseKeyFun 解析key的回调函数
        * -参数：HttpServerFDHandler &k - 和客户端连接的套接字的操作对象的引用
        *       HttpRequestInformation &inf - 客户端信息，保存数据，处理进度，状态机信息等
        * -返回值：-2:处理失败并且需要关闭连接 -1:处理失败但不需要关闭连接 0:处理流程已经扔入工作线程池，需要等待处理完成 1:处理成功
        * @code httpserver->setGetKeyFunction([](HttpServerFDHandler &k,HttpRequestInformation &inf)->int
	                {
			            inf.ctx["key"]=inf.loc;
			            return 1;
	                });
        * @endcode
        */
        void setGetKeyFunction(std::function<int(HttpServerFDHandler &k,HttpRequestInformation &inf)> parseKeyFun){this->parseKey=parseKeyFun;}
        /**
        * @brief 打开Http服务器监听程序
        * @param port 监听的端口
        * @param threads 消费者线程的数量 （默认为8）
        * @return true：打开监听程序成功 false：打开监听程序失败
        */
        bool startListen(const int &port,const int &threads=8)
        {
            return TcpServer::startListen(port,threads);
        }
        bool close() override;
        bool close(const int &fd) override;
        /**
        * @brief 析构函数
        */
        ~HttpServer()
        {
            close();
        }
    };
    /**
    * @brief WebSocket协议的操作类
    * 仅传入套接字，然后使用这个类进行WebSocket的操作
    */
    class WebSocketServerFDHandler:public TcpFDHandler
    {
    public:
        /**
        * @brief 初始化对象，传入套接字等参数
        * @param fd 套接字
        * @param ssl TLS加密的SSL句柄(默认为nullptr)
        * @param flag1 true：启用非阻塞模式  false：启用阻塞模式 （默认为false，即启用阻塞模式）
        * @param flag2 true：启用SO_REUSEADDR模式  false：不启用SO_REUSEADDR模式 （默认为true，即启用SO_REUSEADDR模式）
        */
        void setFD(const int &fd,SSL *ssl=nullptr,const bool &flag1=false,const bool &flag2=true){TcpFDHandler::setFD(fd,ssl,flag1,flag2);}
        /**
        * @brief 获取一条websocket消息
        * @param Tcpinf 保存底层tcp状态的信息
        * @param Websocketinf 保存websocket协议状态信息
        * @param buffer_size 服务器定义的解析缓冲区的大小（单位为字节)
        * @param ii 记录解析次数，一些场合用得到 默认为1
        * @return
        * -1：获取失败
        * 0：一般报文
        * 1：关闭帧
        * 2：心跳确认报文
        * 3：心跳报文
        * 4: 等待数据
        * @note 服务端严格要求客户端帧带 mask，校验 RSV/opcode/最小长度、控制帧、关闭码和 UTF-8，
        *       并支持分片消息之间穿插 ping/pong。累计消息不会超过 buffer_size。
        */
        int getMessage(TcpFDInf &Tcpinf,WebSocketFDInformation &Websocketinf,const unsigned long &buffer_size,const int &ii=1);
        /**
        * @brief 发送一条websocket信息
        * @param msg 需要发送的websocket信息
        * @param type 指定消息类型的自定义字段（通常是 WebSocket 帧的 opcode）
        *        约定格式为 `"1000" + type`，其中：
        *        - "0001" 表示文本帧（Text Frame）
        *        - "0010" 表示二进制帧（Binary Frame）
        *        - "1000" 表示连接关闭（Close Frame）
        *        - "1001" 表示 Ping 帧
        *        - "1010" 表示 Pong 帧
        *        请根据内部约定使用，默认使用 text（文本帧）
        * 
        * @return true：帧已发送，或在服务器回调场景中已被有界队列接受；false：类型、控制帧长度或发送失败。
        * @note 服务端帧不会加 mask；Ping、Pong、Close payload 最大 125 字节。
        */
        bool sendMessage(const std::string &msg,const std::string &type="0001");
       
    };
    
    /**
    * @brief WebSocketServer服务端操作类
    * @see @ref quick_websocket "WebSocket Echo 最小示例"
    */
    class WebSocketServer:public TcpServer
    {
    private:
        std::unordered_map<int,WebSocketFDInformation> wbclientfd;
        std::mutex websocketRegistryMutex;
        std::unordered_map<int,uint64_t> websocketConnections;
        std::unordered_set<int> websocketClosing;
        std::function<void(WebSocketServerFDHandler &k,WebSocketFDInformation &inf)> securitySendBackFun=[](WebSocketServerFDHandler &,WebSocketFDInformation &)->void
        {};
        //std::function<bool(const std::string &msg,WebSocketServer &k,const WebSocketFDInformation &inf)> fc=[](const std::string &message,WebSocketServer &k,const WebSocketFDInformation &inf)->bool
        //{std::cout<<"收到: "<<message<<std::endl;return true;};
        std::function<bool(WebSocketFDInformation &k)> fcc=[](WebSocketFDInformation &)
        {return true;};
        std::function<bool(WebSocketServerFDHandler &k,WebSocketFDInformation &inf)> fccc=[](WebSocketServerFDHandler &,WebSocketFDInformation &)->bool
        {
            return true;
        };
        std::function<bool(WebSocketServerFDHandler &k,WebSocketFDInformation &inf)> globalSolveFun=[](WebSocketServerFDHandler &,WebSocketFDInformation &)->bool
        {return true;};
        std::unordered_map<std::string,std::vector<std::function<int(WebSocketServerFDHandler &k,WebSocketFDInformation &inf)>>> solveFun;
        std::function<int(WebSocketServerFDHandler &k,WebSocketFDInformation &inf)> parseKey=[](WebSocketServerFDHandler &,WebSocketFDInformation &inf)->int
        {inf.ctx["key"]=inf.message;return 1;};
        int seca=20*60;
        int secb=30;

    private:
        void handler_netevent(const int &fd) override;
        void handler_workerevent(WorkerMessage message) override;
        //void consumer(const int &threadID);
        //inline void handler(const int &fd);
        void closeAck(const int &fd,const std::string &closeCodeAndMessage);
        void closeAck(const int &fd,const short &code=1000,const std::string &message="bye");
        
        void handleHeartbeat() override;
        void onConnectionClosed(const int &fd) override;
        bool sendMessageForConnection(const int &fd,const uint64_t connection,const std::string &msg,const std::string &type);
        bool closeWithoutLock(const int &fd,const std::string &closeCodeAndMessage);
        bool closeWithoutLock(const int &fd,const short &code=1000,const std::string &message="bye");
    public:
        /**
        * @brief 把一个任务放入工作线程池由工作线程完成
        * @note 应当把速度慢、会阻塞的io任务放入工作线程池
        * @warning 可执行对象必须严格按照规定的返回值返回
        * @param fun 放入工作线程池的可执行对象
        * -参数：WebSocketServerFDHandler &k - 和客户端连接的套接字的操作对象的引用
        *       WebSocketFDInformation &inf - 客户端信息，保存数据，处理进度，状态机信息等
        * -返回值：-2:处理失败并且需要关闭连接 -1:处理失败但不需要关闭连接 1:处理成功
        * @param k 和客户端连接的套接字的操作对象的引用
        * @param inf 客户端信息的引用，保存数据，处理进度，状态机信息等
        * @note 本函数仅应从该连接的 Reactor 回调中调用。过载拒绝会按失败完成消息关闭连接，并计入 worker_task_rejections。
        */
         void putTask(const std::function<int(WebSocketServerFDHandler &k,WebSocketFDInformation &inf)> &fun,WebSocketServerFDHandler &k,WebSocketFDInformation &inf);
        /**
        * @brief 构造函数，默认是允许最大1000000个连接，每个连接接收缓冲区最大为256kb，启用安全模块。
        * @note 打开安全模块会对性能有影响
        * @param maxFD 服务对象的最大接受连接数 默认为1000000
        * @param buffer_size 同一个连接允许传输的最大数据量（单位为kb） 默认为256kb
        * @param finishQueue_cap Worker 完成队列（Worker → Reactor）的容量，必须为 2 的幂。
        * @note 建议容量不小于“峰值完成速率 × Reactor 最坏暂停时间”；环形队列满时进入带锁后备队列。
        * @param security_open true:开启安全模块 false：关闭安全模块 （默认为开启）
        * @param connectionNumLimit 同一个ip连接数目的上限（默认5）
        * @param connectionSecs   连接速率统计窗口长度（单位：秒）（默认10秒）
        * @param connectionTimes  在 connectionSecs 秒内允许的最大连接次数 （默认3次）
        * @param requestSecs 请求速率统计窗口长度（单位：秒）（默认1秒）
        * @param requestTimes 在秒requestSecs内允许的最大请求数量（默认10次）
        * @param checkFrequency 检查僵尸连接的频率（单位：秒）  -1为不做检查 （默认为60秒）
        * @param connectionTimeout 连接多少秒内没有任何反应就视为僵尸连接 （单位为秒） -1为无限制 （默认120秒）
        */
        WebSocketServer(const unsigned long long &maxFD=1000000,const int &buffer_size=256,const size_t &finishQueue_cap=65536,const bool &security_open=true,
        const int &connectionNumLimit=5,const int &connectionSecs=10,const int &connectionTimes=3,const int &requestSecs=1,const int &requestTimes=10,
        const int &checkFrequency=60,const int &connectionTimeout=120):TcpServer(
              maxFD,
              buffer_size,
              finishQueue_cap,
              security_open,
              connectionNumLimit,
              connectionSecs,
              connectionTimes,
              requestSecs,
              requestTimes,
              checkFrequency,
              connectionTimeout
          ){serverType=3;}
        /**
        * @brief 设置违反信息安全策略时候的返回函数
        * @note 违反信息安全策略时候的返回函数,调用完就关闭连接
        * @param fc 一个函数或函数对象，用于收到客户端消息后处理逻辑
        * -参数：WebSocketServerFDHandler &k - 和客户端连接的套接字的操作对象的引用
        *       WebSocketFDInformation &inf - 客户端信息，保存数据，处理进度，状态机信息等
        * -返回值：true：处理成功 false：处理失败 会关闭连接
        */
        void setSecuritySendBackFun(std::function<void(WebSocketServerFDHandler &k,WebSocketFDInformation &inf)> fc){this->securitySendBackFun=fc;}
        /**
        * @brief 设置全局备用函数
        * @note 找不到对应回调函数的时候会调用全局备用函数
        * @param fc 一个函数或函数对象，用于收到客户端消息后处理逻辑
        * -参数：WebSocketServerFDHandler &k - 和客户端连接的套接字的操作对象的引用
        *       WebSocketFDInformation &inf - 客户端信息，保存数据，处理进度，状态机信息等
        * -返回值：true：处理成功 false：处理失败 会关闭连接
        */
        void setGlobalSolveFunction(std::function<bool(WebSocketServerFDHandler &k,WebSocketFDInformation &inf)> fc){this->globalSolveFun=fc;}
        /**
        * @brief 设置websocket连接成功后就执行的回调函数
        * 注册一个回调函数
        * @param fccc 一个函数或函数对象，websocket连接成功后就执行
        * -参数：const WebSocketFDInformation &inf - Websocket服务端的信息
        *       WebSocketServer &k - 服务端对象的引用
        * @note 传入的函数应该有如下签名 void func(const WebSocketFDInformation &inf,WebSocketServer &k)
        */
        void setStartFunction(std::function<bool(WebSocketServerFDHandler &k,WebSocketFDInformation &inf)> fccc){this->fccc=fccc;}
        /**
        * @brief 设置websocket握手阶段的检查函数，只有检查通过才执行后续握手
        * 注册一个回调函数
        * @param fcc 一个函数或函数对象，用于检查是否接受 WebSocket 握手
        * -参数：const WebSocketFDInformation &inf - Websocket服务端的信息
        * -返回值 ： true：检查通过  false：检查不通过
        * @note 传入的函数应该有如下签名 bool func(const WebSocketFDInformation &k)
        * @note 只有检查通过才执行后续握手，否则断开连接
        */
        void setJudgeFunction(std::function<bool(WebSocketFDInformation &k)> fcc){this->fcc=fcc;}
        /**
        * @brief 设置key对应的收到客户端消息后的回调函数
        * @note 可以设置多个 ，框架会根据设置顺序依次执行回调函数；也可以设置扔入工作线程池处理的流程，注意设置不同的返回值即可。
        * @warning 可执行对象必须严格按照规定的返回值返回
        * @param key 找到对应回调函数的key
        * @param fc 一个函数或函数对象，用于收到客户端消息后处理逻辑
        * -参数：WebSocketServerFDHandler &k - 和客户端连接的套接字的操作对象的引用
        *       WebSocketFDInformation &inf - 客户端信息，保存数据，处理进度，状态机信息等
        * -返回值：-2:处理失败并且需要关闭连接 -1:处理失败但不需要关闭连接 0:处理流程已经扔入工作线程池，需要等待处理完成 1:处理成功
        * @code s->setFunction("ping",[](WebSocketServerFDHandler &k,WebSocketFDInformation &inf)->int
	                {
		                k.sendMessage("pong");
		                return 1;
	                });
        * @endcode
        */
        void setFunction(const std::string &key,std::function<int(WebSocketServerFDHandler &k,WebSocketFDInformation &inf)> fc)
        {
            auto [it, inserted] = solveFun.try_emplace(key);
            it->second.push_back(std::move(fc));
        }
        /**
        * @brief 设置解析出key的回调函数
        * @note 根据传入的参数把key存入TcpInformation信息中的ctx哈希表。框架会根据key的值找到你注册的处理函数。
        * @warning 可执行对象必须严格按照规定的返回值返回
        * @param parseKeyFun 解析key的回调函数
        * -参数：WebSocketServerFDHandler &k - 和客户端连接的套接字的操作对象的引用
        *       WebSocketFDInformation &inf - 客户端信息，保存数据，处理进度，状态机信息等
        * -返回值：-2:处理失败并且需要关闭连接 -1:处理失败但不需要关闭连接 0:处理流程已经扔入工作线程池，需要等待处理完成 1:处理成功
        * @code s->setGetKeyFunction([](WebSocketServerFDHandler &k,WebSocketFDInformation &inf)->int
	                {
			            inf.ctx["key"]=inf.message;
			            return 1;
		            });
        * @endcode
        */
        void setGetKeyFunction(std::function<int(WebSocketServerFDHandler &k,WebSocketFDInformation &inf)> parseKeyFun){this->parseKey=parseKeyFun;}
        //void setFunction(std::function<bool(const std::string &msg,WebSocketServer &k,const WebSocketFDInformation &inf)> fc){this->fc=fc;}
        /**
        * @brief 设置心跳时间
        * @param seca 心跳时间 单位为分钟。不设置默认为20分钟
        */
        void setTimeOutTime(const int &seca){this->seca=seca*60;}
        /**
        * @brief 设置发送心跳后的等待时间
        * @param secb 发送心跳后的等待时间 单位为秒 不设置默认为30秒
        * @note 如果超过这个时间对端没有反应 关闭连接
        */
        void setHBTimeOutTime(const int &secb){this->secb=secb;}
        /**
        * @brief 发送关闭帧关闭对应套接字的 WebSocket 连接（简化方式）
        * 
        * 传入套接字fd然后关闭连接
         * 直接传入编码后的关闭 payload，其中前两字节为关闭码（big-endian），
         * 后续为 UTF-8 编码的关闭原因描述，用于简化调用。
        * 
        * @param fd 套接字fd
        * @param closeCodeAndMessage 编码后的关闭帧 payload（2 字节关闭码 + 可选消息）
        * @return true：关闭成功  false：关闭失败
        */
        bool closeFD(const int &fd,const std::string &closeCodeAndMessage);
        /**
        * @brief 发送关闭帧关闭对应套接字的 WebSocket 连接（标准方式）
        * 
        * 传入套接字fd然后关闭连接
        * 构建符合 RFC 6455 的关闭帧（opcode = 0x8），帧 payload 包含关闭码（2 字节）与可选关闭原因字符串。
        * 
        * @param fd 套接字fd
        * @param code WebSocket 关闭码，常见包括：
        * - 1000：正常关闭（Normal Closure）
        * - 1001：终端离开（Going Away）
        * - 1002：协议错误（Protocol Error）
        * - 1003：不支持的数据类型（Unsupported Data）
        * - 1006：非正常关闭（无关闭帧，程序内部使用）
        * - 1008：策略违规（Policy Violation）
        * - 1011：服务器内部错误（Internal Error）
        * 
        * @param message 可选关闭原因，供调试或日志记录用
        * @return true：关闭成功 false：关闭失败
        */
        bool closeFD(const int &fd,const short &code=1000,const std::string &message="bye");
        /**
        * @brief 发送 WebSocket 消息给某一个客户端
        * 
        * 根据 WebSocket 协议，封装并提交一条服务端数据帧（服务端帧不使用掩码），
        * 支持根据 payload 长度自动选择帧格式：
        * - payload <= 125 字节：使用 1 字节长度
        * - 126 <= payload <= 65535：使用 2 字节扩展长度（附加 126 标记）
        * - payload > 65535：使用 8 字节扩展长度（附加 127 标记）
        * 
        * @param fd 和客户端连接的套接字
        * @param msg 要发送的消息内容（已编码为文本或二进制）
        * @param type 指定消息类型的自定义字段（通常是 WebSocket 帧的 opcode）
        *        约定格式为 `"1000" + type`，其中：
        *        - "0001" 表示文本帧（Text Frame）
        *        - "0010" 表示二进制帧（Binary Frame）
        *        - "1000" 表示连接关闭（Close Frame）
        *        - "1001" 表示 Ping 帧
        *        - "1010" 表示 Pong 帧
        *        请根据内部约定使用，默认使用 text（文本帧）
        * 
        * @return true 帧已被该连接的有界发送队列接受。
        * @return false 连接无效、代次已变化、类型/控制帧长度无效或队列超过高水位。
        */
        bool sendMessage(const int &fd,const std::string &msg,const std::string &type="0001");
        /**
        * @brief 关闭监听和所有连接
        * @note 会阻塞直到全部关闭
        */
        bool close() override;
        /**
        * @brief 关闭某个套接字的连接
        * @note 直接关闭底层 TCP 连接；需要协议级关闭握手时使用 closeFD()。
        */
        bool close(const int &fd) override;
        /**
        * @brief 打开Websocket服务器监听程序
        * @param port 监听的端口
        * @param threads 消费者线程的数量 （默认为8）
        * @return true：打开监听程序成功 false：打开监听程序失败
        */
        bool startListen(const int &port,const int &threads=8)
        {
            //std::thread(&WebSocketServer::HB,this).detach();

            return TcpServer::startListen(port,threads);
        }
        /**
        * @brief 广播发送 WebSocket 消息
        * 
        * 给全体客户端广播发送消息
        * 根据 WebSocket 协议，为每个仍有效的连接提交一条不带掩码的服务端帧，
        * 支持根据 payload 长度自动选择帧格式：
        * - payload <= 125 字节：使用 1 字节长度
        * - 126 <= payload <= 65535：使用 2 字节扩展长度（附加 126 标记）
        * - payload > 65535：使用 8 字节扩展长度（附加 127 标记）
        * 
        * @param msg 要发送的消息内容（已编码为文本或二进制）
        * @param type 指定消息类型的自定义字段（通常是 WebSocket 帧的 opcode）
        *        约定格式为 `"1000" + type`，其中：
        *        - "0001" 表示文本帧（Text Frame）
        *        - "0010" 表示二进制帧（Binary Frame）
        *        - "1000" 表示连接关闭（Close Frame）
        *        - "1001" 表示 Ping 帧
        *        - "1010" 表示 Pong 帧
        *        请根据内部约定使用，默认使用 text（文本帧）
        * 
        * @note 单个慢客户端超过发送高水位时会被隔离关闭，不会让广播线程直接阻塞在 socket/SSL 写入上。
        */
        void sendMessage(const std::string &msg,const std::string &type="0001");
        /**
        * @brief  WebSocketServer的析构函数
        * @note 销毁对象的时候会阻塞直到全部连接和监听等全部关闭
        */
        ~WebSocketServer(){ close(); }
    };

    /**
    * @brief UDP操作的类
    * 传入套接字进行UDP协议的操作
    */
    class UdpFDHandler
    {
    protected:
        int fd=-1;
        bool flag1=false;
        bool flag2=false;
        int sec=-1;
    public:
        /**
        * @brief 设置fd
        * @param fd 需要传入的套接字fd
        * @param flag1 true：设置非阻塞模式  false：设置阻塞模式 （默认为阻塞模式）
        * @param sec 设置阻塞超时时间（秒） （默认为-1 即为无限等待）
        * @param flag2 true：设置SO_REUSEADDR模式 false：不设置SO_REUSEADDR模式
        */
        void setFD(const int &fd,const bool &flag1=false,const int &sec=-1,const bool &flag2=false);
        /**
        * @brief 设置为阻塞模式
        * @param sec 阻塞超时时间 阻塞超过这个时间就不会再阻塞了 默认为-1 即无限等待
        */
        void blockSet(const int &sec=-1);
        /**
        * @brief 设置为非阻塞模式
        */
        void unblockSet();
        /**
        * @brief 设置SO_REUSEADDR模式
        * @return true：设置成功  false：设置失败
        */
        bool multiUseSet();
        /**
        * @brief 返回fd
        */
        int getFD(){return fd;}
        /**
        * @brief 置空对象，关闭套接字
        * @param cle true：置空对象并且关闭套接字  false：仅仅清空对象，不关闭套接字
        */
        void close(const bool &cle=true);
        /**
        * @brief 向目标发送字符串数据。
        *
        * @param data 要发送的数据内容（std::string 类型）。
        * @param ip 目标 IPv4/域名。
        * @param port 目标 UDP 端口。
        * @param block 是否以阻塞模式发送（默认 true）。
        *              - true：会阻塞直到全部数据发送成功除非出错了（无论 socket 是阻塞或非阻塞）；
        *              - false：阻塞与否取决于套接字状态。
        * 
        * @return
        * - 返回值 > 0：成功发送的字节数；
        * - 返回值 <= 0：发送失败；
        *   - -98：目标错误
        *   - -99：对象未绑定 socket；
        *   - -100：非阻塞模式下，发送缓冲区已满。
        *
        * @note 若 block 为 true，会持续阻塞直到全部数据发送完毕除非出错了（无论 socket 是阻塞或非阻塞），适合希望确保完整发送的场景。
        * 若 block 为 false，阻塞与否取决于套接字状态。返回值可能小于 length，需手动处理剩余数据。
        */
        int sendData(const std::string &data,const std::string &ip,const int &port,const bool &block=true);
        /**
        * @brief 向目标发送指定长度的二进制数据。
        *
        * @param data 指向要发送的数据缓冲区。
        * @param length 数据长度（字节）。
        * @param ip 目标 IPv4/域名。
        * @param port 目标 UDP 端口。
        * @param block 是否以阻塞模式发送（默认 true）。
         *              - true：会阻塞直到全部数据发送成功除非出错了（无论 socket 是阻塞或非阻塞）；
         *              - false：阻塞与否取决于套接字状态。
        * 
        * @return
        * - 返回值 > 0：成功发送的字节数；
        * - 返回值 <= 0：发送失败；
        *   - -98：目标错误
        *   - -99：对象未绑定 socket；
        *   - -100：非阻塞模式下，发送缓冲区已满。
        *
        * @note 若 block 为 true，会持续阻塞直到全部数据发送完毕除非出错了（无论 socket 是阻塞或非阻塞）。
        *       若 block 为 false，阻塞与否取决于套接字状态。返回值可能小于 length，需手动处理剩余数据。
        */
        int sendData(const char *data,const uint64_t &length,const std::string &ip,const int &port,const bool &block=true);
        /**
        * @brief 接收一次数据到string字符串容器
        * @param data 接收数据的数据容器（string类型）
        * @param length 最大接收长度
        * @param ip 记录发送来源的ip
        * @param port 记录发送来源的断开
        * @return 
        * - 返回值 > 0：成功接收的字节数；
        * - 返回值 = 0：连接已关闭；
        * - 返回值 < 0：接收失败；
        *   - -99：对象未绑定 socket；
        *   - -100：非阻塞模式且没有数据
        * @note 接收是否会阻塞根据fd的阻塞情况决定
        */
        int recvData(std::string &data,const uint64_t &length,std::string &ip,int &port);
        /**
        * @brief 接收一次数据到char*容器
        * @param data 接收数据的数据容器（char*类型）
        * @param length 最大接收长度
        * @param ip 记录发送来源的ip
        * @param port 记录发送来源的断开
        * @return 
        * - 返回值 > 0：成功接收的字节数；
        * - 返回值 = 0：连接已关闭；
        * - 返回值 < 0：接收失败；
        *   - -99：对象未绑定 socket；
        *   - -100：非阻塞模式且没有数据
        * @note 接收是否会阻塞根据fd的阻塞情况决定
        */
        int recvData(char *data,const uint64_t &length,std::string &ip,int &port);
        
    };
    /**
    * @brief udp客户端的操作类
    */
    class UdpClient:public UdpFDHandler
    {
    public:
        /**
        * @brief 构造函数
        * @param flag1 true：非阻塞模式 false：阻塞模式 （默认阻塞模式）
        * @param sec 设置阻塞超时时间（秒） （默认为无限等待）
        */
        UdpClient(const bool &flag1=false,const int &sec=-1);
        /**
        * @brief 销毁原来的套接字，重新创建一个客户端
        * @param flag1 true：非阻塞模式 false：阻塞模式 （默认阻塞模式）
        * @param sec 设置阻塞超时时间（秒） （默认为无限等待）
        */
        bool createFD(const bool &flag1=false,const int &sec=-1);
        /**
        * @brief 析构函数，对象生命结束会会关闭套接字
        */
        ~UdpClient(){close();}
    };
    /**
    * @brief udp服务端的操作类
    */
    class UdpServer:public UdpFDHandler
    {
    public:
        /**
        * @brief 构造函数
        * @param port 本地监听端口。
        * @param flag1 true：设置非阻塞模式  false：设置阻塞模式 （默认为阻塞模式）
        * @param sec 设置阻塞超时时间（秒） （默认为-1 即为无限等待）
        * @param flag2 true：设置SO_REUSEADDR模式 false：不设置SO_REUSEADDR模式
        */
        UdpServer(const int &port,const bool &flag1=false,const int &sec=-1,const bool &flag2=true);
        /**
        * @brief 销毁原来的套接字，重新创建一个服务端
        * @param port 本地监听端口。
        * @param flag1 true：设置非阻塞模式  false：设置阻塞模式 （默认为阻塞模式）
        * @param sec 设置阻塞超时时间（秒） （默认为-1 即为无限等待）
        * @param flag2 true：设置SO_REUSEADDR模式 false：不设置SO_REUSEADDR模式
        */
        bool createFD(const int &port,const bool &flag1=false,const int &sec=-1,const bool &flag2=true);
        /**
        * @brief 析构函数，对象生命结束会会关闭套接字
        */
        ~UdpServer(){close();}
    };
    }
    /**
    * @namespace stt::system
    * @brief 系统的设置，进程的控制，心跳监控等
    * @ingroup stt
    */
    namespace system
    {
        /**
        * @brief 初始化服务系统的类
        * - 适用于需要稳定运行的服务程序的初始化
        * - 传入的日志文件对象如果是没初始化的空的对象，系统自动在程序目录下创建server_log的文件夹并根据当前时间生成日志文件；如果是初始化了的对象，则启用当前对象下的日志文件。
        * - 日志系统一旦设置好，会生成日志文件记录服务网络程序的运行动态
        * - 日志系统的有效工作时间和传入的日志文件对象的生命周期相关
        */
        class ServerSetting
        {
        public:
            /**
            * @brief 系统的日志系统的读写日志对象的指针
            */
            static file::LogFile *logfile;
            /**
            * @brief 系统的日志系统的语言选择，默认为English
            */
            static std::string language;
        private:
            [[noreturn]] static void signalterminated() noexcept;
        public:
            /**
            * @brief 设置系统的信号
            * - 忽略 SIGPIPE，防止向已关闭 socket 写入时终止进程
            * - 保留 SIGTERM/SIGINT/SIGCHLD 的正常语义
            * - SIGSEGV/SIGABRT 等致命错误使用系统默认处理，不在不安全的 signal handler 中析构 C++ 对象
            */
            static void setExceptionHandling();
            /**
            * @brief 在创建任何工作线程前阻塞 SIGTERM 和 SIGINT。
            * @note 之后应在主线程调用 waitForTerminationSignal() 同步等待退出信号。
            */
            static bool blockTerminationSignals();
            /**
            * @brief 同步等待 SIGTERM/SIGINT，返回收到的信号，失败返回 -1。
            */
            static int waitForTerminationSignal();
            /**
            * @brief 设置日志系统的日志文件对象
            * 传入的日志文件对象如果是没初始化的空的对象，系统自动在程序目录下生成server_log文件夹并且根据当前时间生成一个日志文件记录服务程序的网络通信
            * 如果传入的日志文件对象是初始化了的对象，则启用当前对象下的日志文件。
            * @param logfile 传入日志文件对象的指针，如果这个对象没初始化，系统自动在程序目录下生成server_log文件夹并且根据当前时间生成一个日志文件。 （默认为nullptr，不设置日志文件）
            * @param language 日志文件的语言 "Chinese":中文 除此之外都会设置成英文 （默认为空 设置为英文）
            */
            static void setLogFile(file::LogFile *logfile=nullptr,const std::string &language="");
            /**
            * @brief 执行setExceptionHandling和setLogFile两个函数，完成初始化信号和日志系统
            * @param logfile 传入日志文件对象的指针，如果这个对象没初始化，系统自动在程序目录下生成server_log文件夹并且根据当前时间生成一个日志文件。 （默认为nullptr，不设置日志文件）
            * @param language 日志文件的语言 "Chinese":中文 除此之外都会设置成英文 （默认为空 设置为英文）
            */
            static void init(file::LogFile *logfile=nullptr,const std::string &language="");
        };
        
        /**
        * @brief 封装 System V 信号量的同步工具类。
        * 
        * `csemp` 提供互斥机制，支持进程间同步操作。通过封装 semget、semop、semctl 等系统调用，
        * 实现信号量的初始化、P（等待）操作、V（释放）操作、销毁及读取当前值等功能。
        * 
        * 禁用复制构造和赋值运算符，保证资源唯一。
        */
        class csemp
        {
        private:
            /**
            * @brief 用于 semctl() 系统调用的联合体参数。
            * 
            * `semun` 是 System V 接口中设置信号量属性所必需的用户定义结构。
            */
            union semun  
            {
                int val;                ///< 设置信号量的值（用于 SETVAL）
                struct semid_ds *buf;   ///< 信号量的状态缓冲区（用于 IPC_STAT, IPC_SET）
                unsigned short  *arry;  ///< 设置信号量数组的值（用于 SETALL）
            };

            int   m_semid;      ///< 信号量的 ID，由 semget 获取。
            short m_sem_flg;    ///< 信号量操作标志（如 SEM_UNDO）。

            csemp(const csemp &) = delete;             ///< 禁用拷贝构造
            csemp &operator=(const csemp &) = delete;  ///< 禁用赋值操作

        public:
            /**
            * @brief 构造函数，初始化内部状态。
            */
            csemp():m_semid(-1){}

            /**
            * @brief 初始化信号量。
            * 
            * 如果信号量已存在，则获取它；否则尝试创建并设置初值。
            * 
            * @param key 信号量的唯一键值。
            * @param value 信号量的初始值（默认1，表示互斥锁）。
            * @param sem_flg 信号量操作标志，默认 SEM_UNDO，操作会在进程终止后自动撤销。
            * @return true 成功；false 失败。
            */
            bool init(key_t key, unsigned short value = 1, short sem_flg = SEM_UNDO);

            /**
            * @brief P 操作（等待），尝试将信号量值减去 value。
            * 
            * 若当前值不足，会阻塞直到可用。
            * 
            * @param value 等待值（必须小于0，默认 -1）。
            * @return true 成功；false 失败。
            */
            bool wait(short value = -1);

            /**
            * @brief V 操作（释放），尝试将信号量值加上 value。
            * 
            * 释放资源或唤醒等待中的进程。
            * 
            * @param value 释放值（必须大于0，默认 1）。
            * @return true 成功；false 失败。
            */
            bool post(short value = 1);

            /**
            * @brief 获取信号量当前的值。
            * 
            * @return 信号量的值；失败时返回 -1。
            */
            int getvalue();

            /**
            * @brief 销毁当前信号量。
            * 
            * 一般用于持有该信号量的主进程退出前清理资源。
            * 
            * @return true 成功；false 失败。
            */
            bool destroy();

            /**
            * @brief 析构函数，不自动销毁信号量。
            */
            ~csemp();
        };

        /**
        * @brief 定义MAX_PROCESS_NAME这个宏为100,意思是进程信息中的进程名字长度不超过100个字节
        */
        #define MAX_PROCESS_NAME 100
        /**
        * @brief 定义MAX_PROCESS_INF这个宏为1000,意思是进程信息表记录的进程信息最多为1000条
        */
        #define MAX_PROCESS_INF 1000
        /**
        * @brief 定义SHARED_MEMORY_KEY这个宏为0x5095,意思是进程信息表的共享内存键值为0x5095
        */
        #define SHARED_MEMORY_KEY 0x5095
        /**
        * @brief 定义SHARED_MEMORY_LOCK_KEY这个宏为0x5095,意思是操作进程信息表的信号量的键值为0x5095
        */
        #define SHARED_MEMORY_LOCK_KEY 0x5095

        /**
        * @brief 进程信息的结构体
        */
        struct ProcessInf
        {
            /**
            * @brief 进程id
            */
            pid_t pid;
            /**
            * @brief 进程最后一次心跳时间,是时间戳
            */
            time_t lastTime;
            /**
            * @brief 进程名字
            */
            char name[MAX_PROCESS_NAME];
            /**
            * @brief 进程第一个参数
            */
            char argv0[20];
            /**
            * @brief 进程第二个参数
            */
            char argv1[20];
            /**
            * @brief 进程第三个参数
            */
            char argv2[20];
        };

        /**
        * @brief 负责进程心跳监控，调度的类
        * 用于监控服务进程，保证服务进程持续有效运行
        * 进程结束后，0x5095这一块共享内存和信号量都没有删掉
        * 目前只支持最多三个参数的进程加入监控
        * 应该自己手动在程序编写加入心跳监控系统，更新心跳，检查心跳系统的逻辑。该类只提供调用接口。
        */
        class HBSystem
        {
        private:
        
            static ProcessInf *p;
            static csemp plock;
            static bool isJoin;
        public:
            /**
            * @brief 把进程加入到心跳系统
            * @param name 进程名字的绝对路径
            * @param argv0 进程的第一个参数
            * @param argv1 进程的第二个参数
            * @param argv2 进程的第三个参数
            * @return true：加入成功  false：加入失败
            */
            bool join(const char *name,const char *argv0="",const char *argv1="",const char *argv2="");
            /**
            * @brief 更新当前进程的心跳
            * @return true：更新成功  false：更新失败
            */
            bool renew();
            /**
            * @brief 输出心跳监控系统的所有进程的信息
            */
            static void list();
            /**
            * @brief 检查心跳监控系统
            * 如果上一次心跳更新的时间和现在的时候相差大于等于sec秒，则杀死进程
            * 先发送信号15杀死进程 如果8秒后进程还存在 则发送信号9强制杀死
            * @return true：操作成功  false：操作失败
            */
            static bool HBCheck(const int &sec);
            /**
            * @brief 把当前进程从心跳系统中删除
            * @return true：操作成功 false：操作失败
            */
            bool deleteFromHBS();
            /**
            * @brief HBSystem的析构函数
            * - 把当前进程从心跳系统中删除
            */
            ~HBSystem();
        };

        /**
        * @brief 进程管理的静态工具类
        */
        class Process
        {
        private:
            static void prepareForkedChild(const bool terminateWithParent=false) noexcept
            {
                sigset_t emptySet;
                sigemptyset(&emptySet);
                pthread_sigmask(SIG_SETMASK,&emptySet,nullptr);
                struct sigaction defaultAction{};
                sigemptyset(&defaultAction.sa_mask);
                defaultAction.sa_handler=SIG_DFL;
                for(const int childSignal:{SIGTERM,SIGINT,SIGHUP,SIGQUIT,SIGCHLD,SIGPIPE})
                    sigaction(childSignal,&defaultAction,nullptr);
#ifdef __linux__
                if(terminateWithParent)
                {
                    prctl(PR_SET_PDEATHSIG,SIGTERM);
                    if(getppid()==1)
                        _exit(128+SIGTERM);
                }
#else
                (void)terminateWithParent;
#endif
            }
        public:
        

            /**
            * @brief 启动一个新进程（可选择是否定时重启）
            * 
            * 当 `sec == -1` 时，仅启动一次子进程；否则，会创建一个辅助子进程，定期重启该目标进程。
            * 
            * - 不会修改调用方（父进程）的信号处置。
            * - 定时调度进程退出时，Linux 会向其当前目标子进程发送 SIGTERM，避免孤儿服务。
            * 
            * @tparam Args 可变参数类型（用于传递给目标程序的 argv）
            * @param name 要执行的程序路径（如 `/usr/bin/myapp`）
            * @param sec 定时间隔（单位：秒）。若为 -1，则表示只启动一次，不定时。
            * @param args 启动参数（第一个参数必须是程序名称，即 argv[0]，末尾不需要添加 nullptr）
            * @return true 父进程返回 true 表示启动（或调度）成功
            * @return false fork 或 execv 出错时返回 false
            * 
            * @note
            * - 参数中不需要手动添加 nullptr；内部自动添加。
            * - 执行失败后不会抛出异常；返回 false。
            * - execv 会在子进程中替换当前进程镜像，不返回。
            */
            template<class... Args>
            static bool startProcess(const std::string &name,const int &sec=-1,Args ...args)
            {
                std::vector<const char *> paramList={args...,nullptr};
                if(sec==-1)
                {
                    pid_t pid=fork();
                    if(pid==-1)
                        return false;
                    if(pid>0)
                        return true;
                    else
                    {
                        prepareForkedChild();
                        execv(name.c_str(),const_cast<char* const*>(paramList.data()));
                        _exit(127);
                    }
                }
                pid_t pid=fork();
                if(pid==-1)
                    return false;
                if(pid>0)
                {   
                    return true;
                }
                else
                {
                    prepareForkedChild(true);
                    while(1)
                    {
                        pid=fork();
                        if(pid==0)
                        {
                            prepareForkedChild(true);
                            execv(name.c_str(),const_cast<char* const*>(paramList.data()));
                            _exit(127);
                        }
                        else if(pid>0)
                        {
                            int sts;
                            while(waitpid(pid,&sts,0)<0&&errno==EINTR) {}
                            sleep(sec);
                        }
                        else
                            continue;
                    }
                }
            }
            /**
            * @brief 通过函数创建子进程（可选择是否定时重启）
            * 
            * 使用一个可调用对象（如 Lambda、函数指针、std::function）作为新子进程的主体逻辑。
            * 
            * - 当 `sec == -1`，函数仅执行一次；
            * - 否则，会创建一个辅助进程定期重新 fork 并执行该函数。
            * 
            * @tparam Fn 可调用对象类型（如函数、Lambda）
            * @tparam Args 可调用对象的参数类型
            * @param fn 要执行的函数或可调用对象
            * @param sec 定时间隔（单位：秒）。若为 -1，则表示只执行一次，不定时。
            * @param args 要传递给函数的参数
            * @return true 父进程返回 true 表示启动成功；子进程内部也会返回 true（用于链式调用等）
            * @return false 创建进程失败
            * 
            * @note
            * - 函数的实际执行是在新 fork 出的子进程中。
            * - 函数 fn 必须是可调用的；参数类型需能完美转发。
            * - 子进程执行完毕后会退出；辅助进程将周期性地重新创建它。
            */
            template<class Fn,class... Args>
            static typename std::enable_if<!std::is_convertible<Fn, std::string>::value, bool>::type
            startProcess(Fn&& fn,const int &sec=-1,Args &&...args)
            {
                if(sec==-1)
                {
                    pid_t pid=fork();
                    if(pid==-1)
                        return false;
                    if(pid>0)
                    return true;
                    else
                    {
                        prepareForkedChild();
                        auto f=std::bind(std::forward<Fn>(fn),std::forward<Args>(args)...);
                        f();
                        _exit(EXIT_SUCCESS);
                    }
                }
                pid_t pid=fork();
                if(pid==-1)
                    return false;
                if(pid>0)
                {   
                    return true;
                }
                else
                {
                    prepareForkedChild(true);
                    while(1)
                    {
                        pid=fork();
                        if(pid==0)
                        {
                            prepareForkedChild(true);
                            auto f=std::bind(std::forward<Fn>(fn),std::forward<Args>(args)...);
                            f();
                            _exit(EXIT_SUCCESS);
                        }
                        else if(pid>0)
                        {
                            int sts;
                            while(waitpid(pid,&sts,0)<0&&errno==EINTR) {}
                            sleep(sec);
                        }
                        else
                            continue;
                    }
                }
            }
        };

        using Task = std::function<void()>;
        /**
        * @class WorkerPool
        * @brief 固定大小的工作线程池
        *
        * WorkerPool 内部维护一个任务队列和若干工作线程。
        * 每个工作线程循环从队列中取出任务并执行。
        *
        * ## 特性
        * - 固定线程数量
        * - 线程安全的任务提交
        * - 支持优雅停止（graceful shutdown）
        *
        * ## 线程安全说明
        * - submit() 是线程安全的
        * - stop() 是线程安全的
        *
        * ## 使用示例
        * @code
        * WorkerPool pool(4);
        * pool.submit([] {
        *     // 执行任务
        * });
        * pool.stop();
        * @endcode
        */
        class WorkerPool 
        {
        public:
            /**
            * @brief 构造函数，创建指定数量的工作线程
            *
            * @param n 工作线程数量，必须至少为 1，否则抛出 std::invalid_argument。
            * @param maxPendingTasks 尚未开始执行的任务上限；达到上限时 submit() 返回 false，默认 65536。
            *
            * 构造完成后，所有线程立即启动并进入等待状态。
            */
            explicit WorkerPool(size_t n,size_t maxPendingTasks=65536):
            stop_(false),maxPendingTasks_(std::max<size_t>(1,maxPendingTasks))
            {
                if(n==0)
                    throw std::invalid_argument("WorkerPool requires at least one worker");
                for (size_t i = 0; i < n; ++i) {
                    threads_.emplace_back([this] {
                        this->workerLoop();
                    });
                }
            }
            /**
            * @brief 析构函数
            *
            * 析构时会自动调用 stop()，
            * 确保所有线程退出并被正确 join。
            */
            ~WorkerPool() 
            {
                stop();
            }
            /**
            * @brief 向线程池提交一个任务
            *
            * @param task 可调用对象，函数签名为 void()
            * @return true：任务已提交；false：线程池已停止或等待队列已满，任务未入队。
            * @note 已接受的任务会在 stop() 返回前执行完；任务抛出的异常被线程池边界捕获，不会杀死工作线程。
            */
            bool submit(Task task)
            {
                size_t pending=0;
                {
                    std::lock_guard<std::mutex> lk(mtx_);
                    if (stop_||tasks_.size()>=maxPendingTasks_)
                        return false;
                    tasks_.push(std::move(task));
                    pending=tasks_.size();
                    pendingTaskCount_.store(pending,std::memory_order_relaxed);
                }
                size_t peak=peakPendingTaskCount_.load(std::memory_order_relaxed);
                while(peak<pending&&!peakPendingTaskCount_.compare_exchange_weak(
                      peak,pending,std::memory_order_relaxed,std::memory_order_relaxed)) {}
                cv_.notify_one();
                return true;
            }
            /**
            * @brief 停止线程池并等待所有线程退出
            * @param drain true：执行完已排队任务；false：丢弃尚未开始的任务，仅等待正在执行的任务返回。
             *
            * 调用后：
            * - 不应再提交新的任务
            * - drain=true 时执行完已排队任务；drain=false 时丢弃尚未开始的任务
            * - 所有工作线程都会退出并 join
            *
            * 该函数可安全重复调用。
            */
            void stop(const bool drain=true)
            {
                std::call_once(stopOnce_,[this,drain] {
                    {
                        std::lock_guard<std::mutex> lk(mtx_);
                        stop_ = true;
                        if(!drain)
                        {
                            tasks_=std::queue<Task>();
                            pendingTaskCount_.store(0,std::memory_order_relaxed);
                        }
                    }
                    cv_.notify_all();
                    for (auto &t : threads_)
                    {
                        if (t.joinable()) t.join();
                    }
                });
            }

            /** @brief 返回当前等待执行的任务数（瞬时快照）。 */
            size_t pendingTasks() const
            {
                return pendingTaskCount_.load(std::memory_order_relaxed);
            }

            /** @brief 返回等待任务队列容量。 */
            size_t maxPendingTasks() const noexcept {return maxPendingTasks_;}

            /** @brief 返回等待任务数的历史峰值。 */
            size_t peakPendingTasks() const noexcept
            {
                return peakPendingTaskCount_.load(std::memory_order_relaxed);
            }

        private:
            /**
            * @brief 工作线程主循环
            *
            * 每个工作线程都会执行此函数：
            * - 在条件变量上等待任务或停止信号
            * - 从任务队列中取出任务
            * - 执行任务
            *
            * 当 stop_ 为 true 且任务队列为空时，线程退出。
            */
            void workerLoop() 
            {
                while (true) 
                {
                    Task task;
                    {
                        std::unique_lock<std::mutex> lk(mtx_);
                        cv_.wait(lk, [this] {
                        return stop_ || !tasks_.empty();
                        });
                        if (stop_ && tasks_.empty())
                        {
                            return;
                        }
                        task = std::move(tasks_.front());
                        tasks_.pop();
                        pendingTaskCount_.store(tasks_.size(),std::memory_order_relaxed);
                    }
                    try
                    {
                        task(); // 执行任务
                    }
                    catch (...)
                    {
                        // 单个任务的异常不应终止整个服务进程或工作线程。
                    }
                }
            }

        private:
            std::vector<std::thread> threads_;
            std::queue<Task> tasks_;
            mutable std::mutex mtx_;
            std::condition_variable cv_;
            bool stop_;
            const size_t maxPendingTasks_;
            std::atomic<size_t> pendingTaskCount_{0};
            std::atomic<size_t> peakPendingTaskCount_{0};
            std::once_flag stopOnce_;
        };
    }
    
}


#endif

