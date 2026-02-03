/**
* @mainpage STTNet C++ Framework
* @author StephenTaam(1356597983@qq.com)
* @version 0.5.0
* @date 2026-01-09
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
/**
* @namespace stt
*/
namespace stt
{
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
        T tmp;
        while (pop(tmp)) {}
    }

    /**
     * @brief Try push (non-blocking). Returns false if queue is full.
     *        尝试入队（非阻塞），队列满则返回 false
     */
    bool push(T&& v) noexcept(std::is_nothrow_move_constructible_v<T>) {
        return emplace_impl(std::move(v));
    }

    bool push(const T& v) noexcept(std::is_nothrow_copy_constructible_v<T>) {
        return emplace_impl(v);
    }

    /**
     * @brief Try pop (single consumer). Returns false if empty.
     *        尝试出队（单消费者），空则返回 false
     */
    bool pop(T& out) noexcept(std::is_nothrow_move_assignable_v<T> &&
                              std::is_nothrow_move_constructible_v<T>)
    {
        Slot& slot = buffer_[head_ & mask_];
        const std::size_t seq = slot.seq.load(std::memory_order_acquire);
        const std::intptr_t dif = static_cast<std::intptr_t>(seq) - static_cast<std::intptr_t>(head_ + 1);

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
        slot.seq.store(head_ + capacity_, std::memory_order_release);

        ++head_;
        return true;
    }

    /**
     * @brief Approximate size (may be inaccurate under concurrency)
     *        近似长度（并发下可能不精确）
     */
    std::size_t approx_size() const noexcept {
        const std::size_t t = tail_.load(std::memory_order_relaxed);
        const std::size_t h = head_; // consumer-only
        return (t >= h) ? (t - h) : 0;
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

private:
    const std::size_t capacity_;
    const std::size_t mask_;
    std::vector<Slot> buffer_;

    // Single consumer only
    std::size_t head_;

    // Multi-producer
    std::atomic<std::size_t> tail_;
};

    }
    /**
    * @namespace stt::file
    * @brief File-related operations: file reading and writing, logging, etc.
    * @ingroup stt
    */
    namespace file
    {
    /**
    * @brief A utility class that provides static functions for file operations.
    */
    class FileTool
    {
    public:
        /**
        * @brief Create a new directory.
        * @param ddir The directory path, which can be an absolute or relative path.
        * @param mode A bit mask representing the permissions of the newly created directory (default is 0775, i.e., rwx rwx r-x).
        * @return true if the operation is successful.
        * @return false if the operation fails.
        */
        static bool createDir(const std::string & ddir,const mode_t &mode=0775); 
        /**
        * @brief Copy a file.
        * @param sourceFile The path of the source file, which can be an absolute or relative path.
        * @param objectFile The path of the target file, which can be an absolute or relative path.
        * @return true if the operation is successful.
        * @return false if the operation fails.
        * @note Non-existent paths will not be created automatically.
        */
        static bool copy(const std::string &sourceFile,const std::string &objectFile);
        /**
        * @brief Create a new file.
        * @param filePath The file path, which can be an absolute or relative path.
        * @param mode A bit mask representing the permissions of the newly created file (default is 0666, i.e., rw- rw- rw-).
        * @return true if the operation is successful.
        * @return false if the operation fails.
        * @note Non-existent paths will be created automatically.
        */
        static bool createFile(const std::string &filePath,const mode_t &mode=0666);
        /**
        * @brief Get the size of a file.
        * @param fileName The name of the file (can be an absolute or relative path).
        * @return >=0 Returns the file size.
        * @return -1 Failed to get the file size.
        */
        static size_t get_file_size(const std::string &fileName);
    };
    
    /**
    * @brief A structure that records the relationship between files and threads.
    * @note Used in subsequent classes to ensure thread safety.
    */
    struct FileThreadLock
    {
        /**
        * @brief The file path.
        */
        std::string loc;
        /**
        * @brief Records how many threads are currently using the file.
        */
        int threads;
        /**
        * @brief The lock for this file.
        */
        std::mutex lock;
        /**
        * @brief The constructor of this structure.
        * @param loc The path used to construct the structure object.
        * @param threads The number of threads using the file used to construct the structure object.
        */
        FileThreadLock(const std::string &loc,const int &threads):loc(loc),threads(threads){};
    };

    /**
    * @brief A class for reading and writing disk files.
    * @note 1. The same object of this class can ensure synchronization and thread safety.
    * @note 2. If the same file is operated by objects of this class, synchronization can also be ensured. However, note that when opening the file, use either an absolute path or a relative path consistently.
    * @note 3. There are two modes when operating files with this class: 1. Read from disk + operate on data in memory + save to disk; these three steps are done separately, suitable for scenarios with complex custom operations. 2. Combine the three operations into one step.
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
        
        std::ofstream fout;
        std::string fileName;
	    
        std::string fileNameTemp;
        bool flag=false;
        bool binary;
        mode_t mode;
        size_t size=0;
        uint64_t totalLines=0;
    private:
        void toMemory();
        bool toDisk();
    public:
        /**
        * @brief Open a file.
        * @param fileName The name of the file to open (can be an absolute or relative path).
        * @param create true: Create the file (and directory) if it does not exist. false: Do not create the file if it does not exist (default is true).
        * @param multiple When >= 1, open the file in binary mode. This value is the ratio of the预定 file space size required for operating the file to the original file size. When < 1, open the file in text mode (default is 0, open in text mode).
        * @param size Manually specify the desired preallocated file space size. When this parameter is non-zero, the multiple-based reservation value will not be used; instead, this preallocated size will be applied directly (unit: bytes) (default: 0)
        * @param mode If create is true and the file does not exist, use a bit mask to represent the permissions of the newly created file (default is 0666, rw- rw- rw-).
        * @return true: File opened successfully.
        * @return false: Failed to open the file.
        * @note Any operation requires opening the file first.
        * @warning If the reserved space in binary mode is too small, it may cause a segmentation fault.
        *
        * Example code 1: Open a file named "test" in the same directory as the program in text mode.
        *
        * @code
        * File f; 
        * f.openFile("test");
        * @endcode
        *
        * Example code 2: Open a file named "test" in the same directory as the program in binary mode, intended for read-only.
        *
        * @code
        * File f; 
        * f.openFile("test",true,1); // Read-only means reserving the same space as the original file.
        * @endcode
        *
        * Example code 3: Open a file named "test" in the same directory as the program in binary mode, expecting to write data. The size after writing will not exceed twice the original file size.
        *
        * @code
        * File f; 
        * f.openFile("test",true,2); // Reserve twice the space.
        * @endcode
        *
        * Example code 4: Open a file named "test" in the same directory as the program in binary mode, expecting to write data. The written size will not exceed 1024 bytes, and the original file size is 0.
        *
        * @code
        * File f; 
        * f.openFile("test",true,1,1024); // Since the file size is 0, the third parameter multiple representing the multiple is invalid. Manually specify the size.
        * @endcode
        */
        bool openFile(const std::string &fileName,const bool &create=true,const int &multiple=0,const size_t &size=0,const mode_t &mode=0666);
        /**
        * @brief Close the opened file.
        * @param del true: Delete the file. false: Do not delete the file (default is false).
        * @return true: Operation successful. false: Operation failed.
        */
        bool closeFile(const bool &del=false);
        /**
        * @brief Destructor.
        * @note By default, the file is closed when the object's lifecycle ends, and the file is not deleted.
        */
        ~File(){closeFile(false);}
        /**
        * @brief Check if the object has opened a file.
        * @return true: The object has opened a file. false: The object has not opened a file.
        */
        bool isOpen(){return flag;}
        /**
        * @brief Check if the object has opened the file in binary mode.
        * @return true: The object has opened the file in binary mode. false: The object has not opened the file in binary mode.
        */
        bool isBinary(){return binary;}
        /**
        * @brief Get the name of the opened file.
        * @return Returns a string representing the name of the opened file.
        */
        std::string getFileName(){return fileName;}
        /**
        * @brief Get the number of lines in the opened file.
        * @return Returns the number of lines in the opened file.
        * @warning 1. Only when the file is opened in text mode can the correct value be returned.
        * @warning 2. The number of lines obtained is the number of lines when the file was last read into memory.
        * @warning 3. If the file has never been read into memory, the returned value will not be correct.
        * @note This function may only be useful when operating on data in memory.
        */
        uint64_t getFileLine(){return totalLines;}
        /**
        * @brief Get the size of the file opened in binary mode.
        * @return Returns the size of the file opened in binary mode.
        * @warning 1. Only when the file is opened in binary mode can the correct value be returned.
        * @warning 2. The size obtained is the size of the file when it was last read into memory.
        * @warning 3. If the file has never been read into memory, the returned value will not be correct.
        * @note This function may only be useful when operating on data in memory.
        */
        size_t getFileSize(){return size;}
        /**
        * @brief Get the size of the file opened in binary mode in memory.
        * @return Returns the size of the file opened in binary mode in memory.
        * @warning 1. Only when the file is opened in binary mode can the correct value be returned.
        * @warning 2. The size obtained is the size of the file currently being operated on in memory.
        * @warning 3. If the file has never been read into memory, the returned value will not be correct.
        * @note This function is only useful when operating on data in memory.
        */
        size_t getSize1(){return size1;}
    public:
        /**
        * @brief Read data from disk into memory.
        * @return true if the operation is successful.
        * @return false if the operation fails.
        */
        bool lockMemory();
        /**
        * @brief Write data from memory to disk.
        * @param rec false: Do not roll back the operation and save the operation result to disk. true: Roll back the operation and do not save. (Default is false, no rollback required).
        * @return true if the operation is successful.
        * @return false if the operation fails.
        * @note If writing to disk fails, the operation will be automatically rolled back.
        */     
        bool unlockMemory(const bool &rec=false);
    public:
        /**
        * @name Functions for operating on data in memory - Text mode
        * @{
        */
        /**
        * @brief Find a line.
        * @note 1. Find the first line in the data imported from the file into memory that contains the target string.
        * @note 2. Line numbers start from 1.
        * @param targetString The string to be found.
        * @param linePos Start searching from the specified line (default is the first line).
        * @return >=1 Returns the line number of the match.
        * @return -1 Search failed.
        */
        int findC(const std::string &targetString,const int linePos=1);
        /**
        * @brief Insert a line.
        * @note 1. Insert a line into the data imported from the file into memory.
        * @note 2. Line numbers start from 1.
        * @param data The data to be inserted.
        * @param linePos Insert at the specified line (default is to insert at the end).
        * @return true if the insertion is successful.
        * @return false if the insertion fails.
        */
        bool appendLineC(const std::string &data,const int &linePos=0);
        /**
        * @brief Delete a line.
        * @note 1. Delete a line from the data imported from the file into memory.
        * @note 2. Line numbers start from 1.
        * @param linePos The line to be deleted.
        * @return true if the deletion is successful.
        * @return false if the deletion fails.
        */
        bool deleteLineC(const int &linePos=0);
        /**
        * @brief Delete all lines.
        * @note Delete all lines from the data imported from the file into memory.
        * @return true if the deletion is successful.
        * @return false if the deletion fails.
        */
        bool deleteAllC();
        /**
        * @brief Modify a line.
        * @note 1. Modify a line in the data imported from the file into memory.
        * @note 2. Line numbers start from 1.
        * @param data The data to overwrite the specified line.
        * @param linePos The line to be overwritten (default is the last line).
        * @return true if the modification is successful.
        * @return false if the modification fails.
        */
        bool chgLineC(const std::string &data,const int &linePos=0);
        /**
        * @brief Read a single line.
        * @note 1. Read a single line from the data imported from the file into memory.
        * @note 2. Line numbers start from 1.
        * @param data The string container to receive the data.
        * @param linePos The line to be read.
        * @return true if the reading is successful.
        * @return false if the reading fails.
        */
        bool readLineC(std::string &data,const int linePos);
        /**
        * @brief Read multiple lines.
        * @note 1. Continuously read n lines from the data imported from the file into memory.
        * @note 2. Line numbers start from 1.
        * @param data The string container to receive the data.
        * @param linePos The starting position to read.
        * @param num The number of lines to read.
        * @return A reference to the string parameter data.
        */
        std::string& readC(std::string &data,const int &linePos,const int &num);
        /**
        * @brief Read all lines.
        * @note Read all lines from the data imported from the file into memory.
        * @param data The string container to receive the data.
        * @return A reference to the string parameter data.
        */
        std::string& readAllC(std::string &data);
        /** @} */
        /**
        * @name Functions for operating on data in memory - Binary mode
        * @{
        */
        /**
        * @brief Read a data block.
        * @note 1. Read a block of data from the data imported from the file into memory.
        * @note 2. Data byte units start from 0.
        * @param data The container to receive the data.
        * @param pos The starting position of the data.
        * @param size The size of the data block.
        * @return true if the reading is successful.
        * @return false if the reading fails.
        */
        bool readC(char *data,const size_t &pos,const size_t &size);
        /**
        * @brief Write a data block.
        * @note 1. Write a block of data into the data imported from the file into memory.
        * @note 2. Data byte units start from 0.
        * @param data The container holding the data to be written.
        * @param pos The writing position.
        * @param size The size of the data block to be written.
        * @return true if the writing is successful.
        * @return false if the writing fails.
        */
        bool writeC(const char *data,const size_t &pos,const size_t &size);
        /**
        * @brief Format the data.
        * @note Delete all the data imported from the file into memory.
        */
        bool formatC();
        /** @} */
    public:
        /**
        * @name Functions for directly operating on disk data - Text mode
        * @{
        */
        /**
        * @brief Find a line.
        * @note 1. Find the first line in the file that contains the target string.
        * @note 2. Line numbers start from 1.
        * @param targetString The string to be found.
        * @param linePos Start searching from the specified line (default is the first line).
        * @return >=1 Returns the line number of the match.
        * @return -1 Search failed.
        */
        int find(const std::string &targetString,const int linePos=1);
        /**
        * @brief Insert a line.
        * @note 1. Insert a line into the file.
        * @note 2. Line numbers start from 1.
        * @param data The data to be inserted.
        * @param linePos Insert at the specified line (default is to insert at the end).
        * @return true if the insertion is successful.
        * @return false if the insertion fails.
        */
        bool appendLine(const std::string &data,const int &linePos=0);
        /**
        * @brief Delete a line.
        * @note 1. Delete a line from the file.
        * @note 2. Line numbers start from 1.
        * @param linePos The line to be deleted.
        * @return true if the deletion is successful.
        * @return false if the deletion fails.
        */
        bool deleteLine(const int &linePos=0);
        /**
        * @brief Delete all lines.
        * @note Delete all lines from the file.
        * @return true if the deletion is successful.
        * @return false if the deletion fails.
        */
        bool deleteAll();
        /**
        * @brief Modify a line.
        * @note 1. Modify a line in the file.
        * @note 2. Line numbers start from 1.
        * @param data The data to overwrite the specified line.
        * @param linePos The line to be overwritten (default is the last line).
        * @return true if the modification is successful.
        * @return false if the modification fails.
        */
        bool chgLine(const std::string &data,const int &linePos=0);
        /**
        * @brief Read a single line.
        * @note 1. Read a single line from the file.
        * @note 2. Line numbers start from 1.
        * @param data The string container to receive the data.
        * @param linePos The line to be read.
        * @return true if the reading is successful.
        * @return false if the reading fails.
        */
        bool readLine(std::string &data,const int linePos);
        /**
        * @brief Read multiple lines.
        * @note 1. Continuously read n lines from the file.
        * @note 2. Line numbers start from 1.
        * @param data The string container to receive the data.
        * @param linePos The starting position to read.
        * @param num The number of lines to read.
        * @return A reference to the string parameter data.
        */
        std::string& read(std::string &data,const int &linePos,const int &num);
        /**
        * @brief Read all lines.
        * @note Read all lines from the file.
        * @param data The string container to receive the data.
        * @return A reference to the string parameter data.
        */
        std::string& readAll(std::string &data);
        /** @} */
        /**
        * @name Functions for directly operating on disk data - Binary mode
        * @{
        */
        /**
        * @brief Read a data block.
        * @note 1. Read a block of data from the file.
        * @note 2. Data byte units start from 0.
        * @param data The container to receive the data.
        * @param pos The starting position of the data.
        * @param size The size of the data block.
        * @return true if the reading is successful.
        * @return false if the reading fails.
        */
        bool read(char *data,const size_t &pos,const size_t &size);
        /**
        * @brief Write a data block.
        * @note 1. Write a block of data into the file.
        * @note 2. Data byte units start from 0.
        * @param data The container holding the data to be written.
        * @param pos The writing position.
        * @param size The size of the data block to be written.
        * @return true if the writing is successful.
        * @return false if the writing fails.
        */
        bool write(const char *data,const size_t &pos,const size_t &size);
        /**
        * @brief Format the data.
        * @note Delete all the data in the file.
        */
        void format();
        /** @} */
    };
    }
    /**
    * @namespace stt::time
    * @brief Time-related operations, basic time tools.
    * @ingroup stt
    */
    namespace time
    {
    /**
    * @brief A structure representing a time interval, supporting granularity in days, hours, minutes, seconds, and milliseconds.
    * @note 1. Provides basic operations on time intervals, such as addition, subtraction, comparison, and unit conversion.
    * @note 2. This structure does not represent an absolute time point, but only the difference between two time points.
    * @note 3. The internal implementation uses multiple fields instead of a unified timestamp to improve readability and controllability.
    */
    struct Duration
{
    /**
    * @brief Days
    */
    long long day;
    /**
    * @brief Hours
    */
    int hour;
    /**
    * @brief Minutes
    */
    int min;
    /**
    * @brief Seconds
    */
    int sec;
    /**
    * @brief Milliseconds
    */
    int msec;
    /**
    * @brief Constructor, taking days, hours, minutes, seconds, milliseconds
    */
    Duration(long long a, int b, int c, int d, int e) : day(a), hour(b), min(c), sec(d), msec(e) {}
    Duration() = default;
    /**
    * @brief Determine if the current time interval is greater than another time interval.
    * @param b Another Duration instance to compare.
    * @return True if the current object is greater than parameter b, otherwise false.
    */
    bool operator>(const Duration &b)
    {
        long long total;
        total = day * 24 * 60 * 60 * 1000 + hour * 60 * 60 * 1000 + min * 60 * 1000 + sec * 1000 + msec;
        long long totalB;
        totalB = b.day * 24 * 60 * 60 * 1000 + b.hour * 60 * 60 * 1000 + b.min * 60 * 1000 + b.sec * 1000 + b.msec;
        if (total > totalB)
            return true;
        else
            return false;
    }
    /**
    * @brief Determine if the current time interval is less than another time interval.
    * @param b Another Duration instance to compare.
    * @return True if the current object is less than parameter b, otherwise false.
    */
    bool operator<(const Duration &b)
    {
        long long total;
        total = day * 24 * 60 * 60 * 1000 + hour * 60 * 60 * 1000 + min * 60 * 1000 + sec * 1000 + msec;
        long long totalB;
        totalB = b.day * 24 * 60 * 60 * 1000 + b.hour * 60 * 60 * 1000 + b.min * 60 * 1000 + b.sec * 1000 + b.msec;
        if (total < totalB)
            return true;
        else
            return false;
    }
    /**
    * @brief Determine if the current time interval is equal to another time interval.
    * @param b Another Duration instance to compare.
    * @return True if the current object is equal to parameter b, otherwise false.
    */
    bool operator==(const Duration &b)
    {
        long long total;
        total = day * 24 * 60 * 60 * 1000 + hour * 60 * 60 * 1000 + min * 60 * 1000 + sec * 1000 + msec;
        long long totalB;
        totalB = b.day * 24 * 60 * 60 * 1000 + b.hour * 60 * 60 * 1000 + b.min * 60 * 1000 + b.sec * 1000 + b.msec;
        if (total == totalB)
            return true;
        else
            return false;
    }
    /**
    * @brief Determine if the current time interval is greater than or equal to another time interval.
    * @param b Another Duration instance to compare.
    * @return True if the current object is greater than or equal to parameter b, otherwise false.
    */
    bool operator>=(const Duration &b)
    {
        long long total;
        total = day * 24 * 60 * 60 * 1000 + hour * 60 * 60 * 1000 + min * 60 * 1000 + sec * 1000 + msec;
        long long totalB;
        totalB = b.day * 24 * 60 * 60 * 1000 + b.hour * 60 * 60 * 1000 + b.min * 60 * 1000 + b.sec * 1000 + b.msec;
        if (total >= totalB)
            return true;
        else
            return false;
    }
    /**
    * @brief Determine if the current time interval is less than or equal to another time interval.
    * @param b Another Duration instance to compare.
    * @return True if the current object is less than or equal to parameter b, otherwise false.
    */
    bool operator<=(const Duration &b)
    {
        long long total;
        total = day * 24 * 60 * 60 * 1000 + hour * 60 * 60 * 1000 + min * 60 * 1000 + sec * 1000 + msec;
        long long totalB;
        totalB = b.day * 24 * 60 * 60 * 1000 + b.hour * 60 * 60 * 1000 + b.min * 60 * 1000 + b.sec * 1000 + b.msec;
        if (total <= totalB)
            return true;
        else
            return false;
    }
    /**
    * @brief Add two time intervals.
    * @param b Another Duration to add.
    * @return The resulting Duration after addition.
    */
    time::Duration operator+(const time::Duration &b)
    {
        long long dayy = day;
        int hourr = hour;
        int minn = min;
        int secc = sec;
        int msecc = msec;

        msecc += b.msec;
        secc += b.sec;
        minn += b.min;
        hourr += b.hour;
        dayy += b.day;

        if (msecc / 1000 != 0)
        {
            secc += msecc / 1000;
            msecc = msecc % 1000;
        }

        if (secc / 60 != 0)
        {
            minn += secc / 60;
            secc = secc % 60;
        }

        if (minn / 60 != 0)
        {
            hourr += minn / 60;
            minn = minn % 60;
        }

        if (hourr / 24 != 0)
        {
            dayy += hourr / 24;
            hourr = hourr % 24;
        }
        return time::Duration(dayy, hourr, minn, secc, msecc);
    }
    /**
    * @brief Calculate the difference between two time intervals (current object minus parameter b).
    * @param b Another Duration to subtract.
    * @return The resulting Duration of the difference.
    */
    time::Duration operator-(const time::Duration &b)
    {
        long long dayy = day;
        int hourr = hour;
        int minn = min;
        int secc = sec;
        int msecc = msec;

        msecc = dayy * 24 * 60 * 60 * 1000 + hourr * 60 * 60 * 1000 + minn * 60 * 1000 + secc * 1000 + msecc - b.day * 24 * 60 * 60 * 1000 - b.hour * 60 * 60 * 1000 - b.min * 60 * 1000 - b.sec * 1000 - b.msec;
        secc = 0;
        minn = 0;
        hourr = 0;
        dayy = 0;

        if (msecc / 1000 != 0)
        {
            secc += msecc / 1000;
            msecc = msecc % 1000;
        }

        if (secc / 60 != 0)
        {
            minn += secc / 60;
            secc = secc % 60;
        }

        if (minn / 60 != 0)
        {
            hourr += minn / 60;
            minn = minn % 60;
        }

        if (hourr / 24 != 0)
        {
            dayy += hourr / 24;
            hourr = hourr % 24;
        }
        return time::Duration(dayy, hourr, minn, secc, msecc);
    }
    
    /**
    * @brief Convert the current time interval to a floating-point representation in days.
    */
    double convertToDay()
    { 
        long long total;
        total = hour * 60 * 60 * 1000 + min * 60 * 1000 + sec * 1000 + msec; 
        double k = day + total / 86400000.0000;
        return k;
    }
    /**
    * @brief Convert the current time interval to a floating-point representation in hours.
    */
    double convertToHour()
    {
        long long total;
        total = min * 60 * 1000 + sec * 1000 + msec; 
        double k = day * 24 + hour + total / 36000000.0000;
        return k;
    }
    /**
    * @brief Convert the current time interval to a floating-point representation in minutes.
    */
    double convertToMin()
    {
        long long total;
        total = sec * 1000 + msec; 
        double k = day * 24 * 60 + hour * 60 + min + total / 60000.0000;
        return k;
    }
    /**
    * @brief Convert the current time interval to a floating-point representation in seconds.
    */
    double convertToSec()
    {
        long long total;
        total = msec; 
        double k = day * 24 * 60 * 60 + hour * 60 * 60 + min * 60 + sec + total / 1000.0000;
        return k;
    }
    /**
     * @brief Convert the current time interval to total milliseconds.
    */
    long long convertToMsec()
    {
        long long total;
        total = day * 24 * 60 * 60 * 1000 + hour * 60 * 60 * 1000 + min * 60 * 1000 + sec * 1000 + msec;
        return total;
    }
    /**
    * @brief Recover the standard days-hours-minutes-seconds-milliseconds format from given milliseconds.
    * @param t The millisecond value to recover.
    * @return The converted Duration.
    */
    Duration recoverForm(const long long &t)
    {
        msec = t;
        sec = 0;
        min = 0;
        hour = 0;
        day = 0;

        if (msec / 1000 != 0)
        {
            sec += msec / 1000;
            msec = msec % 1000;
        }

        if (sec / 60 != 0)
        {
            min += sec / 60;
            sec = sec % 60;
        }

        if (min / 60 != 0)
        {
            hour += min / 60;
            min = min % 60;
        }

        if (hour / 24 != 0)
        {
            day += hour / 24;
            hour = hour % 24;
        }
        return Duration(day, hour, min, sec, msec);
    }
};
/**
* @brief Output the Duration object to a stream in a readable format.
*
* This function formats the Duration fields (days, hours, minutes, seconds, milliseconds) 
* and outputs them to the given output stream.
*
* @param os The output stream (e.g., std::cout).
* @param a The Duration object to output.
* @return Reference to the output stream for chaining.
*
* @note The output format is typically human-readable, such as "1d 02:03:04.005" 
* (specific to the implementation).
*/
std::ostream& operator<<(std::ostream &os, const Duration &a);

using Milliseconds = std::chrono::duration<uint64_t, std::milli>;
using Seconds = std::chrono::duration<uint64_t>;
/**
* @brief Define the macro ISO8086A as "yyyy-mm-ddThh:mi:ss"
*/
#define ISO8086A "yyyy-mm-ddThh:mi:ss"
/**
* @brief Define the macro ISO8086B as "yyyy-mm-ddThh:mi:ss.sss"
*/
#define ISO8086B "yyyy-mm-ddThh:mi:ss.sss"


/**
* @brief Class for time operations, calculations, and timing
* @brief Accurate to milliseconds
* @warning Only accurate within 1970 ± 292 years
* @bug Only accurate within 1970 ± 292 years, to be optimized
*/
class DateTime
{
private:
    static Duration& dTOD(const Milliseconds& d1, Duration &D1);
    static Milliseconds& DTOd(const Duration &D1, Milliseconds& d1);
    static std::string &toPGtimeFormat();
    static std::chrono::system_clock::time_point strToTimePoint(const std::string &timeStr, const std::string &format = ISO8086A);
    static std::string& timePointToStr(const std::chrono::system_clock::time_point &tp, std::string &timeStr, const std::string &format = ISO8086A);
public:
    /**
    * @brief Get the current time
    * @note Get the current time and return it as a string
    * @param timeStr The string container to receive the time
    * @param format Specify the time string format (default is 'yyyy-mm-ddThh:mi:ss', i.e., ISO08086A standard)
    * @return Reference to timeStr
    */
    static std::string& getTime(std::string &timeStr, const std::string &format = ISO8086A);
    /**
    * @brief Convert the format of a time string
    * @note Modify the original string via reference
    * @param timeStr The original time string
    * @param oldFormat Original time string format 
    * @param newFormat New time format (default is ISO08086A standard)
    * @return true for success, false for failure
    */
    static bool convertFormat(std::string &timeStr, const std::string &oldFormat, const std::string &newFormat = ISO8086A);
    /**
    * @brief Calculate the difference between two time strings.
    * @param time1 The time to be subtracted from
    * @param time2 The time to subtract
    * @param result A Duration container to receive the result
    * @param format1 Time string format of time1 (default is ISO08086A standard)
    * @param format2 Time string format of time2 (default is ISO08086A standard)
    * @return Reference to result
    */
    static Duration& calculateTime(const std::string &time1, const std::string &time2, Duration &result, const std::string &format1 = ISO8086A, const std::string &format2 = ISO8086A);
    /**
    * @brief Add or subtract a duration from a time string.
    * @param time1 The time string to operate on
    * @param time2 The duration to operate with
    * @param result A string container to receive the result time string
    * @param am '+' for addition, '-' for subtraction
    * @param format1 Format of time1 (default is ISO08086A standard)
    * @param format2 Format of result (default is ISO08086A standard)
    * @return Reference to result
    */
    static std::string& calculateTime(const std::string &time1, const Duration &time2, std::string &result, const std::string &am, const std::string &format1 = ISO8086A, const std::string &format2 = ISO8086A);
    /**
    * @brief Compare the magnitudes of two time strings.
    * @note Later times are considered larger.
    * @param time1 The first time string to compare
    * @param time2 The second time string to compare
    * @param format1 Format of time1 (default is ISO08086A standard)
    * @param format2 Format of time2 (default is ISO08086A standard)
    * @return true if time1 >= time2, false otherwise
    */
    static bool compareTime(const std::string &time1, const std::string &time2, const std::string &format1 = ISO8086A, const std::string &format2 = ISO8086A);
private:
    Duration dt{-1, -1, -1, -1, -1};
    bool flag = false;
    std::chrono::steady_clock::time_point start;
    std::chrono::steady_clock::time_point end;
public:
    /**
    * @brief Start timing
    * @return true for successful start, false for failure
    */
    bool startTiming();
    /**
    * @brief Check time during timing
    * @return A Duration recording the elapsed time so far
    */
    Duration checkTime();
    /**
    * @brief Stop timing
    * @return A Duration recording the elapsed time
    * @note The object saves the last timing result
    */
    Duration endTiming();
public:
    /**
    * @brief Get the last timing duration
    * @return A Duration recording the elapsed time
    */
    Duration getDt() { return dt; }
    /**
    * @brief Return the timing status of the object
    * @return true if timing is in progress, false otherwise
    */
    bool isStart() { return flag; }
};
}
namespace file
{
/**
* @brief Log file operation class
* @note The log reading/writing of this class is thread-safe due to inheritance from the File class
* @note Asynchronous logging will use a separate thread for writing operations, allowing multiple threads to operate on the log simultaneously.
*/
class LogFile : private time::DateTime, protected file::File
{
private:
    std::string timeFormat;
    std::string contentFormat;
    std::atomic<bool> consumerGuard{true};
    //std::mutex queueMutex;
    //std::condition_variable queueCV;
    system::MPSCQueue<std::string> logQueue;
    std::thread consumerThread;
public:
    /**

* @brief Constructor, initializes the consumer thread

* @param logQueue_cap Asynchronous log queue capacity (must be a power of 2).

// // Logs generated by all threads will first enter this lock-free queue, and then be written to the file in batches by an independent logger thread.

// The logging system is not on the main business hot path, allowing logs to be discarded during overload to protect the performance of core services.

// // Selection principle:

// logQueue_cap >= Peak log rate × worst-case logger pause time

//
// Recommended values ​​(empirical):

// - Default: 8192 (~8k)

// - High-frequency logs: 16384 (~16k)

//
// When the queue is full: logs will be discarded, and the framework will not block the calling thread.

//
*/
    LogFile(const size_t &logQueue_cap=8192):logQueue(logQueue_cap)
    {
        consumerGuard=true;
        consumerThread = std::thread([this]()->void
        {
            std::string content;
            while(this->consumerGuard)
            {
                while(this->logQueue.pop(content))//非空则执行
                {                   
                        this->appendLine(content);
                }
                std::this_thread::sleep_for(std::chrono::microseconds(500));

            }
        });
    }
    /**
    * @brief Open a log file
    * @note Create if not exists (including directories). Default directory permission: rwx rwx r-x, 
    * default log file permission: rw- rw- r--
    * @param fileName Log file name (absolute or relative path)
    * @param timeFormat Time format in the log file (default is ISO08086A standard)
    * @param contentFormat Filler format between time and record (default is "   ")
    * @return true for success, false for failure
    */
    bool openFile(const std::string &fileName, const std::string &timeFormat = ISO8086A, const std::string &contentFormat = "   ");
    /**
    * @brief Get the status of whether the log file is open
    * @return true if open, false otherwise
    */
    bool isOpen() { return File::isOpen(); }
    /**
    * @brief Get the file name opened by the object
    * @return The file name opened by the object
    */
    std::string getFileName() { return File::getFileName(); }
    /**
    * @brief Close the log file opened by the object
    * @param del true to close and delete the log file, false to only close (default)
    * @return true for successful close, false for failure
    */
    bool closeFile(const bool &del = false);
    /**
    * @brief Write a line of log
    * @param data Content to be written to the log
    */
    void writeLog(const std::string &data);
    /**
    * @brief Clear all logs
    * @return true for successful clear, false for failure
    */
    bool clearLog();
    /**
    * @brief Delete logs within a specified time interval
    * @param date1 The first parameter of the time interval (default is infinitely small)
    * @param date2 The second parameter of the time interval (default is infinitely large)
    * @note The interval is [date1, date2)
    * @return true for successful deletion, false for failure
    */
    bool deleteLogByTime(const std::string &date1 = "1", const std::string &date2 = "2");
    /**
    * @brief The destructor writes the log and then closes the consumer thread.
    */
    ~LogFile();
};
}
    /**
    * @namespace stt::data
    * @brief Data processing
    * @ingroup stt
    */
    namespace data
    {
        /**
        * @brief Responsible for encryption, decryption, and hashing
        */
        class CryptoUtil
        {
        public:
            /**
            * @brief Symmetric encryption function in AES-256-CBC mode
            * @param before Data container before encryption
            * @param length Length of the data
            * @param passwd Encryption key
            * @param iv IV vector
            * @param after Data container for ciphertext
            * @return true: Encryption successful, false: Encryption failed
            * @note In AES-256-CBC mode, the key is 32 bytes and the iv vector is 16 bytes
            */
            static bool encryptSymmetric(const unsigned char *before, const size_t &length, const unsigned char *passwd, const unsigned char *iv, unsigned char *after);
            /**
            * @brief Symmetric decryption function in AES-256-CBC mode
            * @param before Data container for ciphertext
            * @param length Length of the ciphertext
            * @param passwd Decryption key
            * @param iv IV vector
            * @param after Data container for decrypted data
            * @return true: Decryption successful, false: Decryption failed
            * @note In AES-256-CBC mode, the key is 32 bytes and the iv vector is 16 bytes
            */
            static bool decryptSymmetric(const unsigned char *before, const size_t &length, const unsigned char *passwd, const unsigned char *iv, unsigned char *after);
            /**
            * @brief Calculate the SHA-1 hash value (raw binary form) of the input string.
            *
            * Uses OpenSSL's SHA1 function to hash the input string, and the result is stored in result in 20-byte raw binary form.
            * Note: result may contain non-printable characters and is not suitable for direct output or writing to text.
            *
            * @param ori_str Input original string.
            * @param result Used to store the output SHA-1 hash value (binary form), 20 bytes in length.
            * @return Reference to result.
            *
            * @note This function is suitable for subsequent encryption, signature, etc. (e.g., for HMAC input).
            */
            static std::string& sha1(const std::string &ori_str, std::string &result);
            /**
            * @brief Calculate the SHA-1 hash value of the input string and return it as a hexadecimal string.
            *
            * Uses OpenSSL's SHA1 function to hash the input string, and the result is stored in result in a 40-character hexadecimal format.
            * Each byte is converted to two hexadecimal characters, which can be directly used for readable scenarios such as printing, logging, and storage.
            *
            * @param ori_str Input original string.
            * @param result Used to store the output SHA-1 hash value (40-byte hexadecimal string).
            * @return Reference to result.
            *
            * @note Suitable for hash display, unique identification, log verification, etc. The main difference from sha1 is the output format.
            */
	        static std::string& sha11(const std::string &ori_str, std::string &result);
        };
        /**
        * @brief Responsible for conversion between binary data and strings
        */
        class BitUtil
        {
        public:
            /**
            * @brief Convert a single character to its corresponding 8-bit binary string.
            *
            * @param input Input character.
            * @param result Used to save the output binary string (e.g., 'A' -> "01000001").
            * @return Reference to result.
            */
            static std::string& bitOutput(char input, std::string &result);
            /**
            * @brief Convert each character in the string to binary bits and concatenate them into an overall string.
            *
            * @param input Input string.
            * @param result Save the output continuous bit string (length is input.size() * 8).
            * @return Reference to result.
            */
	        static std::string& bitOutput(const std::string &input, std::string &result);
            /**
            * @brief Get the pos-th bit (from left to right) of the character input (binary) and return '1' or '0'.
            *
            * @param input Input character.
            * @param pos Bit position (1~8, 1 is the most significant bit).
            * @param result Returned bit character: '1' or '0'.
            * @return Reference to result.
            */
	        static char& bitOutput_bit(char input, const int pos, char &result);
            /**
            * @brief Convert a "01" string (binary string) to an unsigned integer.
            *
            * @param input Input binary string (e.g., "1011").
            * @param result Output result value.
            * @return Reference to result.
            */
	        static unsigned long& bitStrToNumber(const std::string &input, unsigned long &result);
            /**
            * @brief Convert a string to binary and then to the corresponding numeric value.
            *
            * @param input Any original data string.
            * @param result Return the converted numeric value.
            * @return Reference to result.
            *
            * @note Actually calls bitOutput to get the bit string first, then converts it to an integer via bitStrToNumber.
            */
	        static unsigned long& bitToNumber(const std::string &input, unsigned long &result);
            /**
            * @brief Compress a "01" string of up to 8 bits into 1 byte (char).
            *
            * @param input Binary string, up to 8 bits.
            * @param result Output compressed byte.
            * @return Reference to result.
            */
	        static char& toBit(const std::string &input, char &result);
            /**
            * @brief Compress an arbitrary length "01" string into binary data, with every 8 bits as a byte.
             *
            * @param input Input bit string (length should be a multiple of 8).
            * @param result Return the compressed binary data (each character represents 1 byte).
            * @return Reference to result.
            */
	        static std::string& toBit(const std::string &input, std::string &result);
        };
        /**
        * @brief Related to random number and string generation
        */
        class RandomUtil
        {
        public:
            /**
            * @brief Generate a random integer
            * @param a Lower bound of the random number range
            * @param b Upper bound of the random number range
            * @note Generate a random number in the range [a, b]
            * @return The generated random number
            */
            static long getRandomNumber(const long &a, const long &b);
            /**
            * @brief Generate a pseudo-random string of specified length within the Base64 character set, and append '=' at the end to complete the Base64 string format
            * @param str Container to save the generated string
            * @param length Length of the string to generate
            * @return Reference to str
            */
            static std::string& getRandomStr_base64(std::string &str, const int &length);
            /**
            * @brief Generate a 32-bit (4-byte) random mask.
            *
            * This function first randomly generates a 32-bit string composed of '0' and '1' (e.g., "010110..."),
            * then converts it into the corresponding 4-byte binary data through the internal `BitUtil::toBit()` function.
            *
            * The conversion result is returned via the mask parameter, which is typically used to generate packet masks, encryption masks, bitmap masks, etc.
            *
            * @param mask Used to store the final generated 4-byte mask (binary string form).
            * @return Reference to mask.
            *
            * @note Internally depends on the function `BitUtil::toBit(const std::string&, std::string&)` to compress the 32-bit binary string into 4 bytes.
            */
            static std::string& generateMask_4(std::string &mask);
        };
        /**
        * @brief Responsible for endianness conversion
        */
        class NetworkOrderUtil
        {
        public:
            /**
            * @brief Reverse the endianness of a 64-bit unsigned integer (big endian <-> little endian).
            *
            * This function simulates the 64-bit version of `htonl`/`ntohl`, i.e., swaps high and low bytes.
            * Modifies the parameter in place and returns a reference.
            *
            * @param data Input 64-bit unsigned integer, returned after byte reversal.
            * @return Reference to the reversed data.
            *
            * @note This implementation does not depend on platform library functions and is suitable for scenarios where the machine endianness is uncertain.
            */
            static unsigned long& htonl_ntohl_64(unsigned long &data); // 64-bit unsigned number conversion to big/little endian (network byte order)
        };
        
        /**
        * @brief Responsible for floating-point precision processing
        */
        class PrecisionUtil
        {
        public:
        
            /**
            * @brief Format a floating-point number into a string representation with a specified number of decimal places.
            * 
            * @param number Input floating-point number.
            * @param bit Number of decimal places to retain.
            * @param str Used to store the formatted string.
            * @return Reference to the formatted result string.
            */
	        static std::string& getPreciesFloat(const float &number, const int &bit, std::string &str);
            /**
            * @brief Retain a specified number of decimal places for a float value and modify the original value directly.
            *
            * @param number Float variable to be processed (will be modified).
            * @param bit Number of decimal places to retain.
            * @return Reference to the modified float.
            */
            static float& getPreciesFloat(float &number, const int &bit);
            /**
            * @brief Format a double-precision floating-point number into a string representation with a specified number of decimal places.
            *
            * @param number Input double value.
            * @param bit Number of decimal places to retain.
            * @param str Used to store the formatted string.
            * @return Reference to the formatted result string.
            */
            static std::string& getPreciesDouble(const double &number, const int &bit, std::string &str);
            /**
            * @brief Retain a specified number of decimal places for a double value and modify the original value directly.
            *
            * @param number Double variable to be processed (will be modified).
            * @param bit Number of decimal places to retain.
            * @return Reference to the modified double.
            */
            static double& getPreciesDouble(double &number, const int &bit);
            /**
            * @brief Dynamically adjust the decimal precision based on the value, retaining a specified number of significant figures.
            *
            * For decimals less than 1, the position of the most significant digit is first determined,
            * and then bit significant figures are retained on this basis.
            * The modified value will be rounded to retain the appropriate number of decimal places and written back to the original variable.
            *
            * @param number Float value to be processed (will be modified).
            * @param bit Number of significant figures to retain.
            * @return Reference to the modified float.
            */
            static float& getValidFloat(float &number, const int &bit);
        };
        /**
        * @brief Responsible for HTTP string and URL parsing
        * Including functions to extract parameters, IP, port, request header fields, etc., from URLs or request messages.
        */
        class HttpStringUtil
        {
        public:
            /**
            * @brief Extract a substring between two markers from the original string.
            *
            * Extracts the content from a to b (excluding a and b), and a starting search position can be specified.
            * If a or b is an empty string, it means from the beginning or to the end, respectively.
            * If a is not found, it defaults to starting from the beginning.
            * If b is not found, it defaults to the end.
            *
            * @param ori_str Original string.
            * @param str String to store the extraction result.
            * @param a Start marker string.
            * @param b End marker string.
            * @param pos Starting search position.
            * @return Returns the position of b in ori_str (may return string::npos if b is not found or b is "")
            * @note If not found, the result string is ""
            */       
	        static size_t get_split_str(const std::string_view& ori_str, std::string_view &str, const std::string_view &a, const std::string_view &b, const size_t &pos = 0);
            /**
            * @brief Extract the value of a specified key from the URL query parameters.
            *
            * @note The URL does not need to be complete. For example, extracting the value of `id` from `?id=123&name=abc` is the same as extracting from `http://xxxx/?id=123&name=abc`.
            *
            * @param ori_str Original URL string.
            * @param str String to store the extraction result.
            * @param name Parameter name (key).
            * @return Reference to the result string.
            */
	        static std::string_view& get_value_str(const std::string_view& ori_str, std::string_view &str, const std::string& name);
            /**
            * @brief Extract the value of a specified field from the HTTP request header.
            *
            * @param ori_str Original HTTP request header string.
            * @param str Extraction result.
            * @param name Request header field name (e.g., "Host").
            * @return Reference to the result string.
            */
	        static std::string_view& get_value_header(const std::string_view& ori_str, std::string_view &str, const std::string& name);
            /**
            * @brief Extract the path and query part from the URL.
            *
            * For example, extract `/path` from `http://abc.com/path?query=123` or from `/path?query=123`.
            *
            * @param ori_str Original URL.
            * @param str Return the path part.
            * @return Reference to the result string.
            */
	        static std::string_view& get_location_str(const std::string_view& ori_str, std::string_view &str);
            /**
            * @brief Extract the path part of the URL (excluding the query).
            *
            * Similar to `get_location_str`, but retains all content after the path (such as parameters).
            *
            * @param URL.
            * @param locPara Return the path+parameter part.
            * @return Reference to the result string.
            */
            static std::string_view& getLocPara(const std::string_view &url, std::string_view &locPara);
            /**
            * @brief Get the query parameter string in the URL (including ?).
            * @note It can be a complete URL or an incomplete one, such as /path?id=123&name=abc
            *
            * @param URL.
            * @param para Return the parameter part (in the form of "?id=123&name=abc").
            * @return Reference to the result string.
            */
            static std::string_view& getPara(const std::string_view &url, std::string_view &para);



            /**
            * @brief Extract a substring between two markers from the original string.
            *
            * Extracts the content from a to b (excluding a and b), and a starting search position can be specified.
            * If a or b is an empty string, it means from the beginning or to the end, respectively.
            * If a is not found, it defaults to starting from the beginning.
            * If b is not found, it defaults to the end.
            *
            * @param ori_str Original string.
            * @param str String to store the extraction result.
            * @param a Start marker string.
            * @param b End marker string.
            * @param pos Starting search position.
            * @return Returns the position of b in ori_str (may return string::npos if b is not found or b is "")
            * @note If not found, the result string is ""
            */       
	        static size_t get_split_str(const std::string_view& ori_str, std::string &str, const std::string_view &a, const std::string_view &b, const size_t &pos = 0);
            /**
            * @brief Extract the value of a specified key from the URL query parameters.
            *
            * @note The URL does not need to be complete. For example, extracting the value of `id` from `?id=123&name=abc` is the same as extracting from `http://xxxx/?id=123&name=abc`.
            *
            * @param ori_str Original URL string.
            * @param str String to store the extraction result.
            * @param name Parameter name (key).
            * @return Reference to the result string.
            */
	        static std::string& get_value_str(const std::string& ori_str, std::string &str, const std::string& name);
            /**
            * @brief Extract the value of a specified field from the HTTP request header.
            *
            * @param ori_str Original HTTP request header string.
            * @param str Extraction result.
            * @param name Request header field name (e.g., "Host").
            * @return Reference to the result string.
            */
	        static std::string& get_value_header(const std::string& ori_str, std::string &str, const std::string& name);
            /**
            * @brief Extract the path and query part from the URL.
            *
            * For example, extract `/path` from `http://abc.com/path?query=123` or from `/path?query=123`.
            *
            * @param ori_str Original URL.
            * @param str Return the path part.
            * @return Reference to the result string.
            */
	        static std::string& get_location_str(const std::string& ori_str, std::string &str);
            /**
            * @brief Extract the path part of the URL (excluding the query).
            *
            * Similar to `get_location_str`, but retains all content after the path (such as parameters).
            *
            * @param URL.
            * @param locPara Return the path+parameter part.
            * @return Reference to the result string.
            */
            static std::string& getLocPara(const std::string &url, std::string &locPara);
            /**
            * @brief Get the query parameter string in the URL (including ?).
            * @note It can be a complete URL or an incomplete one, such as /path?id=123&name=abc
            *
            * @param URL.
            * @param para Return the parameter part (in the form of "?id=123&name=abc").
            * @return Reference to the result string.
            */
            static std::string& getPara(const std::string &url, std::string &para);
            /**
            * @brief Extract the host IP or domain name from the URL.
            *
            * For example, extract `127.0.0.1` from `http://127.0.0.1:8080/`.
            *
            * @warning The URL must be complete and explicitly specify the port
            * @param url Complete URL.
            * @param IP Store the extracted IP or domain name.
            * @return Reference to the result string.
            */
            static std::string& getIP(const std::string &url, std::string &IP);
            /**
             * @brief Extract the port number from the URL.
            *
            * For example, extract 8080 from `http://127.0.0.1:8080/`.
            *
            * @warning Must be a complete URL, and must explicitly specify the port and path, even if the path is empty, write a /
            * @param url Complete URL.
            * @param port Store the parsed port number.
            * @return Reference to the port number.
            */
            static int& getPort(const std::string &url, int &port);
            /**
            * @brief Create an HTTP request header field string.
            *
            * This function constructs a string in the format `field name: field value\r\n`.
            *
            * @param first The first field name.
            * @param second The first field value.
            * @return The constructed single HTTP request header string.
            */
            static std::string createHeader(const std::string& first, const std::string& second);
            /**
            * @brief Recursively construct multiple HTTP request header fields.
            *
            * Supports the construction of multiple field names and values, used as:
            * @code
            * std::string headers = createHeader("Host", "example.com", "Connection", "keep-alive,"Content-Type","charset=UTF-8");
            * @endcode
            * Finally generated:
            * @code
            * Host: example.com\r\n
            * Connection: keep-alive\r\n
            * Content-Type: charset=UTF-8\r\n
            * @endcode
            *
            * @param Args The remaining parameters, which need to be passed in pairs of (field name, field value).
            * @param first The current field name.
            * @param second The current field value.
            * @param args Subsequent field names and values (must be even in number).
            * @return The constructed complete HTTP request header string.
            */
	        template<class... Args>
	        static std::string createHeader(const std::string& first, const std::string& second, Args... args)
	        {
		        std::string cf = first + ": " + second + "\r\n" + createHeader(args...);
		        return cf;
	        }
        };
        /**
        * @brief Responsible for string operations related to the WebSocket protocol
        */
        class WebsocketStringUtil
        {
        public:
            /**
            * @brief Generate the Sec-WebSocket-Accept field value in the WebSocket handshake response.
            *
            * Based on the Sec-WebSocket-Key provided by the client, this function concatenates the WebSocket-specified magic GUID,
            * then performs SHA-1 hashing and Base64 encoding to obtain the accept string required for the handshake response.
            *
            * @param str The input is the Sec-WebSocket-Key provided by the client, which will be modified in place to the result string (Sec-WebSocket-Accept).
            * @return Reference to the modified str.
            *
            * @note The implementation refers to the handshake process in Section 4.2.2 of RFC 6455.
            */
            static std::string& transfer_websocket_key(std::string &str);
        };
        /**
        * @brief Responsible for conversion between strings and numbers
        */
        class NumberStringConvertUtil
        {
        public:
            /**
            * @brief Convert string to int type
            * @note Does not throw exceptions
            * @param ori_str Original string type data
            * @param result Int container to store the result
            * @param i If the conversion fails, assign result to i (default is -1)
            * @return Reference to result
            */
	        static int& toInt(const std::string_view&ori_str, int &result, const int &i = -1);
            /**
            * @brief Convert a hexadecimal number's string representation to a decimal int type number
            * @param ori_str String representation of a hexadecimal number
            * @param result Int container to save the conversion result
            * @return Reference to result
            */
            static int& str16toInt(const std::string_view&ori_str, int &result,const int &i=-1);
            /**
            * @brief Convert string to long type
            * @note Does not throw exceptions
            * @param ori_str Original string type data
            * @param result Long container to store the result
            * @param i If the conversion fails, assign result to i (default is -1)
            * @return Reference to result
            */
	        static long& toLong(const std::string_view&ori_str, long &result, const long &i = -1);
            /**
            * @brief Convert string to float type
            * @note Does not throw exceptions
            * @param ori_str Original string type data
            * @param result Float container to store the result
            * @param i If the conversion fails, assign result to i (default is -1)
            * @return Reference to result
            */
	        static float& toFloat(const std::string&ori_str, float &result, const float &i = -1);
            /**
            * @brief Convert string to double type
            * @note Does not throw exceptions
            * @param ori_str Original string type data
            * @param result Double container to store the result
            * @param i If the conversion fails, assign result to i (default is -1)
            * @return Reference to result
            */
	        static double& toDouble(const std::string&ori_str, double &result, const double &i = -1);
            /**
            * @brief Convert string to bool type
            * @note Does not throw exceptions, true or True, TRUE return true, otherwise return false
            * @param ori_str Original string type data
            * @param result Bool container to store the result
            * @return Reference to result
            */
	        static bool& toBool(const std::string_view&ori_str, bool &result);
            /**
            * @brief Convert a normal string to its corresponding hexadecimal representation string (hex string).
             *
            * For example, input "ABC" will be converted to "414243", and each character is converted to the hexadecimal form of its ASCII.
            *
            * @param ori_str Input original string (can contain any characters, including invisible characters).
            * @param result Reference to the string storing the conversion result.
            * @return Reference to the converted hexadecimal string result.
            * @bug There is a bug to be fixed
            */
            static std::string& strto16(const std::string &ori_str, std::string &result); // String to hexadecimal string     (Not needed for now) (To be fixed)
        };
        /**
        * @brief Data encoding/decoding, mask processing, etc.
        */
        class EncodingUtil
        {
        public:
            /**
            * @brief Base64 encode a string.
            *
            * Uses OpenSSL's BIO interface to Base64 encode the given string without inserting line breaks during encoding.
            *
            * @param input Original string to be encoded (can contain any binary data).
            * @return Encoded Base64 string.
            */
            static std::string base64_encode(const std::string &input);
            /**
            * @brief Decode a Base64 encoded string.
            *
            * Uses OpenSSL's BIO interface to decode the Base64 string. This function does not accept Base64 strings with line breaks.
            *
            * @param input Base64 encoded string.
            * @return Decoded original string.
            */
	        static std::string base64_decode(const std::string &input);
            /**
            * @brief Generate the Sec-WebSocket-Accept field value in the WebSocket handshake response.
            *
            * Based on the Sec-WebSocket-Key provided by the client, this function concatenates the WebSocket-specified magic GUID,
            * then performs SHA-1 hashing and Base64 encoding to obtain the accept string required for the handshake response.
            *
            * @param str The input is the Sec-WebSocket-Key provided by the client, which will be modified in place to the result string (Sec-WebSocket-Accept).
            * @return Reference to the modified str.
            *
            * @note The implementation refers to the handshake process in Section 4.2.2 of RFC 6455.
            */
	        static std::string& transfer_websocket_key(std::string &str);
            /**
            * @brief Generate a 32-bit (4-byte) random mask.
            *
            * This function first randomly generates a 32-bit string composed of '0' and '1' (e.g., "010110..."),
            * then converts it into the corresponding 4-byte binary data through the internal `BitUtil::toBit()` function.
            *
            * The conversion result is returned via the mask parameter, which is typically used to generate packet masks, encryption masks, bitmap masks, etc.
            *
            * @param mask Used to store the final generated 4-byte mask (binary string form).
            * @return Reference to mask.
            *
            * @note Internally depends on the function `BitUtil::toBit(const std::string&, std::string&)` to compress the 32-bit binary string into 4 bytes.
            */
            static std::string& generateMask_4(std::string &mask);
            /**
            * @brief Perform XOR operation (XOR Masking) on a string using a given 4-byte mask.
            *
            * This function XORs each byte of the input string data with the 4 bytes in the mask in sequence.
            * This operation is reversible and can be used to encrypt or decrypt masked data frames in WebSocket.
            *
            * @param data The data string to be XOR processed, which will be modified directly.
            * @param mask The string used as the mask, typically at least 4 bytes.
            * @return Reference to the processed data string.
            */
	        static std::string& maskCalculate(std::string &data, const std::string &mask);
        };
        /**
        * @brief class of solving json data
        */
        class JsonHelper
        {
            public:
            /**
            * @brief Extract the value or nested structure of a specified field in a JSON string.
            *
            * @param oriStr raw JSON string.
            * @param result stores a string reference to the return value.
            * @param type mode, optional values: value, arrayvalue. They are the values in ordinary JSON objects and the values in array objects.
            * @param name key name in JSON (value only).
            * @param num array index position (starting from 0) (for arrayvalue only).
            * @return -1: Extraction failed
            *           0: Returns a non-JSON object
            *           1: Returns a JSON object
            */
            static int getValue(const std::string &oriStr,std::string& result,const std::string &type="value",const std::string &name="a",const int &num=0);

            /**
            * @brief Convert Json::Value to a string.
            * @param val JSON value.
            * @return std::string String form of the value.
            */
            static std::string toString(const Json::Value &val);
            /**
            * @brief Parse a JSON string into Json::Value.
            * @param str Input JSON string.
            * @return Json::Value Parsed JSON object or array.
            */
	        static Json::Value toJsonArray(const std::string & str);
            /**
            * @brief Create a JSON string containing only one key-value pair.
            * 
            * @param T1 Type of the key, usually std::string.
            * @param T2 Type of the value, can be any type acceptable to Json::Value.
            * @param first Key.
            * @param second Value.
            * @return std::string JSON string.
            */
            template<class T1, class T2>
	        static std::string createJson(T1 first, T2 second)
	        {
		        Json::Value root;
		        //root[first] = second;
                if constexpr (std::is_integral_v<T2>) {
                    root[first] = Json::Value(static_cast<Json::Int64>(second));
                } else {
                    root[first] = second;
                }
		        Json::StreamWriterBuilder builder;
		        std::string jsonString = Json::writeString(builder, root);
		        return jsonString;
	        }
            /**
            * @brief Create a JSON string composed of multiple key-value pairs (recursive variadic template).
            * 
            * @param T1 Type of the first key.
            * @param T2 Type of the first value.
            * @param Args The remaining key-value parameters in pairs.
            * @param first The first key.
            * @param second The first value.
            * @param args The remaining key-value parameters.
            * @return std::string The concatenated JSON string.
            */
             template<class T1, class T2, class... Args>
	        static std::string createJson(T1 first, T2 second, Args... args)
	        {
		        Json::Value root;
		        //root[first] = second;
                if constexpr (std::is_integral_v<T2>) {
                    root[first] = Json::Value(static_cast<Json::Int64>(second));
                } else {
                    root[first] = second;
                }
		        std::string kk = createJson(args...);
		        Json::StreamWriterBuilder builder;
		        std::string jsonString = Json::writeString(builder, root);
		        jsonString = jsonString.erase(jsonString.length() - 2);
		        kk = kk.substr(1);
		        return jsonString + "," + kk;

	        }
            /**
            * @brief Create a JSON array string containing only one element.
            * 
            * @param T Any type.
            * @param first The first element.
            * @return std::string JSON array string.
            */
	        template<class T>
	        static std::string createArray(T first)
	        {
		        Json::Value root(Json::arrayValue);
		        root.append(first);
		        Json::StreamWriterBuilder builder;
		        std::string jsonString = Json::writeString(builder, root);
		        return jsonString;
	        }
            /**
            * @brief Create a JSON array string composed of multiple elements (recursive variadic template).
            * 
            * @param T Type of the first element.
            * @param Args Types of the remaining elements.
            * @param first The first element.
            * @param args The remaining elements.
            * @return std::string The concatenated JSON array string.
            */
            template<class T, class... Args>
	        static std::string createArray(T first, Args... args)
	        {
		        Json::Value root(Json::arrayValue);
		        root.append(first);
		        std::string kk = createArray(args...);
		        Json::StreamWriterBuilder builder;
		        std::string jsonString = Json::writeString(builder, root);
		        jsonString = jsonString.erase(jsonString.length() - 2);
		        kk = kk.substr(1);
		        return jsonString + "," + kk;

	        }
            /**
            * @brief Concatenate two JSON strings into a valid JSON (suitable for object or array concatenation).
            * @param a The first JSON string.
            * @param b The second JSON string.
            * @return std::string Concatenated JSON string.
            */
            static std::string jsonAdd(const std::string &a, const std::string &b);
             /**
            * @brief Remove indentation, spaces, etc., from a formatted JSON string to make it compact.
            * @param a Input JSON string.
            * @param b Reference to store the formatted result.
            * @return std::string& Reference to the compact format string.
            */
            static std::string& jsonFormatify(const std::string &a, std::string &b);
            /**
            * @brief Convert \uXXXX in a JSON string to UTF-8 characters.
            * @param input JSON string with Unicode encoding.
            * @param output Reference to store the converted string.
            * @return std::string& Reference to the converted string.
            */
            static std::string& jsonToUTF8(const std::string &input, std::string &output);
        };
    }

    /**
    * @brief API related to information security
    */
    namespace security
    {
        /**
 * @enum RateLimitType
 * @brief Rate limiting algorithm / strategy type.
 *
 * Each strategy has different semantics. Choose based on business traffic patterns
 * and your abuse/threat model.
 *
 * @par 1) Cooldown (Punishment-based / cooldown limiting)
 * - @b Rule:
 *   Once `times` events occur within `secs`, all subsequent requests are rejected.
 *   The limiter only recovers after the system stays quiet for a full `secs`.
 * - @b Characteristics:
 *   Extremely strong against continuous abuse and brute-force attempts;
 *   unfriendly to legitimate short bursts.
 * - @b Suitable for:
 *   - Connection flood / brute-force protection (connect stage)
 *   - Login failure punishment (can be extended to fail-based rules)
 *   - Rapid scripted retries
 * - @b Not suitable for:
 *   - Endpoints where short bursts are normal and "natural recovery" is expected
 *     (e.g. page load producing multiple HTTP requests)
 *
 * @par 2) FixedWindow (Fixed window counter)
 * - @b Rule:
 *   Use a fixed window of length `secs`. Allow at most `times` events within the window.
 *   Counter resets when the window ends.
 * - @b Characteristics:
 *   Simple and low overhead; vulnerable to boundary spikes (burst around window edges).
 * - @b Suitable for:
 *   - Low-risk scenarios requiring simplicity
 *   - Clear quota semantics: "at most M times per N seconds"
 * - @b Not suitable for:
 *   - Strong adversarial scenarios (can be exploited via boundary behavior)
 *
 * @par 3) SlidingWindow (Sliding window / timestamp queue)
 * - @b Rule:
 *   At any time `now`, count events in (now - secs, now]; reject if count >= times.
 * - @b Characteristics:
 *   Fair and smooth; no boundary spike. Requires a timestamp deque (higher cost).
 * - @b Suitable for:
 *   - Public HTTP APIs, user operations where fairness is desired
 *   - Critical endpoints (e.g. /login, /register) at path-level
 * - @b Not suitable for:
 *   - Extremely high-QPS keys with large per-key history where memory/cost is tight
 *     (consider approximate buckets)
 *
 * @par 4) TokenBucket (Token bucket)
 * - @b Rule (common semantics):
 *   Tokens refill at a fixed rate. Each request consumes 1 token. If no token => reject.
 *   In this API, (times, secs) roughly maps to average rate ~= times/secs.
 *   Bucket capacity is typically on the same order as `times` (implementation-defined).
 * - @b Characteristics:
 *   Allows bursts (when tokens accumulated) while limiting long-term average.
 *   Very commonly used in production.
 * - @b Suitable for:
 *   - Traffic shaping (allow short bursts but control long-term average)
 *   - IM messages / HTTP requests where you want "not too hard, not too soft"
 * - @b Not suitable for:
 *   - Hard punishment requirement ("once exceeded, must stop for a while"):
 *     use Cooldown instead.
 */
enum class RateLimitType
{
    Cooldown,      // Punishment-based: hit limit => cooldown, require quiet secs to recover
    FixedWindow,   // Fixed window counter: each secs is a window, allow up to times
    SlidingWindow, // Sliding window: count events within the last secs (timestamp deque)
    TokenBucket    // Token bucket: allow bursts + control long-term average rate
};


/**
 * @struct RateState
 * @brief Runtime state for one limiter key (reused by multiple strategies).
 *
 * @details
 * Stores the live state for one limiting dimension (IP / fd / path, etc.).
 * Different algorithms reuse different fields:
 *
 * - Cooldown / FixedWindow:
 *   - counter + lastTime
 * - SlidingWindow:
 *   - history (timestamp deque)
 * - TokenBucket:
 *   - tokens + lastRefill
 *
 * @note
 * - `violations` counts how many times requests were rejected. This is useful for
 *   defense escalation (DROP / CLOSE / temporary ban).
 * - RateState does NOT make decisions. It only stores state.
 */
struct RateState
{
    // Common
    int counter = 0;
    int violations = 0;
    std::chrono::steady_clock::time_point lastTime{};

    // SlidingWindow
    std::deque<std::chrono::steady_clock::time_point> history;

    // TokenBucket
    double tokens = 0.0;
    std::chrono::steady_clock::time_point lastRefill{};
};


/**
 * @struct ConnectionState
 * @brief Security and limiter state for a single connection (fd).
 *
 * @details
 * Each fd that passes allowConnect() owns a ConnectionState, enabling fd-level defense:
 *
 * - requestRate: overall request-rate limiter for this connection
 * - pathRate: per-path extra limiter states
 * - lastActivity: last activity time (zombie/idle detection)
 *
 * @note
 * - One IP may have multiple ConnectionState instances (multiple concurrent fds).
 * - Must be cleaned up when the connection is closed.
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
 * @brief Security state and connection set for a single IP.
 *
 * @details
 * Core state unit of ConnectionLimiter, implementing layered IP-level + fd-level defense:
 *
 * - activeConnections: concurrent connections from this IP
 * - connectRate: IP-level connection rate limiter state
 * - badScore: accumulated risk score (escalation trigger)
 * - conns: all active connections for this IP (fd -> ConnectionState)
 *
 * @par Defense semantics
 * - activeConnections / connectRate:
 *   - Protect against connection floods and brute-force
 * - badScore:
 *   - Used to escalate defense (disconnect / temporary ban)
 *
 * @note
 * - Blacklisting (ban TTL) is managed by ConnectionLimiter globally.
 * - badScore can be decayed over time or reset after a ban.
 */
struct IPInformation
{
    int activeConnections = 0;
    RateState connectRate;
    int badScore = 0; // IP risk score used for escalation
    std::unordered_map<int, ConnectionState> conns; // fd -> state
};


/**
 * @enum DefenseDecision
 * @brief Security decision returned by ConnectionLimiter.
 *
 * @details
 * Every connection/request must pass the limiter before entering business logic.
 *
 * - ALLOW (0):
 *   - Continue processing
 * - DROP (1):
 *   - Silently ignore the request (no response)
 *   - Intended for lightweight defense at request stage
 * - CLOSE (2):
 *   - Close the connection immediately
 *   - May be accompanied by risk escalation or temporary ban
 *
 * @note
 * - Connect stage typically uses only ALLOW / CLOSE.
 * - DROP is primarily for request stage (fd already exists).
 */
enum DefenseDecision : int
{
    ALLOW = 0,
    DROP  = 1,
    CLOSE = 2
};


/**
 * @class ConnectionLimiter
 * @brief Unified connection & request security gate
 *        (IP-level + fd-level, multi-strategy limiting + blacklist).
 *
 * @details
 * ConnectionLimiter is a "Security Gate".
 * All connections and requests must pass through it before business logic runs.
 *
 * This class does NOT directly perform side effects (close/send/sleep).
 * Instead, it returns a @ref DefenseDecision for the caller to enforce.
 *
 * ---
 * @section design_overview Design Overview
 *
 * Layered defense model:
 *
 * - IP-level defense:
 *   - Concurrent connection limit (maxConnections)
 *   - Connection rate limit (connectRate)
 *   - IP risk scoring (badScore)
 *   - Temporary blacklist with TTL (blacklist)
 *
 * - fd-level defense:
 *   - Per-connection request rate (requestRate)
 *   - Per-path extra limits (pathRate)
 *   - Activity tracking (lastActivity) for zombie detection
 *
 * ---
 * @section defense_decision Decision Semantics
 *
 * allowConnect / allowRequest return @ref DefenseDecision:
 *
 * - ALLOW (0): proceed
 * - DROP  (1): ignore silently (request stage only)
 * - CLOSE (2): close immediately (may escalate / ban)
 *
 * @note
 * - Connect stage usually uses only ALLOW / CLOSE.
 * - DROP is mainly used in request stage.
 *
 * ---
 * @section rate_limit Strategies
 *
 * Supported algorithms (see @ref RateLimitType):
 * - Cooldown
 * - FixedWindow
 * - SlidingWindow
 * - TokenBucket
 *
 * Defaults:
 * - connectStrategy: Cooldown
 * - requestStrategy: SlidingWindow
 * - pathStrategy: SlidingWindow
 *
 * ---
 * @section thread_safety Thread Safety
 *
 * @warning
 * This class is NOT thread-safe by itself.
 * Concurrent access to internal tables (table/pathConfig/blacklist)
 * must be protected externally (e.g. single event-loop thread, or a mutex).
 *
 * ---
 * @section lifecycle Lifecycle
 *
 * - allowConnect:
 *   - Decide whether a new connection is allowed
 *   - On ALLOW, register the fd under the IP
 *
 * - allowRequest:
 *   - Decide whether a request on an existing fd is allowed
 *
 * - clearIP:
 *   - Called when a connection is closed, to reclaim state and counters
 *
 * - connectionDetect:
 *   - Detect and cleanup idle/zombie connections (should be called by a timer)
 */
class ConnectionLimiter
{
public:
    /**
     * @brief Constructor.
     *
     * @param maxConn Max concurrent connections allowed per IP (activeConnections cap).
     * @param idleTimeout Idle timeout (seconds) used for zombie detection. If < 0, disable.
     */
    ConnectionLimiter(const int& maxConn = 20, const int& idleTimeout = 60)
        : maxConnections(maxConn), connectionTimeout(idleTimeout) {}

    // ========== Strategy configuration ==========

    /**
     * @brief Set the strategy used for connection-rate limiting.
     *
     * @param type Strategy type, see @ref RateLimitType.
     * @note Default is @ref RateLimitType::Cooldown.
     */
    void setConnectStrategy(const RateLimitType &type);

    /**
     * @brief Set the strategy used for fd-level request limiting.
     *
     * @param type Strategy type, see @ref RateLimitType.
     * @note Default is @ref RateLimitType::SlidingWindow.
     */
    void setRequestStrategy(const RateLimitType &type);

    /**
     * @brief Set the strategy used for path-level extra limiting.
     *
     * @param type Strategy type, see @ref RateLimitType.
     * @note Default is @ref RateLimitType::SlidingWindow.
     */
    void setPathStrategy(const RateLimitType &type);

    // ========== Path configuration ==========

    /**
     * @brief Configure extra rate limits for a specific path (path-level rule).
     *
     * @param path Target path, e.g. "/login", "/register".
     * @param times Maximum allowed requests within `secs`.
     * @param secs Window size (seconds).
     *
     * @note
     * setPathLimit() defines an @b additional rule:
     * - The (times, secs) passed into allowRequest() is still applied first
     *   as the connection/IP-level rule.
     * - If `path` matches a configured rule, the path-level rule is evaluated next.
     * - Relationship is AND: any layer failing results in rejection.
     */
    void setPathLimit(const std::string &path, const int &times, const int &secs);

    // ========== Core decisions ==========

    /**
     * @brief Security decision for a newly accepted connection (IP-level gate).
     *
     * @param ip Remote IP address.
     * @param fd Newly accepted file descriptor.
     * @param times Maximum allowed connection attempts within `secs`.
     * @param secs Connection-rate window size (seconds).
     *
     * @return DefenseDecision
     * - ALLOW: connection is allowed and fd will be registered
     * - CLOSE: reject and caller should close the connection immediately
     *
     * @note
     * - Connect stage typically does not use DROP.
     * - If the IP is blacklisted or in a high-risk state, returns CLOSE directly.
     */
    DefenseDecision allowConnect(const std::string &ip, const int &fd,
                                const int &times, const int &secs);

    /**
     * @brief Security decision for a single request on an existing connection.
     *
     * @param ip Remote IP address.
     * @param fd File descriptor associated with the request.
     * @param path Request path (used for path-level extra limiting).
     * @param times Request-rate limit (max requests within `secs`).
     * @param secs Request-rate window size (seconds).
     *
     * @return DefenseDecision
     * - ALLOW: process normally
     * - DROP: ignore silently (no response)
     * - CLOSE: close connection
     */
    DefenseDecision allowRequest(const std::string &ip, const int &fd,
                                const std::string_view &path,
                                const int &times, const int &secs);

    // ========== Lifecycle ==========

    /**
     * @brief Reclaim state for an fd when the connection is closed.
     *
     * @param ip Remote IP address.
     * @param fd Closed file descriptor.
     *
     * @note
     * - Must be called after close(fd).
     * - Keeps activeConnections and internal state consistent.
     */
    void clearIP(const std::string &ip, const int &fd);

    /**
     * @brief Detect and cleanup an idle/zombie connection.
     *
     * @param ip Remote IP address.
     * @param fd File descriptor to check.
     *
     * @return true  The connection is considered zombie and has been cleaned up.
     * @return false Not timed out or not found.
     *
     * @note
     * - "Activity" means allowConnect()/allowRequest() updates lastActivity.
     * - Prefer calling via a timer instead of scanning hot paths.
     */
    bool connectionDetect(const std::string &ip, const int &fd);
    /**
 * @brief Immediately add the specified IP to the blacklist (direct ban).
 *
 * @details
 * This function is used when a client exhibits @b clearly malicious behavior.
 * It bypasses score-based and progressive penalties and directly bans the IP
 * by inserting it into the blacklist.
 *
 * Ban semantics:
 * - If the IP is not currently blacklisted: it will be added;
 * - If the IP is already blacklisted: the ban expiration time will be
 *   @b refreshed (overwritten);
 * - If @p banSeconds < 0: the ban is permanent
 *   (implemented using @c std::chrono::steady_clock::time_point::max).
 *
 * @param ip The IP address to be banned.
 * @param banSeconds Ban duration in seconds:
 *   - > 0 : ban for @p banSeconds seconds (temporary ban)
 *   - = 0 : no operation
 *   - < 0 : permanent ban
 * @param reasonCN Ban reason in Chinese (for logging).
 * @param reasonEN Ban reason in English (for logging).
 *
 * @note
 * - This function does @b not immediately close existing connections;
 *   the caller should close the corresponding fd after receiving
 *   a @ref DefenseDecision::CLOSE decision.
 * - Uses @c std::chrono::steady_clock and is not affected by system time changes.
 * - This function represents a @b terminal security action and should be
 *   used with caution.
 *
 * @note
 * - If the IP is already banned and the existing expiration time is later than
 *   the new one, the longer ban will be preserved (the ban will not be shortened).
 */
void banIP(const std::string &ip,
           int banSeconds,
           const std::string &reasonCN,
           const std::string &reasonEN);
/**
 * @brief Manually remove an IP address from the blacklist.
 *
 * @param ip The IP address to be unbanned.
 */
void unbanIP(const std::string &ip);
/**
 * @brief Check whether the specified IP address is currently banned.
 *
 * @param ip The IP address to check.
 * @return true  The IP is currently banned.
 * @return false The IP is not banned or the ban has expired.
 */
bool isBanned(const std::string &ip) const;


private:
    // Core limiter primitive
    bool allow(RateState &st, const RateLimitType &type,
               const int &times, const int &secs,
               const std::chrono::steady_clock::time_point &now);

private:
    int maxConnections;
    int connectionTimeout;

    RateLimitType connectStrategy = RateLimitType::Cooldown;
    RateLimitType requestStrategy = RateLimitType::SlidingWindow;
    RateLimitType pathStrategy    = RateLimitType::SlidingWindow;

    std::unordered_map<std::string, IPInformation> table;
    std::unordered_map<std::string, std::pair<int,int>> pathConfig;

    // IP -> unban time
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> blacklist;

    inline void logSecurity(const std::string &msgCN, const std::string &msgEN);
};

    }

    /**
    * @namespace stt::network
    * @brief Network framework, protocols, communication, and IO multiplexing related
    * @ingroup stt
    */
    namespace network
    {
    /**
    * @brief TCP socket operation class
    */
    class TcpFDHandler
    {
    protected:
        int fd = -1;
        bool flag1 = false;
        bool flag2 = false;
        SSL *ssl = nullptr;
        int sec=-1;
    public:
        /**
        * @brief if the connection lost while using sendData function with block=true，this flag will be set to true
        */
        bool flag3 = false;
    public:
        /**
        * @brief Initialize the object with a socket
        * @note Can choose to set the socket to blocking or non-blocking, SO_REUSEADDR mode (only applicable to server sockets),
        *       and can also choose the SSL handle after encrypting the socket
        * @param fd Socket descriptor
        * @param ssl SSL handle after TLS encryption of the fd (can be nullptr if none)
        * @param flag1 true: Enable non-blocking mode, false: Enable blocking mode (default is false, i.e., blocking mode)
        * @param flag2 true: Enable SO_REUSEADDR mode, false: Do not enable SO_REUSEADDR mode (default is false)
        * @param sec blocking timeout Blocking beyond this time will not be blocked again The default is -1, that is, infinite waiting
        */
        void setFD(const int &fd, SSL *ssl, const bool &flag1 = false, const bool &flag2 = false,const int &sec=-1);
        /**
        * @brief Get the socket of this object
        * @return The socket of this object
        */
        int getFD() { return fd; }
        /**
        * @brief Get the encrypted SSL handle of this object
        * @return The encrypted SSL handle. Returns nullptr if no encrypted SSL handle exists.
        */
        SSL *getSSL() { return ssl; }
        /**
        * @brief Close the object
        * @param cle true: Close the object and the socket connection, false: Only close the object (default is true)
        */
        void close(const bool &cle = true);
        /**
        * @brief Set the socket in the object to blocking mode
        * @param sec Blocking timeout, no longer block if blocking exceeds this time, default is -1, i.e., infinite wait
        */
        void blockSet(const int &sec = -1);
        /**
        * @brief Set the socket in the object to non-blocking mode
        */
        void unblockSet();
        /**
        * @brief Set the socket in the object to SO_REUSEADDR mode
        */
        bool multiUseSet();
        /**
        * @brief Determine if the object has a socket bound
        * @return true: The object has a socket bound, false: The object has no socket bound
        */
        bool isConnect() { if (fd == -1) return false; else return true; }
    public:
        /**
        * @brief Send string data to the connected socket.
        *
        * @param data Data content to be sent (std::string type).
        * @param block Whether to send in blocking mode (default true).
        *              - true: Will block until all data is sent successfully unless an error occurs (regardless of socket blocking state); 
        *                     Check the flag3 flag to determine if the connection is disconnected.
        *              - false: Blocking depends on socket state.
        * 
        * @return
        * - Return value > 0: Number of bytes successfully sent;
        * - Return value = 0: Connection closed (block=false) or 0 bytes sent successfully (block=true);
        * - Return value < 0 (only when block=false): Sending failed;
        *   - -99: Object not bound to a socket;
        *   - -100: Send buffer full in non-blocking mode.
        *
        * @note If block is true, it will continue to block until all data is sent unless an error occurs (regardless of socket blocking state), 
        *       and the return value is always >= 0, suitable for scenarios requiring complete sending. 
        *       Check the flag3 flag to determine if the connection is disconnected.
        *       If block is false, blocking depends on socket state. The return value may be less than the expected length, 
        *       requiring manual handling of remaining data.
        */
        int sendData(const std::string &data, const bool &block = true);
        /**
        * @brief Send a specified length of binary data to the connected socket.
        *
        * @param data Pointer to the data buffer to be sent.
        * @param length Data length (bytes).
        * @param block Whether to send in blocking mode (default true).
        *              - true: Will block until all data is sent successfully unless an error occurs (regardless of socket blocking state); 
        *                     Check the flag3 flag to determine if the connection is disconnected.
        *              - false: Blocking depends on socket state.
        * 
        * @return
        * - Return value > 0: Number of bytes successfully sent;
        * - Return value = 0: Connection closed (block=false) or 0 bytes sent successfully (block=true);
        * - Return value < 0 (only when block=false): Sending failed;
        *   - -99: Object not bound to a socket;
        *   - -100: Send buffer full in non-blocking mode.
        *
        * @note If block is true, it will continue to block until all data is sent unless an error occurs (regardless of socket blocking state), 
        *       and the return value is always >= 0, suitable for scenarios requiring complete sending. 
        *       Check the flag3 flag to determine if the connection is disconnected.
        *       If block is false, blocking depends on socket state. The return value may be less than length, 
        *       requiring manual handling of remaining data.
        */
        int sendData(const char *data, const uint64_t &length, const bool &block = true);
        /**
        * @brief Blocking receive of specified length of data from a connected socket into a string
        *
        * @param data Data container for received data (string type)
        * @param length Reception length
        * @param sec Waiting time in seconds, -1 for infinite blocking (default is 2s)
        * @return
        * - > 0: Number of bytes successfully received;
        * - = 0: Connection closed;
        * - < 0: Reception failed;
        *   - -99: Object not bound to a socket;
        *   - -100: Timeout
        *
        * @note If the specified data size is not received, it will block until timeout or error
        */
        int recvDataByLength(std::string &data, const uint64_t &length, const int &sec = 2);
        /**
        * @brief Blocking receive of specified length of data from a connected socket into a char* container
        *
        * @param data Data container for received data (char* type)
        * @param length Reception length
        * @param sec Waiting time in seconds, -1 for infinite blocking (default is 2s)
        * @return
        * - > 0: Number of bytes successfully received;
        * - = 0: Connection closed;
        * - < 0: Reception failed;
        *   - -99: Object not bound to a socket;
        *   - -100: Timeout
        *
        * @note If the specified data size is not received, it will block until timeout or error
        */
        int recvDataByLength(char *data, const uint64_t &length, const int &sec = 2);
        /**
        * @brief Receive data once from a connected socket into a string container
        * @param data Data container for received data (string type)
        * @param length Maximum reception length
        * @return 
        * - > 0: Number of bytes successfully received;
        * - = 0: Connection closed;
        * - < 0: Reception failed;
        *   - -99: Object not bound to a socket;
        *   - -100: No data in non-blocking mode
        * @note Whether reception blocks depends on the fd's blocking state
        */
        int recvData(std::string &data, const uint64_t &length);
        /**
        * @brief Receive data once from a connected socket into a char* container
        * @param data Data container for received data (char* type)
        * @param length Maximum reception length
        * @return 
        * - > 0: Number of bytes successfully received;
        * - = 0: Connection closed;
        * - < 0: Reception failed;
        *   - -99: Object not bound to a socket;
        *   - -100: No data in non-blocking mode
        * @note Whether reception blocks depends on the fd's blocking state
        */
        int recvData(char *data, const uint64_t &length);
    };

    /**
    * @brief TCP client operation class
    * @note Blocking mode by default
    */
    class TcpClient : public TcpFDHandler
    {
    private:
        std::string serverIP = "";
        int serverPort = -1;
        bool flag = false;
        bool TLS;
        SSL_CTX *ctx = nullptr;
        const char *ca;
        const char *cert;
        const char *key;
        const char *passwd;
    private:
        bool createFD();
        void closeAndUnCreate();
        bool initCTX(const char *ca, const char *cert = "", const char *key = "", const char *passwd = "");
    public:
        /**
        * @brief Constructor of TcpClient class
        * @param TLS true: Enable TLS encryption, false: Do not enable TLS encryption (default is false)
        * @param ca Path to CA root certificate (must be filled if TLS encryption is enabled, default is empty)
        * @param cert Path to client certificate (optional, default is empty)
        * @param key Path to client private key (optional, default is empty)
        * @param passwd Password for private key decryption (optional, default is empty)
        * @note 
        * - ca is used to verify whether the server's certificate is trustworthy (can use the operating system's built-in root certificate for verification)
        * - If TLS encryption is enabled, ca is required, others are optional
        * - If the server requires client authentication (two-way TLS/SSL), you need to provide a valid client certificate.
        */
        TcpClient(const bool &TLS = false, const char *ca = "", const char *cert = "", const char *key = "", const char *passwd = "");
        /**
        * @brief Initiate a TCP connection to the server
        * @param ip Server IP
        * @param port Server port
        * @return true: Connection successful, false: Connection failed
        */
        bool connect(const std::string &ip, const int &port);        
        /**
        * @brief Reset TLS encryption parameters for the first time or reconfigure
        * @note The set TLS parameters will accompany the entire lifecycle unless this function is called to reset
        * @param TLS true: Enable TLS encryption, false: Do not enable TLS encryption (default is false)
        * @param ca Path to CA root certificate (must be filled if TLS encryption is enabled, default is empty)
        * @param cert Path to client certificate (optional, default is empty)
        * @param key Path to client private key (optional, default is empty)
        * @param passwd Password for private key decryption (optional, default is empty)
        * @note 
        * - ca is used to verify whether the server's certificate is trustworthy
        * - If TLS encryption is enabled, ca is required, others are optional
        * - If the server requires client authentication (two-way TLS/SSL), you need to provide a valid client certificate.
        */
        void resetCTX(const bool &TLS = false, const char *ca = "", const char *cert = "", const char *key = "", const char *passwd = "");
        /**
        * @brief If the object has a socket connection, close and release the connection and socket, and create a new socket.
        * @return true: Closed successfully and new socket created successfully, false: Closed successfully but new socket creation failed
        */
        bool close();
        /**
        * @brief Destructor of TcpClient, closes and releases the socket and its connection
        */
        ~TcpClient() { closeAndUnCreate(); }
    public:
        /**
        * @brief Return the IP of the connected server
        * @return IP of the connected server
        */
        std::string getServerIP() { return serverIP; }
        /**
        * @brief Return the port of the connected client
        * @return Port of the connected server
        */
        int getServerPort() { return serverPort; }
        /**
        * @brief Return the connection status of the object
        * @return true: Connected, false: Not connected
        */
        bool isConnect() { return flag; }
    };  

    /**
    * @brief Http/Https client operation class
    * @note 
    * - The request header will automatically include Connection: keep-alive
    * - If you need to reset the TLS/Https encryption certificate, you currently need to destroy the object and reconstruct it
    * - If no socket function is passed, the underlying TCP is blocking by default
    */
    class HttpClient : private TcpClient
    {
    private:
        bool flag = false;
    public:
        /**
        * @brief Constructor of HttpClient class
        * @param TLS true: Enable Https encryption, false: Do not enable Https encryption (default is false)
        * @param ca Path to CA root certificate (must be filled if TLS encryption is enabled, default is empty)
        * @param cert Path to client certificate (optional, default is empty)
        * @param key Path to client private key (optional, default is empty)
        * @param passwd Password for private key decryption (optional, default is empty)
        * @note 
        * - ca is used to verify whether the server's certificate is trustworthy
        * - If Https encryption is enabled, ca is required, others are optional
        * - If the server requires client authentication (two-way TLS/SSL), you need to provide a valid client certificate.
        */
        HttpClient(const bool &TLS = false, const char *ca = "", const char *cert = "", const char *key = "", const char *passwd = "") : TcpClient(TLS, ca, cert, key, passwd) {}
    public:
        /**
        * @brief Send a GET request to the server
        * @note To get the result, please call the isReturn function; if there is a result, the response header and body 
        *       are stored in the global variables header and body.
        * @note If TLS is used, the https protocol will be automatically adopted, otherwise the http protocol will be used
        * @note is blocking by default
        * @param url Complete http/https url (note that the port and path need to be specified explicitly) 
        *        e.g., https://google.com should be written as https://google.com:443/ (complete with :443 and /)
        * @param header Http request header; if not generated by createHeader, remember to add \r\n at the end.
        * @param header1 Additional item for the HTTP request header; if needed, must fill in a valid item; 
        *        no need to add \r\n at the end (cannot use createHeader). (keepalive item is filled in by default)
        * @param sec blocking timeout (s) Blocking beyond this time will no longer block The default is -1, i.e. infinite waiting
        * @return true: Request sent successfully, false: Request sending failed 
        *         Note: This does not mean whether a response is received, only that the sending was successful
        * @warning Requires a complete http/https protocol url (note that the port and path need to be specified explicitly 
        *          even if the path is not needed, fill in /) e.g., https://google.com should be written as https://google.com:443/
        */
        bool getRequest(const std::string &url, const std::string &header = "", const std::string &header1 = "Connection: keep-alive",const int &sec=-1);
        /**
        * @brief Send a POST request to the server
        * @note To get the result, please call the isReturn function; if there is a result, the response header and body 
        *       are stored in the global variables header and body.
        * @note If TLS is used, the https protocol will be automatically adopted, otherwise the http protocol will be used
        * @note is blocking by default
        * @param url Complete http/https url (note that the port and path need to be specified explicitly) 
        *        e.g., https://google.com should be written as https://google.com:443/ (complete with :443 and /)
        * @param body Http request body
        * @param header Http request header; if not generated by createHeader, remember to add \r\n at the end.
        * @param header1 Additional item for the HTTP request header; if needed, must fill in a valid item; 
        *        no need to add \r\n at the end (cannot use createHeader). (keepalive item is filled in by default)
        * @param sec blocking timeout (s) Blocking beyond this time will no longer block The default is -1, i.e. infinite waiting
        * @return true: Request sent successfully, false: Request sending failed 
        *         Note: This does not mean whether a response is received, only that the sending was successful
        * @warning Requires a complete http/https protocol url (note that the port and path need to be specified explicitly 
        *          even if the path is not needed, fill in /) e.g., https://google.com should be written as https://google.com:443/
        */
        bool postRequest(const std::string &url, const std::string &body = "", const std::string &header = "", const std::string &header1 = "Connection: keep-alive",const int &sec=-1);
        /**
        * @brief Send a GET request to the server from a tcp socket
        * @note To get the result, please call the isReturn function; if there is a result, the response header and body 
        *       are stored in the global variables header and body.
        * @note If ssl is not nullptr, the https protocol will be automatically adopted, otherwise the http protocol will be used
        * @note When calling, it will be blocked, change the status of the original FD, pay attention to backup and restore
        * @param fd tcp socket
        * @param ssl TLS encrypted socket
        * @param url Complete http/https url (note that the port and path need to be specified explicitly) 
        *        e.g., https://google.com should be written as https://google.com:443/ (complete with :443 and /)
        * @param header Http request header; if not generated by createHeader, remember to add \r\n at the end.
        * @param header1 Additional item for the HTTP request header; if needed, must fill in a valid item; 
        *        no need to add \r\n at the end (cannot use createHeader). (keepalive item is filled in by default)
        * @param sec blocking timeout (s) Blocking beyond this time will no longer block The default is 2s
        * @return true: Request sent successfully, false: Request sending failed 
        *         Note: This does not mean whether a response is received, only that the sending was successful
        * @warning Requires a complete http/https protocol url (note that the port and path need to be specified explicitly 
        *          even if the path is not needed, fill in /) e.g., https://google.com should be written as https://google.com:443/
        */
        bool getRequestFromFD(const int &fd, SSL *ssl, const std::string &url, const std::string &header = "", const std::string &header1 = "Connection: keep-alive",const int &sec=2);
        /**
        * @brief Send a POST request to the server
        * @note To get the result, please call the isReturn function; if there is a result, the response header and body 
        *       are stored in the global variables header and body.
        * @note If ssl is not nullptr, the https protocol will be automatically adopted, otherwise the http protocol will be used
        * @note When calling, it will be blocked, change the status of the original FD, pay attention to backup and restore
        * @param fd tcp socket
        * @param ssl TLS encrypted socket
        * @param url Complete http/https url (note that the port and path need to be specified explicitly) 
        *        e.g., https://google.com should be written as https://google.com:443/ (complete with :443 and /)
        * @param body http request body
        * @param header Http request header; if not generated by createHeader, remember to add \r\n at the end.
        * @param header1 Additional item for the HTTP request header; if needed, must fill in a valid item; 
        *        no need to add \r\n at the end (cannot use createHeader). (keepalive item is filled in by default)
        * @param sec blocking timeout (s) Blocking beyond this time will no longer block The default is 2s
        * @return true: Request sent successfully, false: Request sending failed 
        *         Note: This does not mean whether a response is received, only that the sending was successful
        * @warning Requires a complete http/https protocol url (note that the port and path need to be specified explicitly 
        *          even if the path is not needed, fill in /) e.g., https://google.com should be written as https://google.com:443/
        */
        bool postRequestFromFD(const int &fd, SSL *ssl, const std::string &url, const std::string &body = "", const std::string &header = "", const std::string &header1 = "Connection: keep-alive",const int &sec=2);
    public:
        /**
        * @brief Get the server response status
        * @return true: Server response successful, false: Server response failed
        */
        bool isReturn() { return flag; }
        /**
        * @brief Server response header
        */
        std::string header = "";
        /**
        * @brief Server response body
        */
        std::string body = "";
    };
    
    /**
    * @brief Listen to a single handle with epoll
    */
    class EpollSingle
    {
    private:
        int fd;
        bool flag = true;
        std::function<bool(const int &fd)> fc = [](const int &fd)->bool
        {return true;};
        std::function<void(const int &fd)> fcEnd = [](const int &fd)->void
        {};
        std::function<bool(const int &fd)> fcTimeOut = [](const int &fd)->bool
        {return true;};
        bool flag1 = true;
        bool flag2 = false;
        time::Duration dt{0,20,0,0,0};
        bool flag3 = false;
        time::Duration t;
    private:
        void epolll();
    public:
        /**
        * @brief Start listening
        * @param fd Handle to be listened to
        * @param flag true: Level triggering, false: Edge triggering
        * @param dt Listening timeout duration
        */
        void startListen(const int &fd, const bool &flag = true, const time::Duration &dt = time::Duration{0,0,20,0,0});
    public:
        /**
        * @brief Return epoll listening status
        * @return true: Listening, false: Not listening
        */
        bool isListen() { return flag2; }
        /**
        * @brief Set the processing function after epoll triggering
        * Register a callback function
        * @param fc A function or function object for callback processing when data is available
        * - Parameter: const int &fd - Socket to be processed
        * - Return: bool - true for successful processing, false for processing failure
        * @note The passed function should have the signature bool func(const int &fd)
        * @note If processing fails, epoll listening will exit (socket is not closed)
        */
        void setFunction(std::function<bool(const int &fd)> fc) { this->fc = fc; }
        /**
        * @brief Set the callback function before epoll exits
        * Register a callback function
        * @param fcEnd A function or function object for processing the epoll exit process
        * - Parameter: const int &fd - Socket to be processed
        * @note The passed function should have the signature void func(const int &fd)
        */
        void setEndFunction(std::function<void(const int &fd)> fcEnd) { this->fcEnd = fcEnd; };
        /**
        * @brief Set the callback function triggered after epoll timeout
        * Register a callback function
        * @param fc A function or function object for callback processing when epoll times out
        * - Parameter: const int &fd - Socket to be processed
        * - Return: bool - true for successful processing, false for processing failure
        * @note The passed function should have the signature bool func(const int &fd)
        * @note If processing fails, epoll listening will exit (socket is not closed)
        */
        void setTimeOutFunction(std::function<bool(const int &fd)> fcTimeOut) { this->fcTimeOut = fcTimeOut; };
        /**
        * @brief End epoll listening
        * Will block until epoll exits completely
        * @return true: Processing ended, false: Ending failed
        */
        bool endListen();
        /**
        * @brief Send a signal to end epoll
        * @note Only sends the signal, does not follow up on subsequent logic
        */
        void endListenWithSignal() { flag1 = false; }
        /**
        * @brief Start the epoll exit countdown until new messages arrive on the socket
        * If no new messages arrive by the end of the countdown, epoll will exit
        * @param t A Duration object for the countdown duration (default is 10 seconds)
        */
        void waitAndQuit(const time::Duration &t = time::Duration{0,0,0,10,10}) { flag3 = true; this->t = t; }
        /**
        * @brief Destructor of EpollSingle
        * Calls eldListen to block and exit epoll
        */
        ~EpollSingle() { endListen(); }
    };
    
    /**
    * @brief Websocket client operation class
    * - If you need to reset the TLS/Https encryption certificate, you currently need to destroy the object and reconstruct it
    * The underlying TCP is blocking by default
    */
    class WebSocketClient : private TcpClient 
    {
    private:
        bool flag4 = false;
        std::function<bool(const std::string &message, WebSocketClient &k)> fc = [](const std::string &message, WebSocketClient &k)->bool
        {std::cout<<"Received: "<<message<<std::endl;return true;};
        std::string url;
        EpollSingle k;
        bool flag5 = false;
    private:
        bool close1();
    public:
        /**
        * @brief Constructor of WebSocketClient class
        * @param TLS true: Enable wss encryption, false: Do not enable wss encryption (default is false)
        * @param ca Path to CA root certificate (must be filled if TLS encryption is enabled, default is empty)
        * @param cert Path to client certificate (optional, default is empty)
        * @param key Path to client private key (optional, default is empty)
        * @param passwd Password for private key decryption (optional, default is empty)
        * @note 
        * - ca is used to verify whether the server's certificate is trustworthy
        * - If wss encryption is enabled, ca is required, others are optional
        * - If the server requires client authentication (two-way TLS/SSL), you need to provide a valid client certificate.
        */
        WebSocketClient(const bool &TLS = false, const char *ca = "", const char *cert = "", const char *key = "", const char *passwd = "") : TcpClient(TLS, ca, cert, key, passwd) {}
        /**
        * @brief Set the callback function after receiving a message from the server
        * Register a callback function
        * @param fc A function or function object for processing logic after receiving a message from the server
        * - Parameters: string &message - Message to be processed
                  WebSocketClient &k - Reference to the current object
        * - Return: bool - true for successful processing, false for processing failure
        * @note The passed function should have the signature bool func(const std::string &message, WebSocketClient &k)
        * @note If processing fails, the entire websocket connection will be closed directly
        */
        void setFunction(std::function<bool(const std::string &message, WebSocketClient &k)> fc) { this->fc = fc; }
        /**
        * @brief Connect to a websocket server
        * @param url Complete ws/wss url (note that the port and path need to be specified explicitly) 
        *        e.g., wss://google.com should be written as wss://google.com:443/ (complete with :443 and /)
        * @param min Heartbeat time in minutes (default is 20 minutes)
        * @warning Requires a complete ws/wss url (note that the port and path need to be specified explicitly) 
        *          e.g., wss://google.com should be written as wss://google.com:443/
        */
        bool connect(const std::string &url, const int &min = 20);
        /**
        * @brief Send a WebSocket message
        * 
        * According to the WebSocket protocol, encapsulate and send a masked data frame (clients must use masking),
        * supporting automatic frame format selection based on payload length:
        * - payload <= 125 bytes: Use 1-byte length
        * - 126 <= payload <= 65535: Use 2-byte extended length (with 126 marker)
        * - payload > 65535: Use 8-byte extended length (with 127 marker)
        * 
        * @param message Content of the message to be sent (already encoded as text or binary)
        * @param type Custom field specifying the message type (usually the WebSocket frame's opcode)
        *        Conventional format is "1000" + type, where:
        *        - "0001" represents text frame (Text Frame)
        *        - "0010" represents binary frame (Binary Frame)
        *        - "1000" represents connection close (Close Frame)
        *        - "1001" represents Ping frame
        *        - "1010" represents Pong frame
        *        Please use according to internal conventions, text frame (Text Frame) is used by default
        * 
        * @return true if sending is successful
        * @return false if sending fails (may be due to unestablished connection or sending exception)
        */
        bool sendMessage(const std::string &message, const std::string &type = "0001");
        /**
        * @brief Send a close frame and close the WebSocket connection (simplified method)
        * 
        * Directly pass the encoded close payload, where the first two bytes are the close code (big-endian),
        * followed by the UTF-8 encoded close reason description, for simplified calling.
        * 
        * @param closeCodeAndMessage Encoded close frame payload (2-byte close code + optional message)
        * @param wait Whether to wait for the underlying listening thread/epoll event processing to exit
        */
        void close(const std::string &closeCodeAndMessage, const bool &wait = true);
        /**
        * @brief Send a close frame and close the WebSocket connection (standard method)
        * 
        * Build a close frame (opcode = 0x8) that conforms to RFC 6455, with the frame payload containing 
        * the close code (2 bytes) and an optional close reason string.
        * 
        * @param code WebSocket close code, common ones include:
        * - 1000: Normal Closure
        * - 1001: Going Away
        * - 1002: Protocol Error
        * - 1003: Unsupported Data
        * - 1006: Abnormal Closure (no close frame, used internally by the program)
        * - 1008: Policy Violation
        * - 1011: Internal Error
        * 
        * @param message Optional close reason for debugging or logging
        * @param wait Whether to wait for the underlying listening thread/epoll event processing to exit
        */
        void close(const short &code = 1000, const std::string &message = "bye", const bool &wait = true);       
    public:
        /**
        * @brief Return the connection status
        * @return true: Connected to the server, false: Not connected
        */
        bool isConnect() { return flag4; }
        /**
        * @brief Return the url if connected to the server
        * @return The url
        */
        std::string getUrl() { return url; }
        /**
        * @brief Return the server ip if connected to the server
        * @return The server ip
        */
        std::string getServerIp() { return TcpClient::getServerIP(); }
        /**
        * @brief Return the server port if connected to the server
        * @return The server port
        */
        std::string getServerPort() { return TcpClient::getServerIP(); }
        /**
        * @brief Destructor of WebSocketClient class, gracefully disconnects when destroying the object
        */    
        ~WebSocketClient();
    };

    /**
    * @brief Structure to save HTTP/HTTPS request information
    */
    struct HttpRequestInformation 
    {
        /**
        * @brief Low-level socket
        */
        int fd;
        /**
        * @brief connection objection fd
        */
        uint64_t connection_obj_fd;
        /**
        * @brief Request type
        */
        std::string type;
        /**
        * @brief Path and parameters in URL
        */
        std::string locPara;
        /**
        * @brief path in url
        */
        std::string loc;
        /**
        * @brief parameters in url
        */
        std::string para;
        /**
        * @brief Request header
        */
        std::string header;
        /**
        * @brief Request body
        */
        std::string body;
        /**
        * @brief Request body (chunked)
        */
        std::string body_chunked;
        /**
        * @brief required data warehouse
        */
        std::unordered_map<std::string,std::any> ctx;
    
    };

    struct TcpFDInf;
    /**
    * @brief Operation class for parsing and responding to Http/https requests
    * Only pass in the socket, then use this class for Http operations
    */
    class HttpServerFDHandler : public TcpFDHandler
    {
    public:
        /**
        * @brief Initialize the object, pass in socket and other parameters
        * @param fd Socket
        * @param ssl TLS encrypted SSL handle (default is nullptr)
        * @param flag1 true: Enable non-blocking mode, false: Enable blocking mode (default is false, i.e., blocking mode)
        * @param flag2 true: Enable SO_REUSEADDR mode, false: Do not enable SO_REUSEADDR mode (default is true, i.e., enable mode)
        */
        void setFD(const int &fd, SSL *ssl = nullptr, const bool &flag1 = false, const bool &flag2 = true) { TcpFDHandler::setFD(fd, ssl, flag1, flag2); }
        /**
        * @brief Parse Http/Https request
        * @param TcpInf stores the state information of the underlying TCP processing socket
        * @param HttpInf stores the information of Http
        * @param buffer_size The size of the server-defined parsing buffer (in bytes)
        * @param times sometims will be used to record solve times
        * @return 1: Parsing completed 0: Parsing still needs to be continued -1: Parsing failed
        * @note TcpInf.status
        *
        * 0 Initial status
        * 1 Receive request header
        * 2 Receive request body (chunk mode)
        * 3 Receive request body (non-chunk mode)
        * 
        */
        int solveRequest(TcpFDInf &TcpInf,HttpRequestInformation &HttpInf,const unsigned long &buffer_size,const int &times=1);
        /**
        * @brief Send Http/Https response
        * @param data String container with response body data
        * @param code Http response status code and status description (default is "200 OK")
        * @param header Http request header; if not generated by createHeader, remember to add \r\n at the end.
        * @param header1 Additional item for the HTTP request header; if needed, must fill in a valid item; 
        *        no need to add \r\n at the end (cannot use createHeader). (For example, you can fill in the keepalive field by default)
        * @return true: Response sent successfully, false: Response sending failed
        */
        bool sendBack(const std::string &data, const std::string &header = "", const std::string &code = "200 OK", const std::string &header1 = "");
        /**
        * @brief Send Http/Https response
        * @param data Char * container with response body data
        * @param length Data length in the char* container
        * @param code Http response status code and status description (default is "200 OK")
        * @param header Http request header; if not generated by createHeader, remember to add \r\n at the end.
        * @param header1 Additional item for the HTTP request header; if needed, must fill in a valid item; 
        *        no need to add \r\n at the end (cannot use createHeader). (For example, you can fill in the keepalive field by default)
        * @param header_length Maximum length of the response header added up (default is 50)
        * @warning The reserved space must be accurate, otherwise the delivery may fail
        * @warning All data pointed to by char must be ensured that 0 ends with 0 is not counted in the length, otherwise there is a risk of crashing
        * @return true: The response was successfully sent false: The response failed to be sent
        */
        bool sendBack(const char *data,const size_t &length,const char *header="\0",const char *code="200 OK\0",const char *header1="\0",const size_t &header_length=50);
    };

    /**
    * @brief Structure to save client WS/WSS request information
    */
    struct WebSocketFDInformation
    {
        /**
        * @brief Underlying socket descriptor
        */
        int fd;
        /**
        * @brief connection objection fd
        */
        uint64_t connection_obj_fd;
        /**
        * @brief true: Close frame sent, false: Close frame not sent
        */
        bool closeflag;
        /**
        * @brief Http/Https path and parameters during the handshake phase
        */
        std::string locPara;
        /**
        * @brief Http/Https request header during the handshake phase
        */
        std::string header;
        /**
        * @brief Time of sending heartbeat (fill in 0 if not sent) (should be cleared to 0 after checking)
        */
        time_t HBTime = 0;
        /**
        * @brief Time of last received message
        */
        time_t response;
        /**
        * @brief Length to be received
        */
        size_t recv_length;
        /**
        * @brief Length have received
        */
        size_t have_recv_length;
        /**
        * @brief Message type
        */
        int message_type;
        /**
        * @brief Message
        */
        std::string message="";
        /**
        * @brief state of fin
        */
        bool fin;
        /**
        * @brief mask
        */
        std::string mask;
        /**
        * @brief required data warehouse
        */
        std::unordered_map<std::string,std::any> ctx;
        /**
        * @brief http information in handshake stage
        */
        HttpRequestInformation httpinf;
    };

    enum class TLSState : uint8_t {
    NONE = 0,        // 非 TLS 连接（普通 TCP）
    HANDSHAKING,    // TLS 握手中（SSL_accept 还没完成）
    ESTABLISHED,    // TLS 已建立，可以 SSL_read / SSL_write
    ERROR           // TLS 出错（可选）
    };

    /**
    * @brief Save TCP client information
    */
    struct TcpInformation
    {
        /**
        * @brief socket fd
        */
        int fd;
        /**
        * @brief connection objection fd
        */
        uint64_t connection_obj_fd;
        /**
        * @brief Naked data
        */
        std::string data;
        /**
        * @brief required data warehouse
        */
        std::unordered_map<std::string,std::any> ctx;
    };

   /**
    * @brief Structure to save TCP client information
    */
    struct TcpFDInf
    {
        /**
        * @brief Socket file descriptor
        */
        int fd;
        /**
        * @brief connection objection fd
        */
        uint64_t connection_obj_fd;
        /**
        * @brief Client IP
        */
        std::string ip;
        /**
        * @brief Client port
        */
        std::string port;
        /**
        * @brief The current fd status, used to save the processor logic
        */
        int status;
        /**
        * @brief Save the data received from the client
        */
        std::string_view data;
        /**
        * @brief If encrypted, store the encryption handle
        */
        SSL* ssl;
        /**
        * @brief tls state
        */
        TLSState tls_state;
        /**
        * @brief Receives the space pointer
        */
        char *buffer;
        /**
        * @brief Receives a spatial position pointer
        */
        unsigned long p_buffer_now;
        /**
         * @brief Record which step the current state machine is at.
         */
        int FDStatus;
        /**
        * @brief Queue waiting to be processed
        */
        std::queue<std::any> pendindQueue;
    };

    /**
    * @brief Data structure for pushing tasks into a completion queue after they are completed at the work site
    */
    struct WorkerMessage
    {
        /**
        * @brief Low-level sockets
        */
        int fd;
        /**
        * @brief Return value -2: Failure and connection closed required; -1: Failure but connection closed not required; 1: Success.
        */
        int ret;
    };
    
    /**
    * @brief Tcp server class
    * @note The default underlying implementation is epoll edge trigger + socket non-blocking mode
    */
    class TcpServer 
    {
    protected:
        system::MPSCQueue<WorkerMessage> finishQueue;
        stt::system::WorkerPool *workpool; 
        unsigned long buffer_size;
        unsigned long long  maxFD;
        security::ConnectionLimiter connectionLimiter;
        //std::unordered_map<int,TcpFDInf> clientfd;
        //std::mutex lc1;
        TcpFDInf *clientfd;
        int flag1=true;
        //std::queue<QueueFD> *fdQueue;
        //std::mutex *lq1;
        //std::condition_variable cv1;
        //std::condition_variable *cv;
        //int consumerNum;
        //std::mutex lco1;
        bool unblock;
        SSL_CTX *ctx=nullptr;
        bool TLS=false;
        //std::unordered_map<int,SSL*> tlsfd;
        //std::mutex ltl1;
        bool security_open;
        //bool flag_detect;
        //bool flag_detect_status;
        int workerEventFD;
        int serverType; // 1 tcp 2 http 3 websocket
        int connectionSecs;
        int connectionTimes;
        int requestSecs;
        int requestTimes;
        int checkFrequency;
        uint64_t connection_obj_fd;
    private:
        std::function<void(const int &fd)> closeFun=[](const int &fd)->void
        {

        };
        std::function<void(TcpFDHandler &k,TcpInformation &inf)> securitySendBackFun=[](TcpFDHandler &k,TcpInformation &inf)->void
        {};
        std::function<bool(TcpFDHandler &k,TcpInformation &inf)> globalSolveFun=[](TcpFDHandler &k,TcpInformation &inf)->bool
        {return true;};
        std::unordered_map<std::string,std::vector<std::function<int(TcpFDHandler &k,TcpInformation &inf)>>> solveFun;
        std::function<int(TcpFDHandler &k,TcpInformation &inf)> parseKey=[](TcpFDHandler &k,TcpInformation &inf)->int
        {inf.ctx["key"]=inf.data;return 1;};
        int fd=-1;
        int port=-1;
        int flag=false;
        bool flag2=false;
    private:
        void epolll(const int &evsNum);
        //virtual void consumer(const int &threadID);
        virtual void handler_netevent(const int &fd);
        virtual void handler_workerevent(const int &fd,const int &ret);
        virtual void handleHeartbeat()=0;
    public:
        /**
        * @brief Add a task to a worker thread pool and have it completed by worker threads.
        * @note Slow, blocking I/O tasks should be placed in a worker thread pool.
        * @warning Executable objects must return values ​​strictly according to the specified return values.
        * @param fun Executable objects placed in the worker thread pool
        * -parameter：TcpFDHandler &k - References to the operation objects of the socket connected to the client
        *       TcpFDInf &inf - Client information, saved data, processing progress, state machine information, etc.
        * -return value：-2:Processing failed and connection needs to be closed -1: Processing failed but connection does not need to be closed 1: Processing succeeded
        * @param k References to the operation objects of the socket connected to the client
        * @param inf References to client information, data storage, processing progress, state machine information, etc.
        */
        void putTask(const std::function<int(TcpFDHandler &k,TcpInformation &inf)> &fun,TcpFDHandler &k,TcpInformation &inf);
        /**
 * @brief Constructor.
 *
 * By default, allows up to 1,000,000 concurrent connections, allocates
 * a maximum receive buffer of 256 KB per connection, and enables
 * the security module.
 *
 * @note
 * Enabling the security module will introduce additional overhead
 * and may impact performance.
 *
 * @param maxFD
 *        Maximum number of connections this server instance can accept.
 *        Default: 1,000,000.
 *
 * @param buffer_size
 *        Maximum amount of data a single connection is allowed to receive,
 *        in kilobytes (KB).
 *        Default: 256 KB.
 *
* @param finishQueue_cap The capacity of the worker completion queue (Worker → Reactor), which must be a power of 2.

//
/ This queue holds the results of tasks completed by worker threads, waiting for reactor threads to consume them.

// This is part of the main data path and is extremely sensitive to system throughput and latency.

//
/ Selection principle:

// finishQueue_cap >= Peak completion rate (QPS) × worst-case reactor pause time

//
/ Suggested values ​​(empirical):

// - Low load/light business: 8192 (~8k)

// - Regular high concurrency: 65536 (~64k) [Default]

// - Extreme burst traffic: 131072 (~128k)

//
/ When the queue is full, requests will be dropped; the framework will not block the producer.
 * @param security_open
 *        Whether to enable the security module.
 *        - true  : enable security checks (default)
 *        - false : disable security checks
 *
 * @param connectionNumLimit
 *        Maximum number of concurrent connections allowed per IP.
 *        Default: 20.
 *
 * @param connectionSecs
 *        Time window (in seconds) for connection rate limiting.
 *        Default: 1 second.
 *
 * @param connectionTimes
 *        Maximum number of connection attempts allowed within
 *        `connectionSecs` seconds.
 *        Default: 6.
 *
 * @param requestSecs
 *        Time window (in seconds) for request rate limiting.
 *        Default: 1 second.
 *
 * @param requestTimes
 *        Maximum number of requests allowed within `requestSecs` seconds.
 *        Default: 40.
 *
 * @param checkFrequency
 *        Frequency (in seconds) for checking zombie/idle connections.
 *        -1 disables zombie connection detection.
 *        Default: 60 seconds.
 *
 * @param connectionTimeout
 *        If a connection has no activity for this many seconds,
 *        it is considered a zombie connection.
 *        -1 means no timeout (infinite).
 *        Default: 60 seconds.
 */
TcpServer(
    const unsigned long long &maxFD = 1000000,
    const int &buffer_size = 256,
    const size_t &finishQueue_cap=65536,
    const bool &security_open = true,
    const int &connectionNumLimit = 20,
    const int &connectionSecs = 1,
    const int &connectionTimes = 6,
    const int &requestSecs = 1,
    const int &requestTimes = 40,
    const int &checkFrequency = 60,
    const int &connectionTimeout = 60
)
: maxFD(maxFD),
  buffer_size(buffer_size * 1024),
  finishQueue(finishQueue_cap),
  security_open(security_open),
  connectionSecs(connectionSecs),
  connectionTimes(connectionTimes),
  requestSecs(requestSecs),
  requestTimes(requestTimes),
  connectionLimiter(connectionNumLimit, connectionTimeout),
  checkFrequency(checkFrequency)
{
    serverType = 1;
}

        /**
        * @brief Start the TCP server listening program
        * @param port Port to listen on
        * @param threads Number of consumer threads (default is 8)
        * @return true: Listening started successfully, false: Failed to start listening
        */
        bool startListen(const int &port, const int &threads = 8);
        /**
        * @brief Enable TLS encryption and configure server-side certificate and key
        * 
        * This function initializes OpenSSL and enables TLS (SSL/TLSv1 protocol family) support for the TCP server.
        * It loads the server-side certificate, private key, and an optional CA root certificate for peer verification.
        * 
        * If TLS is already enabled, the context will be automatically rebuilt (reloaded).
        * 
        * @param cert Server certificate chain file path (usually PEM format, including intermediate certificates)
        * @param key Private key file path (matching PEM format key for the certificate)
        * @param passwd Password for the private key file (can be an empty string if the key is not encrypted)
        * @param ca CA root certificate path for verifying client certificates (PEM format)
        * 
        * @note The protocol method used is `SSLv23_method()`, which actually supports SSLv3/TLSv1/TLSv1.1/TLSv1.2 and higher versions 
        *       (depending on the OpenSSL version and configuration)
        * 
        * @note The certificate verification policy uses `SSL_VERIFY_FAIL_IF_NO_PEER_CERT`, meaning:
        * - If the client does not provide a certificate, the handshake fails (safer, recommended)
        * - If the certificate is invalid or verification fails, the handshake is also terminated
        * 
        * @return true if TLS is enabled successfully, server is in encrypted state
        * @return false if enabling fails (specific error will be logged)
        * 
        * @warning After enabling TLS, all incoming connections must follow the TLS handshake process, otherwise communication will fail
        * 
        * @see redrawTLS() If a TLS context already exists, it will be released and rebuilt first (can be used for hot updating certificates)
        */
        bool setTLS(const char *cert, const char *key, const char *passwd, const char *ca);
        /**
        * @brief Revoke TLS encryption, CA certificate, etc.
        */
        void redrawTLS();
        /**
 * @brief Set the callback invoked when an information security policy is violated.
 *
 * @details
 * This callback is executed when a TCP client violates a security rule
 * (e.g. connection abuse, rate limiting, malformed data, or other
 * security-related conditions).
 * After the callback is executed, the connection will be closed.
 *
 * @note
 * The callback is invoked once per security violation.
 * The connection is closed unconditionally after the callback returns.
 *
 * @param fc
 *        A function or function object used to handle the response
 *        sent back to the client upon a security violation.
 *
 *        Callback parameters:
 *        - TcpFDHandler &k  
 *          Reference to the handler object used to operate on the
 *          client socket/connection.
 *
 *        - TcpInformation &inf  
 *          Client-related information, including buffered data,
 *          processing progress, and protocol/state-machine context.
 *
 * @note
 * The callback does not return a value.
 * The connection will be closed regardless of the callback outcome.
 */
void setSecuritySendBackFun(
    std::function<void(TcpFDHandler &k,
                       TcpInformation &inf)> fc
)
{
    this->securitySendBackFun = fc;
}


        /**
        * @brief Set global fallback function
        * @note When a corresponding callback function cannot be found, a global backup function will be called.
        * @param key Find the key of the corresponding callback function
        * @param fc A function or function object used for processing logic after receiving a message from the client.
        * -Parameters: TcpFDHandler &k - A reference to the operation object of the socket connected to the client.
        *       TcpInformation &inf - Client information, saved data, processing progress, state machine information, etc.
        * -Return value: true: Processing successful; false: Processing failed and the connection will be closed.
        */
        void setGlobalSolveFunction(std::function<bool(TcpFDHandler &k,TcpInformation &inf)> fc){this->globalSolveFun=fc;}
        /**
 * @brief Register a callback function for a specific key.
 * @note Multiple callbacks can be registered for the same key. The framework
 *       will execute them sequentially in the order they are registered.
 *       You may also choose to dispatch the processing to a worker thread pool
 *       by returning specific values.
 * @warning The callable object must strictly follow the required return value
 *          conventions.
 * @param key The key used to locate the corresponding callback function.
 * @param fc A function or function object that defines the logic to handle
 *        incoming client messages.
 *        - Parameters:
 *          TcpFDHandler &k  - Reference to the socket operation object associated
 *                            with the client connection.
 *          TcpInformation &inf - Client information, including received data,
 *                            processing progress, state machine data, etc.
 *        - Return value:
 *          -2 : Processing failed and the connection must be closed.
 *          -1 : Processing failed but the connection should remain open.
 *           0 : Processing has been dispatched to a worker thread pool and
 *               the framework should wait for completion.
 *           1 : Processing succeeded.
 */
void setFunction(
    const std::string &key,
    std::function<int(TcpFDHandler &k, TcpInformation &inf)> fc)
{
    auto [it, inserted] = solveFun.try_emplace(key);
    it->second.push_back(std::move(fc));
}

/**
 * @brief Set the callback function used to parse the key.
 * @note This function extracts the key from the input parameters and stores it
 *       in the ctx hash table within TcpInformation. The framework uses this key
 *       to locate the registered processing callbacks.
 * @warning The callable object must strictly follow the required return value
 *          conventions.
 * @param parseKeyFun The callback function used to parse the key.
 *        - Parameters:
 *          TcpFDHandler &k  - Reference to the socket operation object associated
 *                            with the client connection.
 *          TcpInformation &inf - Client information, including received data,
 *                            processing progress, state machine data, etc.
 *        - Return value:
 *          -2 : Processing failed and the connection must be closed.
 *          -1 : Processing failed but the connection should remain open.
 *           0 : Processing has been dispatched to a worker thread pool and
 *               the framework should wait for completion.
 *           1 : Processing succeeded.
 */
void setGetKeyFunction(
    std::function<int(TcpFDHandler &k, TcpInformation &inf)> parseKeyFun)
{
    this->parseKey = parseKeyFun;
}
        /**
        * @brief Stop listening
        * @warning Only stops listening (but the socket can no longer receive, it depends on listening and consumers, so this function is of little significance)
        * @return true: Stopped successfully, false: Failed to stop
        */
        bool stopListen();
        /**
        * @brief Close listening and all connected sockets
        * @note Closes listening and all connected sockets, registered callback functions and TLS will not be deleted or redrawn
        * @note Will block until all closures are complete
        * @return true: Closed successfully, false: Failed to close
        */
        bool close();
        /**
        * @brief Close the connection of a specific socket
        * @param fd Socket to be closed
        * @return true: Closed successfully, false: Failed to close
        */
        virtual bool close(const int &fd);
        /**
        * @brief set function after tcp connection close
        */
        void setCloseFun(std::function<void(const int &fd)> closeFun){this->closeFun=closeFun;}
    public:
        /**
        * @brief Return the listening status of the object
        * @return true: Listening, false: Not listening
        */
        bool isListen() { return flag; }
        /**
        * @brief Query the connection with the server, pass in the socket, and return the encrypted SSL handle
        * @return Encrypted SSL pointer; returns nullptr if this fd does not exist or there is no encryption
        */
        SSL* getSSL(const int &fd);
        /**
        * @brief Destructor of TcpServer class
        * @note Calls the close function to close
        */
        ~TcpServer() { close(); }
    };


    
    /**
    * @brief Http/HttpServer server operation class
    * @note support http/1.0 1.1
    */
    class HttpServer : public TcpServer
{
private:
    std::function<void(HttpServerFDHandler &k,HttpRequestInformation &inf)> securitySendBackFun=[](HttpServerFDHandler &k,HttpRequestInformation &inf)->void
        {};
    std::vector<std::function<int(HttpServerFDHandler &k,HttpRequestInformation &inf)>> globalSolveFun;
    // std::function<bool(HttpServerFDHandler &k, HttpRequestInformation &inf)> globalSolveFun = {};
    std::unordered_map<
        std::string,
        std::vector<std::function<int(HttpServerFDHandler &k, HttpRequestInformation &inf)>>
    > solveFun;

    std::function<int(HttpServerFDHandler &k, HttpRequestInformation &inf)> parseKey =
        [](HttpServerFDHandler &k, HttpRequestInformation &inf) -> int {
            inf.ctx["key"] = inf.loc;
            return 1;
        };
        HttpRequestInformation *httpinf;

private:
    void handler_netevent(const int &fd);
    void handler_workerevent(const int &fd, const int &ret);
    void handleHeartbeat(){}

public:
    /**
     * @brief Submit a task to the worker thread pool.
     * @note Slow or potentially blocking I/O operations should be dispatched
     *       to the worker thread pool.
     * @warning The callable object must strictly follow the required return
     *          value conventions.
     * @param fun The callable object to be executed by the worker thread pool.
     *        - Parameters:
     *          HttpServerFDHandler &k  - Reference to the socket operation object
     *                                  associated with the client connection.
     *          HttpRequestInformation &inf - Client request information,
     *                                  including data, processing progress,
     *                                  and state machine information.
     *        - Return value:
     *          -2 : Processing failed and the connection must be closed.
     *          -1 : Processing failed but the connection should remain open.
     *           1 : Processing succeeded.
     * @param k   Reference to the socket operation object associated with
     *            the client connection.
     * @param inf Reference to the client request information.
     */
    void putTask(
        const std::function<int(HttpServerFDHandler &k, HttpRequestInformation &inf)> &fun,
        HttpServerFDHandler &k,
        HttpRequestInformation &inf
    );

    /**
 * @brief Constructor.
 *
 * By default, allows up to 1,000,000 concurrent connections, allocates
 * a maximum receive buffer of 256 KB per connection, and enables
 * the security module.
 *
 * @note
 * Enabling the security module introduces additional overhead
 * and may impact performance.
 *
 * @param maxFD
 *        Maximum number of connections this server instance can accept.
 *        Default: 1,000,000.
 *
 * @param buffer_size
 *        Maximum amount of data a single connection is allowed to receive,
 *        in kilobytes (KB).
 *        Default: 256 KB.
 *
 * @param finishQueue_cap The capacity of the worker completion queue (Worker → Reactor), which must be a power of 2.

//
/ This queue holds the results of tasks completed by worker threads, waiting for reactor threads to consume them.

// This is part of the main data path and is extremely sensitive to system throughput and latency.

//
/ Selection principle:

// finishQueue_cap >= Peak completion rate (QPS) × worst-case reactor pause time

//
/ Suggested values ​​(empirical):

// - Low load/light business: 8192 (~8k)

// - Regular high concurrency: 65536 (~64k) [Default]

// - Extreme burst traffic: 131072 (~128k)

//
/ When the queue is full, requests will be dropped; the framework will not block the producer.
 * @param security_open
 *        Whether to enable the security module.
 *        - true  : enable security checks (default)
 *        - false : disable security checks
 *
 * @param connectionNumLimit
 *        Maximum number of concurrent connections allowed per IP.
 *        Default: 10.
 *
 * @param connectionSecs
 *        Time window (in seconds) for connection rate limiting.
 *        Default: 1 second.
 *
 * @param connectionTimes
 *        Maximum number of connection attempts allowed within
 *        `connectionSecs` seconds.
 *        Default: 3.
 *
 * @param requestSecs
 *        Time window (in seconds) for request rate limiting.
 *        Default: 1 second.
 *
 * @param requestTimes
 *        Maximum number of requests allowed within `requestSecs` seconds.
 *        Default: 20.
 *
 * @param checkFrequency
 *        Frequency (in seconds) for checking zombie/idle connections.
 *        -1 disables zombie connection detection.
 *        Default: 30 seconds.
 *
 * @param connectionTimeout
 *        If a connection has no activity for this many seconds,
 *        it is considered a zombie connection.
 *        -1 means no timeout (infinite).
 *        Default: 30 seconds.
 */
HttpServer(
    const unsigned long long &maxFD = 1000000,
    const int &buffer_size = 256,
    const size_t &finishQueue_cap=65536,
    const bool &security_open = true,
    const int &connectionNumLimit = 10,
    const int &connectionSecs = 1,
    const int &connectionTimes = 3,
    const int &requestSecs = 1,
    const int &requestTimes = 20,
    const int &checkFrequency = 30,
    const int &connectionTimeout = 30
)
: TcpServer(
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
  )
{
    serverType = 2;
}
    /**
 * @brief Set the callback invoked when an information security policy is violated.
 *
 * @details
 * This callback is executed when a client violates a security rule
 * (e.g. rate limiting, abnormal request behavior, or other security checks).
 * After the callback is executed, the connection will be closed.
 *
 * @note
 * The callback is invoked once per security violation.
 * The connection is closed unconditionally after the callback returns.
 *
 * @param fc
 *        A function or function object used to handle the response
 *        sent back to the client upon a security violation.
 *
 *        Callback parameters:
 *        - HttpServerFDHandler &k  
 *          Reference to the handler object that operates on the
 *          client connection/socket.
 *
 *        - HttpRequestInformation &inf  
 *          Client request information, including buffered data,
 *          processing progress, and HTTP state/context.
 *
 * @note
 * The callback does not return a value.
 * The connection will be closed regardless of the outcome of the callback.
 */
void setSecuritySendBackFun(
    std::function<void(HttpServerFDHandler &k,
                       HttpRequestInformation &inf)> fc
)
{
    this->securitySendBackFun = fc;
}

        /**

* @brief Sets a global backup function

* @note The global backup function will be called when the corresponding callback function is not found. Multiple backup functions can be set, and the framework will execute them in the order they are set. You can also set the process of throwing the data into the worker thread pool; just be sure to set different return values.

* @param fc A function or function object used for processing logic after receiving a message from the client.

* - Parameters: HttpServerFDHandler &k - A reference to the operation object of the socket connected to the client.

* HttpRequestInformation &inf - Client information, saved data, processing progress, state machine information, etc.

* * - Return value: -2: Processing failed and the connection needs to be closed -1: Processing failed but the connection does not need to be closed 0: The processing flow has been thrown into the worker thread pool and needs to wait for processing to complete 1: Processing succeeded

*/
        void setGlobalSolveFunction(std::function<int(HttpServerFDHandler &k,HttpRequestInformation &inf)> fc){globalSolveFun.push_back(std::move(fc));}
    /**
     * @brief Register a callback function for a specific key.
     * @note Multiple callbacks can be registered for the same key. The framework
     *       executes them sequentially in the order they are registered.
     *       Processing can also be dispatched to the worker thread pool by
     *       returning appropriate values.
     * @warning The callable object must strictly follow the required return
     *          value conventions.
     * @param key The key used to locate the corresponding callback functions.
     * @param fc  A function or function object that defines the logic for
     *            handling client requests.
     *        - Parameters:
     *          HttpServerFDHandler &k  - Reference to the socket operation object
     *                                  associated with the client connection.
     *          HttpRequestInformation &inf - Client request information,
     *                                  including data, processing progress,
     *                                  and state machine information.
     *        - Return value:
     *          -2 : Processing failed and the connection must be closed.
     *          -1 : Processing failed but the connection should remain open.
     *           0 : Processing has been dispatched to the worker thread pool
     *               and the framework should wait for completion.
     *           1 : Processing succeeded.
     *
     * @code
     * httpserver->setFunction("/ping",
     *     [](HttpServerFDHandler &k, HttpRequestInformation &inf) -> int {
     *         k.sendBack("pong");
     *         return 1;
     *     });
     * @endcode
     *
     * @code
     * httpserver->setFunction("/ping",
     *     [](HttpServerFDHandler &k, HttpRequestInformation &inf) -> int {
     *         httpserver->putTask(
     *             [](HttpServerFDHandler &k, HttpRequestInformation &inf) -> int {
     *                 k.sendBack("pong");
     *                 return 1;
     *             },
     *             k,
     *             inf
     *         );
     *         return 0;
     *     });
     * @endcode
     */
    void setFunction(
        const std::string &key,
        std::function<int(HttpServerFDHandler &k, HttpRequestInformation &inf)> fc
    )
    {
        auto [it, inserted] = solveFun.try_emplace(key);
        it->second.push_back(std::move(fc));
    }

    /**
     * @brief Set the callback function used to parse the key.
     * @note This function extracts the key from the input parameters and stores
     *       it in the ctx hash table of HttpRequestInformation. The framework
     *       uses this key to locate the registered processing callbacks.
     * @warning The callable object must strictly follow the required return
     *          value conventions.
     * @param parseKeyFun The callback function used to parse the key.
     *        - Parameters:
     *          HttpServerFDHandler &k  - Reference to the socket operation object
     *                                  associated with the client connection.
     *          HttpRequestInformation &inf - Client request information,
     *                                  including data, processing progress,
     *                                  and state machine information.
     *        - Return value:
     *          -2 : Processing failed and the connection must be closed.
     *          -1 : Processing failed but the connection should remain open.
     *           0 : Processing has been dispatched to the worker thread pool
     *               and the framework should wait for completion.
     *           1 : Processing succeeded.
     *
     * @code
     * httpserver->setGetKeyFunction(
     *     [](HttpServerFDHandler &k, HttpRequestInformation &inf) -> int {
     *         inf.ctx["key"] = inf.loc;
     *         return 1;
     *     });
     * @endcode
     */
    void setGetKeyFunction(
        std::function<int(HttpServerFDHandler &k, HttpRequestInformation &inf)> parseKeyFun
    )
    {
        this->parseKey = parseKeyFun;
    }

    /**
     * @brief Start the HTTP server listening loop.
     * @param port The port to listen on.
     * @param threads Number of worker/consumer threads (default: 8).
     * @return true if the server starts listening successfully,
     *         false otherwise.
     */
    bool startListen(const int &port, const int &threads = 8)
    {
        httpinf = new HttpRequestInformation[maxFD];
        return TcpServer::startListen(port, threads);
    }

    /**
     * @brief Destructor.
     */
    ~HttpServer() {delete[] httpinf;}
};

    /**
    * @brief WebSocket protocol operation class
    * Only pass in the socket, then use this class for WebSocket operations
    */
    class WebSocketServerFDHandler : private TcpFDHandler
    {
    public:
        /**
        * @brief Initialize the object, pass in socket and other parameters
        * @param fd Socket
        * @param ssl TLS encrypted SSL handle (default is nullptr)
        * @param flag1 true: Enable non-blocking mode, false: Enable blocking mode (default is false, i.e., blocking mode)
        * @param flag2 true: Enable SO_REUSEADDR mode, false: Do not enable SO_REUSEADDR mode (default is true, i.e., enable mode)
        */
        void setFD(const int &fd, SSL *ssl = nullptr, const bool &flag1 = false, const bool &flag2 = true) { TcpFDHandler::setFD(fd, ssl, flag1, flag2); }
        /**
        * @brief Get a websocket message
        * @param Tcpinf saves the underlying TCP status information
        * @param Websocketinf saves the websocket protocol status information
        * @param buffer_size The size of the server-defined parsing buffer (in bytes).
        * @param ii Record the number of resolutions, which can be used in some cases The default is 1
        * @return
        * -1: Failed to get
        * 0: General message
        * 1: Close frame
        * 2: Heartbeat confirmation message
        * 3: Heartbeat message
        * 4：waiting data
        * @note TcpInf.status
        *
        * 0 Initial state
        * 1 Confirmation message type
        * 2 Acknowledgment message length
        * 3 Receiveing mask
        * 4 Receiving messages
        *
        */
        int getMessage(TcpFDInf &Tcpinf,WebSocketFDInformation &Websocketinf,const unsigned long &buffer_size,const int &ii=1);
        /**
        * @brief Send a websocket message
        * @param msg Websocket message to be sent
        * @param type Custom field specifying the message type (usually the WebSocket frame's opcode)
        *        Conventional format is "1000" + type, where:
        *        - "0001" represents text frame (Text Frame)
        *        - "0010" represents binary frame (Binary Frame)
        *        - "1000" represents connection close (Close Frame)
        *        - "1001" represents Ping frame
        *        - "1010" represents Pong frame
        *        Please use according to internal conventions, text frame (Text Frame) is used by default
        * 
        * @return true: Sent successfully, false: Sending failed
        */
        bool sendMessage(const std::string &msg, const std::string &type = "0001");
       
    };
    
    /**
    * @brief WebSocketServer server operation class
    */
    class WebSocketServer : public TcpServer
{
private:
    std::unordered_map<int, WebSocketFDInformation> wbclientfd;
    std::function<void(WebSocketServerFDHandler &k,WebSocketFDInformation &inf)> securitySendBackFun=[](WebSocketServerFDHandler &k,WebSocketFDInformation &inf)->void
        {};

    std::function<bool(WebSocketFDInformation &k)> fcc =
        [](WebSocketFDInformation &k) {
            return true;
        };

    std::function<bool(WebSocketServerFDHandler &k, WebSocketFDInformation &inf)> fccc =
        [](WebSocketServerFDHandler &k, WebSocketFDInformation &inf) ->bool
        {return true;};

    std::function<bool(WebSocketServerFDHandler &k, WebSocketFDInformation &inf)> globalSolveFun =
        [](WebSocketServerFDHandler &k, WebSocketFDInformation &inf) -> bool {
            return true;
        };

    std::unordered_map<
        std::string,
        std::vector<std::function<int(WebSocketServerFDHandler &, WebSocketFDInformation &)>>>
        solveFun;

    std::function<int(WebSocketServerFDHandler &k, WebSocketFDInformation &inf)> parseKey =
        [](WebSocketServerFDHandler &k, WebSocketFDInformation &inf) -> int {
            inf.ctx["key"] = inf.message;
            return 1;
        };

    int seca = 20 * 60;  // heartbeat interval (seconds)
    int secb = 30;       // heartbeat response timeout (seconds)



private:
    void handler_netevent(const int &fd);
    void handler_workerevent(const int &fd, const int &ret);

    void closeAck(const int &fd, const std::string &closeCodeAndMessage);
    void closeAck(const int &fd, const short &code = 1000, const std::string &message = "bye");

    

    void handleHeartbeat();

    bool closeWithoutLock(const int &fd, const std::string &closeCodeAndMessage);
    bool closeWithoutLock(const int &fd, const short &code = 1000, const std::string &message = "bye");

public:
    /**
     * @brief Submit a task to the worker thread pool.
     * @note Slow or blocking I/O operations should be dispatched to the worker pool.
     * @warning The callable object must strictly follow the required return value conventions.
     * @param fun The callable task to be executed by the worker thread pool.
     *        - Parameters:
     *          WebSocketServerFDHandler &k  - Socket operation object bound to the client.
     *          WebSocketFDInformation &inf - Client information including data,
     *                                       processing progress, and state machine data.
     *        - Return value:
     *          -2 : Processing failed and the connection must be closed.
     *          -1 : Processing failed but the connection should remain open.
     *           1 : Processing succeeded.
     * @param k   Reference to the socket handler.
     * @param inf Reference to the client information.
     */
    void putTask(
        const std::function<int(WebSocketServerFDHandler &, WebSocketFDInformation &)> &fun,
        WebSocketServerFDHandler &k,
        WebSocketFDInformation &inf);

    /**
 * @brief Constructor.
 *
 * By default, allows up to 1,000,000 concurrent connections, allocates
 * a maximum receive buffer of 256 KB per connection, and enables
 * the security module.
 *
 * @note
 * Enabling the security module introduces additional overhead
 * and may impact performance.
 *
 * @param maxFD
 *        Maximum number of connections this server instance can accept.
 *        Default: 1,000,000.
 *
 * @param buffer_size
 *        Maximum amount of data a single connection is allowed to receive,
 *        in kilobytes (KB).
 *        Default: 256 KB.
 *
 * @param finishQueue_cap The capacity of the worker completion queue (Worker → Reactor), which must be a power of 2.

//
/ This queue holds the results of tasks completed by worker threads, waiting for reactor threads to consume them.

// This is part of the main data path and is extremely sensitive to system throughput and latency.

//
/ Selection principle:

// finishQueue_cap >= Peak completion rate (QPS) × worst-case reactor pause time

//
/ Suggested values ​​(empirical):

// - Low load/light business: 8192 (~8k)

// - Regular high concurrency: 65536 (~64k) [Default]

// - Extreme burst traffic: 131072 (~128k)

//
/ When the queue is full, requests will be dropped; the framework will not block the producer.
 * @param security_open
 *        Whether to enable the security module.
 *        - true  : enable security checks (default)
 *        - false : disable security checks
 *
 * @param connectionNumLimit
 *        Maximum number of concurrent connections allowed per IP.
 *        Default: 5.
 *
 * @param connectionSecs
 *        Time window (in seconds) for connection rate limiting.
 *        Default: 10 seconds.
 *
 * @param connectionTimes
 *        Maximum number of connection attempts allowed within
 *        `connectionSecs` seconds.
 *        Default: 3.
 *
 * @param requestSecs
 *        Time window (in seconds) for request/message rate limiting.
 *        Default: 1 second.
 *
 * @param requestTimes
 *        Maximum number of requests/messages allowed within
 *        `requestSecs` seconds.
 *        Default: 10.
 *
 * @param checkFrequency
 *        Frequency (in seconds) for checking zombie/idle connections.
 *        -1 disables zombie connection detection.
 *        Default: 60 seconds.
 *
 * @param connectionTimeout
 *        If a connection has no activity for this many seconds,
 *        it is considered a zombie connection.
 *        -1 means no timeout (infinite).
 *        Default: 120 seconds.
 */
WebSocketServer(
    const unsigned long long &maxFD = 1000000,
    const int &buffer_size = 256,
    const size_t &finishQueue_cap=65536,
    const bool &security_open = true,
    const int &connectionNumLimit = 5,
    const int &connectionSecs = 10,
    const int &connectionTimes = 3,
    const int &requestSecs = 1,
    const int &requestTimes = 10,
    const int &checkFrequency = 60,
    const int &connectionTimeout = 120
)
: TcpServer(
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
  )
{
    serverType = 3;
}
/**
 * @brief Set the callback invoked when an information security policy is violated.
 *
 * @details
 * This callback is executed when a WebSocket client violates a security rule
 * (e.g. rate limiting, abnormal message behavior, protocol abuse, etc.).
 * After the callback is executed, the connection will be closed.
 *
 * @note
 * The callback is invoked once per violation.
 * The connection is closed unconditionally after the callback returns.
 *
 * @param fc
 *        A function or function object used to handle the response
 *        sent back to the client upon a security violation.
 *
 *        Callback parameters:
 *        - WebSocketServerFDHandler &k  
 *          Reference to the handler object that operates on the
 *          WebSocket connection/socket.
 *
 *        - WebSocketFDInformation &inf  
 *          Client connection information, including buffered data,
 *          processing progress, and WebSocket state machine context.
 *
 * @note
 * The callback does not return a value.
 * The connection will be closed regardless of the callback outcome.
 */
void setSecuritySendBackFun(
    std::function<void(WebSocketServerFDHandler &k,
                       WebSocketFDInformation &inf)> fc
)
{
    this->securitySendBackFun = fc;
}


    /**
     * @brief Set the global fallback handler.
     * @note This handler is invoked when no matching key-based callback is found.
     * @param fc Fallback function to process incoming messages.
     *        - Parameters:
     *          WebSocketServerFDHandler &k
     *          WebSocketFDInformation &inf
     *        - Return value:
     *          true  : Processing succeeded.
     *          false : Processing failed and the connection will be closed.
     */
    void setGlobalSolveFunction(
        std::function<bool(WebSocketServerFDHandler &, WebSocketFDInformation &)> fc)
    {
        this->globalSolveFun = fc;
    }

    /**
     * @brief Set the callback invoked immediately after a WebSocket connection is established.
     * @param fccc Callback executed after successful WebSocket handshake.
     *        - Parameters:
     *          WebSocketServerFDHandler &k
     *          WebSocketFDInformation &inf
     */
    void setStartFunction(
        std::function<bool(WebSocketServerFDHandler &, WebSocketFDInformation &)> fccc)
    {
        this->fccc = fccc;
    }

    /**
     * @brief Set the handshake validation callback.
     * @note Only if this check passes will the WebSocket handshake continue.
     * @param fcc Validation function.
     *        - Parameters:
     *          WebSocketFDInformation &inf
     *        - Return value:
     *          true  : Validation passed.
     *          false : Validation failed and the connection will be closed.
     */
    void setJudgeFunction(std::function<bool(WebSocketFDInformation &)> fcc)
    {
        this->fcc = fcc;
    }

    /**
     * @brief Register a message handler callback for a specific key.
     * @note Multiple handlers can be registered for the same key and will be
     *       executed sequentially.
     * @warning Return values must strictly follow the defined conventions.
     * @param key Message key.
     * @param fc  Callback function.
     *        - Return value:
     *          -2 : Close connection.
     *          -1 : Processing failed but keep connection.
     *           0 : Dispatched to worker pool.
     *           1 : Processing succeeded.
     */
    void setFunction(
        const std::string &key,
        std::function<int(WebSocketServerFDHandler &, WebSocketFDInformation &)> fc)
    {
        auto [it, inserted] = solveFun.try_emplace(key);
        it->second.push_back(std::move(fc));
    }

    /**
     * @brief Set the key parsing callback.
     * @note The parsed key must be stored in inf.ctx["key"].
     * @param parseKeyFun Key parsing function.
     */
    void setGetKeyFunction(
        std::function<int(WebSocketServerFDHandler &, WebSocketFDInformation &)> parseKeyFun)
    {
        this->parseKey = parseKeyFun;
    }

    /**
     * @brief Set heartbeat interval.
     * @param seca Heartbeat interval in minutes (default: 20 minutes).
     */
    void setTimeOutTime(const int &seca)
    {
        this->seca = seca * 60;
    }

    /**
     * @brief Set heartbeat response timeout.
     * @param secb Time to wait after sending a heartbeat (seconds).
     * @note If no response is received within this time, the connection is closed.
     */
    void setHBTimeOutTime(const int &secb)
    {
        this->secb = secb;
    }

    /**
     * @brief Close a WebSocket connection using an encoded close payload.
     * @param fd Socket file descriptor.
     * @param closeCodeAndMessage Encoded close payload
     *        (2 bytes close code + optional UTF-8 reason).
     * @return true if closed successfully, false otherwise.
     */
    bool closeFD(const int &fd, const std::string &closeCodeAndMessage);

    /**
     * @brief Close a WebSocket connection using standard RFC 6455 format.
     * @param fd Socket file descriptor.
     * @param code WebSocket close code (default: 1000).
     * @param message Optional close reason.
     * @return true if closed successfully, false otherwise.
     */
    bool closeFD(const int &fd, const short &code = 1000, const std::string &message = "bye");

    /**
     * @brief Send a WebSocket message to a specific client.
     * @param fd Client socket file descriptor.
     * @param msg Message payload.
     * @param type Frame opcode type (default: text frame).
     * @return true if sent successfully, false otherwise.
     */
    bool sendMessage(
        const int &fd,
        const std::string &msg,
        const std::string &type = "0001")
    {
        WebSocketServerFDHandler k;
        k.setFD(fd, getSSL(fd), unblock);
        return k.sendMessage(msg, type);
    }

    /**
     * @brief Close all connections and stop listening.
     * @note This call blocks until all resources are released.
     */
    bool close();
    /**
        * @brief Close the connection of a certain socket
        @ @note Polymorphism of TcpServer's close for a certain socket. close Tcp connection directly and do not use websocket rule.
        */
        bool close(const int &fd);
    /**
     * @brief Start the WebSocket server.
     * @param port Listening port.
     * @param threads Number of worker threads (default: 8).
     */
    bool startListen(const int &port, const int &threads = 8)
    {
        //std::thread(&WebSocketServer::HB, this).detach();
        return TcpServer::startListen(port, threads);
    }

    /**
     * @brief Broadcast a WebSocket message to all connected clients.
     * @param msg Message payload.
     * @param type Frame opcode type.
     */
    void sendMessage(const std::string &msg, const std::string &type = "0001");

    /**
     * @brief Destructor.
     * @note Blocks until all connections, heartbeat threads,
     *       and listening sockets are fully closed.
     */
    ~WebSocketServer()
    {
        
    }
};


    /**
    * @brief UDP operation class
    * Pass in the socket for UDP protocol operations
    */
    class UdpFDHandler
    {
    protected:
        int fd = -1;
        bool flag1 = false;
        bool flag2 = false;
        int sec = -1;
    public:
        /**
        * @brief Set fd
        * @param fd Socket fd to be passed in
        * @param flag1 true: Set to non-blocking mode, false: Set to blocking mode (default is blocking mode)
        * @param sec Set blocking timeout (seconds) (default is -1, i.e., infinite wait)
        * @param flag2 true: Set SO_REUSEADDR mode, false: Do not set SO_REUSEADDR mode
        */
        void setFD(const int &fd, const bool &flag1 = false, const int &sec = -1, const bool &flag2 = false);
        /**
        * @brief Set to blocking mode
        * @param sec Blocking timeout, no longer block if blocking exceeds this time, default is -1, i.e., infinite wait
        */
        void blockSet(const int &sec = -1);
        /**
        * @brief Set to non-blocking mode
        */
        void unblockSet();
        /**
        * @brief Set SO_REUSEADDR mode
        * @return true: Set successfully, false: Set failed
        */
        bool multiUseSet();
        /**
        * @brief Return fd
        */
        int getFD() { return fd; }
        /**
        * @brief Empty the object and close the socket
        * @param cle true: Empty the object and close the socket, false: Only empty the object, do not close the socket
        */
        void close(const bool &cle = true);
        /**
        * @brief Send string data to the target.
        *
        * @param data Data content to be sent (std::string type).
        * @param block Whether to send in blocking mode (default true).
        *              - true: Will block until all data is sent successfully unless an error occurs (regardless of socket blocking state);
        *              - false: Blocking depends on socket state.
        * 
        * @return
        * - Return value > 0: Number of bytes successfully sent;
        * - Return value <= 0: Sending failed;
        *   - -98: Target error
        *   - -99: Object not bound to a socket;
        *   - -100: Send buffer full in non-blocking mode.
        *
        * @note If block is true, it will continue to block until all data is sent unless an error occurs 
        *       (regardless of socket blocking state), suitable for scenarios where complete sending is required.
        *       If block is false, blocking depends on socket state. The return value may be less than length, 
        *       requiring manual handling of remaining data.
        */
        int sendData(const std::string &data, const std::string &ip, const int &port, const bool &block = true);
        /**
        * @brief Send a specified length of binary data to the target.
        *
        * @param data Pointer to the data buffer to be sent.
        * @param length Data length (bytes).
        * @param block Whether to send in blocking mode (default true).
         *              - true: Will block until all data is sent successfully unless an error occurs (regardless of socket blocking state);
         *              - false: Blocking depends on socket state.
        * 
        * @return
        * - Return value > 0: Number of bytes successfully sent;
        * - Return value <= 0: Sending failed;
        *   - -98: Target error
        *   - -99: Object not bound to a socket;
        *   - -100: Send buffer full in non-blocking mode.
        *
        * @note If block is true, it will continue to block until all data is sent unless an error occurs.
        *       If block is false, blocking depends on socket state. The return value may be less than length, 
        *       requiring manual handling of remaining data.
        */
        int sendData(const char *data, const uint64_t &length, const std::string &ip, const int &port, const bool &block = true);
        /**
        * @brief Receive data once into a string container
        * @param data Data container for received data (string type)
        * @param length Maximum reception length
        * @param ip Record the source ip of the sender
        * @param port Record the source port of the sender
        * @return 
        * - Return value > 0: Number of bytes successfully received;
        * - Return value = 0: Connection closed;
        * - Return value < 0: Reception failed;
        *   - -99: Object not bound to a socket;
        *   - -100: No data in non-blocking mode
        * @note Whether reception blocks depends on the fd's blocking state
        */
        int recvData(std::string &data, const uint64_t &length, std::string &ip, int &port);
        /**
        * @brief Receive data once into a char* container
        * @param data Data container for received data (char* type)
        * @param length Maximum reception length
        * @param ip Record the source ip of the sender
        * @param port Record the source port of the sender
        * @return 
        * - Return value > 0: Number of bytes successfully received;
        * - Return value = 0: Connection closed;
        * - Return value < 0: Reception failed;
        *   - -99: Object not bound to a socket;
        *   - -100: No data in non-blocking mode
        * @note Whether reception blocks depends on the fd's blocking state
        */
        int recvData(char *data, const uint64_t &length, std::string &ip, int &port);
        
    };
    /**
    * @brief Udp client operation class
    */
    class UdpClient : public UdpFDHandler
    {
    public:
        /**
        * @brief Constructor
        * @param flag1 true: Non-blocking mode, false: Blocking mode (default blocking mode)
        * @param sec Set blocking timeout (seconds) (default is infinite wait)
        */
        UdpClient(const bool &flag1 = false, const int &sec = -1);
        /**
        * @brief Destroy the original socket and recreate a client
        * @param flag1 true: Non-blocking mode, false: Blocking mode (default blocking mode)
        * @param sec Set blocking timeout (seconds) (default is infinite wait)
        */
        bool createFD(const bool &flag1 = false, const int &sec = -1);
        /**
        * @brief Destructor, closes the socket when the object's life ends
        */
        ~UdpClient() { close(); }
    };
    /**
    * @brief Udp server operation class
    */
    class UdpServer : public UdpFDHandler
    {
    public:
        /**
        * @brief Constructor
        * @param port Port number
        * @param flag1 true: Set to non-blocking mode, false: Set to blocking mode (default is blocking mode)
        * @param sec Set blocking timeout (seconds) (default is -1, i.e., infinite wait)
        * @param flag2 true: Set SO_REUSEADDR mode, false: Do not set SO_REUSEADDR mode
        */
        UdpServer(const int &port, const bool &flag1 = false, const int &sec = -1, const bool &flag2 = true);
        /**
        * @brief Destroy the original socket and recreate a server
        * @param flag1 true: Set to non-blocking mode, false: Set to blocking mode (default is blocking mode)
        * @param sec Set blocking timeout (seconds) (default is -1, i.e., infinite wait)
        * @param flag2 true: Set SO_REUSEADDR mode, false: Do not set SO_REUSEADDR mode
        */
        bool createFD(const int &port, const bool &flag1 = false, const int &sec = -1, const bool &flag2 = true);
        /**
        * @brief Destructor, closes the socket when the object's life ends
        */
        ~UdpServer() { close(); }
    };
    }
    /**
    * @namespace stt::system
    * @brief System settings, process control, heartbeat monitoring, etc.
    * @ingroup stt
    */
    namespace system
    {
        /**
        * @brief Class for initializing the service system
        * - Suitable for initializing service programs that need to run stably
        * - If the passed log file object is uninitialized (empty), the system automatically creates a server_log folder in the program directory 
        *   and generates a log file based on the current time; if it is an initialized object, the log file under the current object is enabled.
        * - Once the logging system is set up, it generates log files to record the operational dynamics of the service network program
        * - The effective working time of the logging system is related to the lifecycle of the passed log file object
        */
        class ServerSetting
        {
        public:
            /**
            * @brief Pointer to the log file object for reading and writing logs in the system
            */
            static file::LogFile *logfile;
            /**
            * @brief Language selection for the system's logging system, default is English
            */
            static std::string language;
        private:
            static void signalterminated(){std::cout<<"Terminated by uncaught exception"<<std::endl;if(ServerSetting::logfile!=nullptr){if(ServerSetting::language=="Chinese")ServerSetting::logfile->writeLog("未捕获的异常终止");else ServerSetting::logfile->writeLog("end for uncaught exception");}kill(getpid(),15);}
            static void signalSIGSEGV(int signal){std::cout<<"SIGSEGV"<<std::endl;if(ServerSetting::logfile!=nullptr){if(ServerSetting::language=="Chinese")ServerSetting::logfile->writeLog("信号SIGSEGV");else ServerSetting::logfile->writeLog("signal SIGSEGV");}kill(getpid(),15);}
            static void signalSIGABRT(int signal){std::cout<<"SIGABRT"<<std::endl;if(ServerSetting::logfile!=nullptr){if(ServerSetting::language=="Chinese")ServerSetting::logfile->writeLog("信号SIGABRT");else ServerSetting::logfile->writeLog("signal SIGABRT");}kill(getpid(),15);}
        public:
            /**
            * @brief Set system signals
            * - Blocks signals 1-14 and 14-64
            * - Sends signal 15 when receiving SIGSEGV
            * - Sends signal 15 when an uncaught exception occurs
            * - Sends signal 15 when receiving SIGABRT
            * - The exit method for signal 15 is customized
            */
            static void setExceptionHandling();
            /**
            * @brief Set the log file object for the logging system
            * If the passed log file object is uninitialized (empty), the system automatically generates a server_log folder in the program directory 
            * and creates a log file based on the current time to record the network communication of the service program.
            * If the passed log file object is initialized, the log file under the current object is enabled.
            * @param logfile Pointer to the log file object. If the object is uninitialized, the system automatically generates a server_log folder 
            *                and creates a log file based on the current time. (Default is nullptr, no log file is set)
            * @param language Language of the log file. "Chinese" for Chinese, otherwise English. (Default is empty, set to English)
            */
            static void setLogFile(file::LogFile *logfile = nullptr, const std::string &language = "");
            /**
            * @brief Execute the setExceptionHandling and setfile::LogFile functions to complete the initialization of signals and the logging system
            * @param logfile Pointer to the log file object. If the object is uninitialized, the system automatically generates a server_log folder 
            *                and creates a log file based on the current time. (Default is nullptr, no log file is set)
            * @param language Language of the log file. "Chinese" for Chinese, otherwise English. (Default is empty, set to English)
            */
            static void init(file::LogFile *logfile = nullptr, const std::string &language = "");
        };
        
        /**
        * @brief Synchronization tool class encapsulating System V semaphores.
        * 
        * `csemp` provides a mutual exclusion mechanism, supporting inter-process synchronization operations. By encapsulating system calls 
        * such as semget, semop, semctl, it implements semaphore initialization, P (wait), V (release), destruction, and current value reading.
        * 
        * Copy construction and assignment operators are disabled to ensure resource uniqueness.
        */
        class csemp
        {
        private:
            /**
            * @brief Union parameter for semctl() system call.
            * 
            * `semun` is a user-defined structure necessary for setting semaphore attributes in the System V interface.
            */
            union semun  
            {
                int val;                ///< Set the value of the semaphore (used for SETVAL)
                struct semid_ds *buf;   ///< Semaphore status buffer (used for IPC_STAT, IPC_SET)
                unsigned short  *arry;  ///< Set the values of the semaphore array (used for SETALL)
            };

            int   m_semid;      ///< Semaphore ID obtained by semget.
            short m_sem_flg;    ///< Semaphore operation flags (such as SEM_UNDO).

            csemp(const csemp &) = delete;             ///< Disabled copy constructor
            csemp &operator=(const csemp &) = delete;  ///< Disabled assignment operator

        public:
            /**
            * @brief Constructor, initializes internal state.
            */
            csemp():m_semid(-1){}

            /**
            * @brief Initialize the semaphore.
            * 
            * If the semaphore already exists, it is obtained; otherwise, an attempt is made to create and set the initial value.
            * 
            * @param key Unique key of the semaphore.
            * @param value Initial value of the semaphore (default 1, representing a mutex).
            * @param sem_flg Semaphore operation flags, default SEM_UNDO, operations are automatically revoked when the process terminates.
            * @return true on success; false on failure.
            */
            bool init(key_t key, unsigned short value = 1, short sem_flg = SEM_UNDO);

            /**
            * @brief P operation (wait), attempts to subtract value from the semaphore.
            * 
            * If the current value is insufficient, it will block until available.
            * 
            * @param value Wait value (must be less than 0, default -1).
            * @return true on success; false on failure.
            */
            bool wait(short value = -1);

            /**
            * @brief V operation (release), attempts to add value to the semaphore.
            * 
            * Releases resources or wakes up waiting processes.
            * 
            * @param value Release value (must be greater than 0, default 1).
            * @return true on success; false on failure.
            */
            bool post(short value = 1);

            /**
            * @brief Get the current value of the semaphore.
            * 
            * @return The value of the semaphore; -1 on failure.
            */
            int getvalue();

            /**
            * @brief Destroy the current semaphore.
            * 
            * Generally used to clean up resources before the main process holding the semaphore exits.
            * 
            * @return true on success; false on failure.
            */
            bool destroy();

            /**
            * @brief Destructor, does not automatically destroy the semaphore.
            */
            ~csemp();
        };

        /**
        * @brief Define the macro MAX_PROCESS_NAME as 100, meaning the process name in process information does not exceed 100 bytes
        */
        #define MAX_PROCESS_NAME 100
        /**
        * @brief Define the macro MAX_PROCESS_INF as 1000, meaning the process information table records up to 1000 process information entries
        */
        #define MAX_PROCESS_INF 1000
        /**
        * @brief Define the macro SHARED_MEMORY_KEY as 0x5095, meaning the shared memory key for the process information table is 0x5095
        */
        #define SHARED_MEMORY_KEY 0x5095
        /**
        * @brief Define the macro SHARED_MEMORY_LOCK_KEY as 0x5095, meaning the semaphore key for operating the process information table is 0x5095
        */
        #define SHARED_MEMORY_LOCK_KEY 0x5095

        /**
        * @brief Structure for process information
        */
        struct ProcessInf
        {
            /**
            * @brief Process ID
            */
            pid_t pid;
            /**
            * @brief Last heartbeat time of the process, as a timestamp
            */
            time_t lastTime;
            /**
            * @brief Process name
            */
            char name[MAX_PROCESS_NAME];
            /**
            * @brief First parameter of the process
            */
            char argv0[20];
            /**
            * @brief Second parameter of the process
            */
            char argv1[20];
            /**
            * @brief Third parameter of the process
            */
            char argv2[20];
        };

        /**
        * @brief Class responsible for process heartbeat monitoring and scheduling
        * Used to monitor service processes and ensure they continue to run effectively
        * After the process ends, the shared memory at 0x5095 and the semaphore are not deleted
        * Currently only supports processes with up to three parameters to join monitoring
        * The logic of joining the heartbeat monitoring system, updating heartbeats, and checking the heartbeat system should be manually written in the program. This class only provides call interfaces.
        */
        class HBSystem
        {
        private:
        
            static ProcessInf *p;
            static csemp plock;
            static bool isJoin;
        public:
            /**
            * @brief Add a process to the heartbeat system
            * @param name Absolute path of the process name
            * @param argv0 First parameter of the process
            * @param argv1 Second parameter of the process
            * @param argv2 Third parameter of the process
            * @return true: Joined successfully, false: Join failed
            */
            bool join(const char *name, const char *argv0 = "", const char *argv1 = "", const char *argv2 = "");
            /**
            * @brief Update the heartbeat of the current process
            * @return true: Updated successfully, false: Update failed
            */
            bool renew();
            /**
            * @brief Output information of all processes in the heartbeat monitoring system
            */
            static void list();
            /**
            * @brief Check the heartbeat monitoring system
            * If the time difference between the last heartbeat update and the current time is greater than or equal to sec seconds, kill the process
            * First send signal 15 to kill the process. If the process still exists after 8 seconds, send signal 9 to force kill
            * @return true: Operation successful, false: Operation failed
            */
            static bool HBCheck(const int &sec);
            /**
            * @brief Remove the current process from the heartbeat system
            * @return true: Operation successful, false: Operation failed
            */
            bool deleteFromHBS();
            /**
            * @brief Destructor of HBSystem
            * - Remove the current process from the heartbeat system
            */
            ~HBSystem();
        };

        /**
        * @brief Static utility class for process management
        */
        class Process
        {
        public:
        

            /**
            * @brief Start a new process (with the option to restart periodically)
            * 
            * When `sec == -1`, the child process is started only once; otherwise, an auxiliary child process is created to restart the target process periodically.
            * 
            * - When starting periodically, the auxiliary process blocks all signals.
            * - The started target process blocks all signals except SIGCHLD and SIGTERM.
            * 
            * @param Args Variable parameter types (used to pass to the target program's argv)
            * @param name Path of the program to execute (e.g., `/usr/bin/myapp`)
            * @param sec Time interval (in seconds). If -1, start only once without periodicity.
            * @param args Startup parameters (the first parameter must be the program name, i.e., argv[0], no need to add nullptr at the end)
            * @return true The parent process returns true indicating successful startup (or scheduling)
            * @return false Returns false when fork or execv fails
            * 
            * @note
            * - No need to manually add nullptr to the parameters; it is added internally.
            * - No exception is thrown on execution failure; returns false.
            * - execv replaces the current process image in the child process and does not return.
            */
            template<class... Args>
            static bool startProcess(const std::string &name, const int &sec = -1, Args ...args)
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
                        execv(name.c_str(),const_cast<char* const*>(paramList.data()));
                        return false;
                    }
                }
                for(int ii=1;ii<=64;ii++)
                    signal(ii,SIG_IGN);           
                pid_t pid=fork();
                if(pid==-1)
                    return false;
                if(pid>0)
                {   
                    return true;
                }
                else
                {
                    signal(SIGCHLD,SIG_DFL);
                    signal(15,SIG_DFL);
                    while(1)
                    {
                        pid=fork();
                        if(fork()==0)
                        {
                            execv(name.c_str(),const_cast<char* const*>(paramList.data()));
                            exit(0);
                        }
                        else if(pid>0)
                        {
                            int sts;
                            wait(&sts);
                            sleep(sec);
                        }
                        else
                            continue;
                    }
                }
            }
            /**
            * @brief Create a child process through a function (with the option to restart periodically)
            * 
            * Use a callable object (such as Lambda, function pointer, std::function) as the main logic of the new child process.
            * 
            * - When `sec == -1`, the function is executed only once;
            * - Otherwise, an auxiliary process is created to periodically fork and execute the function.
            * 
            * @param Fn Callable object type (such as function, Lambda)
            * @param Args Parameter types for the callable object
            * @param fn Function or callable object to execute
            * @param sec Time interval (in seconds). If -1, execute only once without periodicity.
            * @param args Parameters to pass to the function
            * @return true The parent process returns true indicating successful startup; the child process also returns true (for chaining, etc.)
            * @return false Failed to create the process
            * 
            * @note
            * - The actual execution of the function is in the newly forked child process.
            * - The function fn must be callable; parameter types must be perfectly forwarded.
            * - The child process exits after execution; the auxiliary process periodically recreates it.
            */
            template<class Fn, class... Args>
            static typename std::enable_if<!std::is_convertible<Fn, std::string>::value, bool>::type
            startProcess(Fn&& fn, const int &sec = -1, Args &&...args)
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
                        auto f=std::bind(std::forward<Fn>(fn),std::forward<Args>(args)...);
                        f();
                        return true;
                    }
                }
                for(int ii=1;ii<=64;ii++)
                    signal(ii,SIG_IGN);           
                pid_t pid=fork();
                if(pid==-1)
                    return false;
                if(pid>0)
                {   
                    return true;
                }
                else
                {
                    signal(SIGCHLD,SIG_DFL);
                    signal(15,SIG_DFL);
                    while(1)
                    {
                        pid=fork();
                        if(pid==0)
                        {
                            auto f=std::bind(std::forward<Fn>(fn),std::forward<Args>(args)...);
                            f();
                            return true;
                        }
                        else if(pid>0)
                        {
                            int sts;
                            wait(&sts);
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
        * @brief Fixed-size worker thread pool
        *
        * WorkerPool Internally, it maintains a task queue and several worker threads.
        * Each worker thread takes a task from the queue and executes it in a loop.
        *
        * ## characteristic
        * - Fixed number of threads
        * - Thread-safe task submission
        * - Supports graceful shutdown.
        *
        * ## Thread safety instructions
        * - submit() is thread-safe
        * - stop() is thread-safe
        *
        * ## Usage example
        * @code
        * WorkerPool pool(4);
        * pool.submit([] {
        *     // perform tasks
        * });
        * pool.stop();
        * @endcode
        */
        class WorkerPool 
        {
        public:
            /**
            * @brief Constructor, creates a specified number of worker threads.
            *
            * @param n Number of worker threads
            *
            * Once constructed, all threads start immediately and enter a waiting state.
            */
            explicit WorkerPool(size_t n):stop_(false)
            {
                for (size_t i = 0; i < n; ++i) {
                    threads_.emplace_back([this] {
                        this->workerLoop();
                    });
                }
            }
            /**
            * @brief destructor
            *
            * stop() is automatically called during destruction.
            * Ensure all threads exit and are correctly joined.
            */
            ~WorkerPool() 
            {
                stop();
            }
            /**
            * @brief Submit a task to the thread pool
            *
            * @param task Callable objects, with the function signature void()
            *
            * The task is placed in an internal queue and executed asynchronously by a worker thread.
            */
            void submit(Task task) 
            {
                {
                    std::lock_guard<std::mutex> lk(mtx_);
                    tasks_.push(std::move(task));
                }
                cv_.notify_one();
            }
            /**
             * @brief Stop the thread pool and wait for all threads to exit.
             *
            * After calling:
            * - No new tasks should be submitted.
            * - Submitted but not yet executed tasks will be completed.
            * - All worker threads will exit and join.
            *
            * This function can be called repeatedly safely.
            */
            void stop() 
            {
                {
                    std::lock_guard<std::mutex> lk(mtx_);
                    stop_ = true;
                }
                cv_.notify_all();
                for (auto &t : threads_) 
                {
                    if (t.joinable()) t.join();
                }
            }

        private:
            /**
            * @brief Worker thread main loop
            *
            * This function will be executed by each worker thread:
            * - Waiting for a task or stop signal on a condition variable.
            * - Retrieve tasks from the task queue.
            * - perform tasks
            *
            * The thread exits when stop_ is true and the task queue is empty.
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
                    }
                    task(); // perform tasks
                }
            }

        private:
            std::vector<std::thread> threads_;
            std::queue<Task> tasks_;
            std::mutex mtx_;
            std::condition_variable cv_;
            bool stop_;
        };
    }

    
}


#endif