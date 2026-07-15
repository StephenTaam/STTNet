#include"../include/sttnet.h"
#include <openssl/rand.h>
using namespace std;
using namespace stt::file;
using namespace stt::time;
using namespace stt::data;
using namespace stt::network;
using namespace stt::system;

namespace {
void recordAtomicMaximum(std::atomic<uint64_t> &target,const uint64_t value) noexcept
{
    uint64_t current=target.load(std::memory_order_relaxed);
    while(current<value&&!target.compare_exchange_weak(
          current,value,std::memory_order_relaxed,std::memory_order_relaxed)) {}
}

char asciiLower(const char value) noexcept
{
    return value>='A'&&value<='Z'?static_cast<char>(value-'A'+'a'):value;
}

bool asciiCaseEqual(const std::string_view left,const std::string_view right) noexcept
{
    if(left.size()!=right.size())
        return false;
    for(size_t index=0;index<left.size();++index)
    {
        if(asciiLower(left[index])!=asciiLower(right[index]))
            return false;
    }
    return true;
}

std::string_view trimHttpWhitespace(std::string_view value) noexcept
{
    while(!value.empty()&&(value.front()==' '||value.front()=='\t'))
        value.remove_prefix(1);
    while(!value.empty()&&(value.back()==' '||value.back()=='\t'))
        value.remove_suffix(1);
    return value;
}

bool getHttpHeaderValueCaseInsensitiveView(const std::string_view header,const std::string_view name,
                                           std::string_view &value) noexcept
{
    value={};
    size_t lineStart=0;
    while(lineStart<header.size())
    {
        size_t lineEnd=header.find("\r\n",lineStart);
        if(lineEnd==std::string_view::npos)
            lineEnd=header.size();
        if(lineEnd==lineStart)
            break;
        const std::string_view line=header.substr(lineStart,lineEnd-lineStart);
        const size_t colon=line.find(':');
        if(colon!=std::string_view::npos&&
           asciiCaseEqual(trimHttpWhitespace(line.substr(0,colon)),name))
        {
            value=trimHttpWhitespace(line.substr(colon+1));
            return true;
        }
        if(lineEnd==header.size())
            break;
        lineStart=lineEnd+2;
    }
    return false;
}

bool getHttpHeaderValueCaseInsensitive(const std::string &header,const std::string_view name,std::string &value)
{
    std::string_view view;
    if(!getHttpHeaderValueCaseInsensitiveView(header,name,view))
    {
        value.clear();
        return false;
    }
    value.assign(view.data(),view.size());
    return true;
}

bool httpHeaderContainsToken(const std::string_view value,const std::string_view expected) noexcept
{
    size_t tokenStart=0;
    while(tokenStart<=value.size())
    {
        size_t tokenEnd=value.find(',',tokenStart);
        if(tokenEnd==std::string_view::npos)
            tokenEnd=value.size();
        if(asciiCaseEqual(trimHttpWhitespace(value.substr(tokenStart,tokenEnd-tokenStart)),expected))
            return true;
        if(tokenEnd==value.size())
            break;
        tokenStart=tokenEnd+1;
    }
    return false;
}

bool isSwitchingProtocolsStatus(const std::string_view statusLine) noexcept
{
    constexpr std::string_view prefix="HTTP/1.1 ";
    return statusLine.size()>=prefix.size()+3&&
           statusLine.substr(0,prefix.size())==prefix&&
           statusLine.substr(prefix.size(),3)=="101"&&
           (statusLine.size()==prefix.size()+3||statusLine[prefix.size()+3]==' '||
            statusLine[prefix.size()+3]=='\t');
}
}


            //思路：先从尾到头遍历检查看看哪里的目录是不存在的，边检查边逐级创建
bool stt::file::FileTool::createDir(const string & ddir,const mode_t &mode)
{	
	string k=ddir;
	string test;
	size_t ii=1;
	while(1)
	{	
		if((ii=k.find('/',ii))==string::npos)//已经是最顶层
		{	
			if(access(k.c_str(),F_OK)<0)//不存在就创建
			{
				if(mkdir(k.c_str(),mode)<0)
				{
					//cout<<k<<endl;
					perror("mkdir() fail");
					return false;
				}
			}
			break;
		}
		//非最顶层
		test=k.substr(0,ii);
		if(access(test.c_str(),F_OK)<0)//不存在就创建
		{
                                if(mkdir(test.c_str(),mode)<0)
                                {
                                        //cout<<test<<endl;
                                        perror("mkdir() fail");
					return false;
                                }
                        
		}
		ii++;
	}		
	return true;
}
bool stt::file::FileTool::createFile(const string &filePath,const mode_t &mode)
{
    if(access(filePath.c_str(),F_OK)==0)//检查文件是否已经存在
    {
        //cerr<<"file has already exist"<<endl;
        return true;
    }
    //创建路径
    size_t ii=filePath.rfind("/");
    string path=filePath;
    if(ii!=string::npos)
    {
        path.erase(ii);
        if(createDir(path,0775)==false)
        {
            cerr<<"create path fail"<<endl;
            return false;
        }
    }
    //创建文件
    int fd=open(filePath.c_str(),O_CREAT|O_EXCL,mode);
    if(fd<0)
    {
        perror("open() fail");
        return false;
    }
    //关闭文件
    ::close(fd);
    return true;
}
bool stt::file::FileTool::copy(const string &sourceFile,const string &objectFile)
{
    ifstream source(sourceFile,ios::binary);
    if(!source.is_open())
        return false;
    ofstream target(objectFile,ios::binary|ios::trunc);
    if(!target.is_open())
        return false;
    target<<source.rdbuf();
    return (source.good()||source.eof())?target.good():false;
}
    size_t stt::file::FileTool::get_file_size(const string &fileName)
    {
        struct stat st;
        if(stat(fileName.c_str(),&st)==0)
        {
            return st.st_size;
        }
        else
            return -1;
    }
    mutex File::l1;
    unordered_map<string,FileThreadLock> File::fl2;
    bool stt::file::File::closeFile(const bool &del)
    {
        bool ok=true;
        if(!isOpen())//检查是否对象打开了文件
            return true;
        //看是否需要unlockmemory
        //如果还是锁住说明是突然关闭，那么理应回退内存，操作失败 
        if(memoryLockOwnedByCurrentThread())
            (void)unlockMemory(true);
        unique_lock<mutex> objectLock(che);
        if(!isOpen())
            return true;

        //关闭锁注册表
        //if(l1use)
        unique_lock<mutex> lock2(l1);
        auto ii=fl2.find(fileName);
        if(ii!=fl2.end())
        {
        if(ii->second.threads==1)
        {
            fl2.erase(ii);
        }
        else
            ii->second.threads--;
        }
        //if(l1use)
        lock2.unlock();
        //关闭读流
        fin.clear();//清除文件数据流
        fin.close();

        //if(fin.fail())
        //{
        //   perror("fin close() fail");
        //    ok=false;
        //}
        if(del==true)
        {
            if(remove(fileName.c_str())!=0)
            {
                perror("file delete() fail");
                ok=false;
            }
        }
        //清除内存数据
        if(binary)
        {
            if(data_binary!=nullptr)
            {
                delete[] data_binary;
                data_binary=nullptr;
            }
            if(backUp_binary!=nullptr)
            {
                delete[] backUp_binary;
                backUp_binary=nullptr;
            }
            size1=0;
            size2=0;
            malloced=0;
        }
        else
        {
            data.clear();
            backUp.clear();
            totalLines=0;
        }
        //关闭fd
        //::close(fd);
        //fd=-1;
        //完成
        flag.store(false,std::memory_order_release);
        return ok;
        
    }
    bool stt::file::File::toDisk()//创造临时文件 加锁 写 覆盖原文件。不在源文件直接写是怕清空内容后来不及写就发生故障 文字消失 (就算前面主文件有读写锁了，只能保证在这个类的操作不会影响这个临时文件，不代表其他不会，所以一样要加上读锁)
    {
        //创建一个临时文件
        int fdd=open(fileNameTemp.c_str(),O_RDWR|O_CREAT,mode);//问了进程锁 用posix的接口打开文件
        if(fdd<0)
        {
            cerr<<"临时文件创建失败"<<endl;
            return false;
        }
        //打开临时文件
        if(binary)
        {
            fout.open(fileNameTemp,ios::binary);
            if(!fout.is_open())
            {
                perror("tempfile fout open() fail");
                fout.clear();
                fout.close();
                ::close(fdd);
                return false;
            }
            fout.write(data_binary,size1);
            
            size=size1;
        }
        else
        {
            fout.open(fileNameTemp,ios::trunc);
            if(!fout.is_open())
            {
                perror("tempfile fout open() fail");
                fout.clear();
                fout.close();
                ::close(fdd);
                return false;
            }
            fout<<unitbuf;//不启用缓冲区

            //把内存数据存到一个字符串
            string dataa;
            for(auto &ii:this->data)
            {
                dataa+=ii+"\n";
            }
            if(dataa!="")
                dataa.erase(dataa.size()-1);

            fout<<dataa;

            totalLines=this->data.size();
        }
        
        fout.clear();
        fout.close();
        if(fout.fail())
        {
            perror("temp fout close() fail");
            ::close(fdd);
            return false;
        }
        if(rename(fileNameTemp.c_str(),fileName.c_str())<0)
		{
			perror("temp rename() fail");
            ::close(fdd);
			return false;
		}
        ::close(fdd);
        return true;
    }

    void stt::file::File::toMemory()
    {
        fin.close();
        if(binary)
        {
            /*
            fin.open(fileName,ios::binary);
            fin.seekg(0,ios::end);
            streampos file_size = fin.tellg(); // 获取文件大小
            size=static_cast<size_t>(file_size);

            fin.seekg(0,ios::beg);
            */
            size=FileTool::get_file_size(fileName);
            fin.open(fileName,ios::binary);
            
            if(multiple_backup==0)
            {
                if(size>0&&static_cast<size_t>(multiple)>
                   std::numeric_limits<size_t>::max()/size)
                    malloced=size;
                else
                    malloced=size*static_cast<size_t>(multiple);
            }
            else
            {
                malloced=std::max(size,multiple_backup);
            }

            char *newData=malloced==0?nullptr:new(std::nothrow) char[malloced];
            char *newBackup=malloced==0?nullptr:new(std::nothrow) char[malloced];
            if(malloced>0&&(newData==nullptr||newBackup==nullptr))
            {
                delete[] newData;
                delete[] newBackup;
                malloced=0;
                size1=0;
                size2=0;
                return;
            }
            delete[] data_binary;
            delete[] backUp_binary;
            data_binary=newData;
            backUp_binary=newBackup;
            if(size>0)
            {
                fin.read(data_binary,static_cast<std::streamsize>(size));
                memcpy(backUp_binary,data_binary,size);
            }

            size1=size;
            size2=size;
        }
        else
        {
            fin.open(fileName,ios::in);
            string dataa;
            data.clear();
            fin.seekg(0, ios::beg); 
            while(1)
            {
                getline(fin,dataa);
                if(fin)
                {
                    //cout<<"+dataa:"<<dataa<<endl;
                    data.push_back(dataa);
                }
                else
                {
                    //fin.seekg(0,ios::beg);
                    break;
                }
            }
            totalLines=data.size();
        }
        
        //fin.seekg(0, ios::end); // 移动到文件末尾
        //streampos file_size = fin.tellg(); // 获取文件大小
        //size=static_cast<size_t>(file_size);
    }
    bool stt::file::File::openFile(const string & fileName,const bool &create,const int &multiple,const size_t &size,const mode_t &mode)
    {
        if(isOpen())
        {
            if(!closeFile(false))
            {
                cerr<<"对象无法关闭已经打开的文件"<<endl;
                return false;
            }
        }
        if(!create)
        {
            if(access(fileName.c_str(),F_OK)!=0)//检查文件是否已经存在
                return false;
        }
        this->mode=mode;
        if(multiple>=1)
        {
            this->binary=true;
            this->multiple=multiple;
            this->multiple_backup=size;
        }
        else
            this->binary=false;
        this->fileName=fileName;
        this->fileNameTemp=fileName+".temp";
        if(!createFile(fileName,mode))
        {
            cerr<<"文件不存在 且创建文件失败"<<endl;
            return false;
        }
        //读文件流
        /*
        //std::ios::in：打开文件用于读取（默认模式）。
	    //std::ios::out：打开文件用于写入。
	    //std::ios::app：打开文件用于追加写入，文件指针移动到文件末尾。
	    //std::ios::binary：以二进制模式打开文件，用于处理二进制数据。
        */
        if(binary)
        {
            fin.open(fileName,ios::binary);
        }
        else
        {
            fin.open(fileName,ios::in);
        }
        if(!fin.is_open())
        {
            perror("file fin open() fail");
            return false;
        }
        
        //把数据读入到容器
        //toMemory();
        //if(l1use)
        unique_lock<mutex> lock2(l1);
        auto ii=fl2.find(fileName);
        if(ii!=fl2.end())
            ii->second.threads++;
        else
        {
            File::fl2.emplace(piecewise_construct,forward_as_tuple(fileName),forward_as_tuple(fileName,1));
        }
        //if(l1use)
        lock2.unlock();
        flag.store(true,std::memory_order_release);
        return true;
    }
    string& stt::file::File::read(string &data,const int &linePos,const int &num)
    {
        unique_lock<mutex> lock1(fl1); //因为要调用size函数 所以要上锁小心改变
        toMemory();
        
        return readC(data,linePos,num);
    }
    string& stt::file::File::readC(string &data,const int &linePos,const int &num)
    {
        data="";
        if(!isOpen())
            return data;
        if(linePos>=1&&num>0&&static_cast<size_t>(linePos)<=this->data.size()&&
           static_cast<size_t>(num)<=this->data.size()-static_cast<size_t>(linePos)+1)
        {
            const size_t first=static_cast<size_t>(linePos-1);
            for(size_t offset=0;offset<static_cast<size_t>(num);++offset)
            {
                data+=this->data[first+offset]+"\n";
            }
            if(data!="")
                data.erase(data.size()-1);
            return data;
        }
        else
            return data;
    }
    bool stt::file::File::readLine(string &data,const int linePos)
    {
        unique_lock<mutex> lock1(fl1); //因为要调用size函数 所以要上锁小心改变
        toMemory();
        
        return readLineC(data,linePos);
    }
    
    bool stt::file::File::readLineC(string &data,const int linePos)
    {
        if(!isOpen())
        {
            cerr<<"对象没有打开文件 无法调用函数"<<endl;
            return false;
        }
        if(linePos<1||static_cast<size_t>(linePos)>this->data.size())
            return false;
        data=this->data[linePos-1];
        return true;
    }
    
    string& stt::file::File::readAll(string &data)
    {
        unique_lock<mutex> lock1(fl1);//由于是read，不需要加锁（要确保读写统一性请组合操作 组合操作前后操作锁），因为要遍历容器，所以要上内存的容器锁
        toMemory();
        readAllC(data);
        return data;
    }
    
    string& stt::file::File::readAllC(string &data)
    {
        data="";
        if(!isOpen())
        {
            cerr<<"对象没有打开文件 无法调用函数"<<endl;
            return data;
        }
        
        for(auto &ii:this->data)
        {
            data+=ii+"\n";
        }

        if(data!="")
            data.erase(data.size()-1);
        return data;
    }
    
    void stt::file::File::lockfl2()
    {
        //if(l1use)
        unique_lock<mutex> lock2(l1);
        auto ii=fl2.find(fileName);
        if(ii==fl2.end())
        {
            //cout<<fileName<<endl;
            //cout<<"lock找不到文件"<<endl;
        }
        else
            ii->second.lock.lock();
    }
    void stt::file::File::unlockfl2()
    {
        //if(l1use)
        unique_lock<mutex> lock2(l1);
        auto ii=fl2.find(fileName);
        //if(ii==fl2.end())
            //cout<<"unlock找不到文件"<<endl;
        //else
        if(ii!=fl2.end())
            ii->second.lock.unlock();
    }
    bool stt::file::File::memoryLockOwnedByCurrentThread()
    {
        std::lock_guard<std::mutex> lock(memoryOwnerMutex);
        return memoryLocked&&memoryLockOwner==std::this_thread::get_id();
    }
    void stt::file::File::markMemoryLockOwned()
    {
        std::lock_guard<std::mutex> lock(memoryOwnerMutex);
        memoryLocked=true;
        memoryLockOwner=std::this_thread::get_id();
    }
    void stt::file::File::releaseMemoryLocks()
    {
        unlockfl2();
        fl1.unlock();
        {
            std::lock_guard<std::mutex> lock(memoryOwnerMutex);
            memoryLocked=false;
            memoryLockOwner=std::thread::id{};
        }
        che.unlock();
    }
    bool stt::file::File::lockMemory()
    {
        //if(fd<0)
        //{
        //    fd=open(fileName.c_str(),O_RDWR,mode);//问了进程锁 用posix的接口打开文件
        //    if(fd<0)
        //    {
        //        cerr<<"fd打开文件失败"<<endl;
        //        return false;
        //    }
        //}
        //if(flag_lock)
        //    unlockMemory(true);
        //flag_lock=true;
        if(memoryLockOwnedByCurrentThread())
            return false;
        che.lock();
        if(!isOpen())
        {
            che.unlock();
            return false;
        }
        bool fileLockHeld=false;
        bool memoryLockHeld=false;
        try
        {
        //线程上锁
        lockfl2();
        fileLockHeld=true;
         // 获取文件锁
        //struct flock fl;
        //fl.l_type = F_RDLCK;  // 写入锁
        //fl.l_whence = SEEK_SET;
        //fl.l_start = 0;
        //fl.l_len = 0;
        //if (fcntl(fd, F_SETLKW, &fl) == -1)
		//{
        //    ::close(fd);
        //    fd=-1;
        //    cerr << getpid()<<":failed to acquire lock." <<endl;
        //    return false;
        //}
        //File::fl2.lock();
        fl1.lock();
        memoryLockHeld=true;
        toMemory();
        if(!binary)
            backUp=data;
        }
        catch (...)
        {
            if(memoryLockHeld) fl1.unlock();
            if(fileLockHeld) unlockfl2();
            che.unlock();
            return false;
        }
        markMemoryLockOwned();
        return true;
    }
    bool stt::file::File::unlockMemory(const bool &rec)
    {
        if(!memoryLockOwnedByCurrentThread())
            return false;
        //if(!chee.owns_lock())
        //    return false;
        //struct flock fl;
        //fl.l_type = F_UNLCK;  // 解锁
        //fl.l_whence = SEEK_SET;
        //fl.l_start = 0;
        //fl.l_len = 0;
        //flag_lock=false;
        if(rec||!toDisk())
        {
            //释放原来文件的锁
            //File::fl2.unlock();
            //if (fcntl(fd, F_SETLK, &fl) == -1) 
	        //{
            //    cerr << getpid()<< "Failed to unlock file" << endl;
		    //    perror("unlock");
            //    //::close(fd);
            //    //fd不可以取消,取消了就无法处理没解锁的情况了
            //    //return false;
            //}
            //close(fd);
            //fd=-1;
            if(!binary)
                data=backUp;
            else
            {
                if(size2>0)
                    memcpy(data_binary,backUp_binary,size2);
                size1=size2;
            }

            if(rec==false)
            {
                releaseMemoryLocks();
                return false;
            }
            else
            {
                releaseMemoryLocks();
                return true;//因为别的原因（导入了参数，而参数来自其他操作）需要回退内存也是返回false
            }
        }
        else
        {
            //释放原来文件的锁
            //File::fl2.unlock();
            //if (fcntl(fd, F_SETLK, &fl) == -1) 
	        //{
            //    cerr << getpid()<< "Failed to unlock file" << endl;
		    //    perror("unlock");
            //    //::close(fd);
            //    //fd不可以取消
            //    //return false;
            //}
            //close(fd);
            //fd=-1;
            releaseMemoryLocks();
            return true;
        }
    }
    int stt::file::File::findC(const string &targetString,const int linePos)
    {
        if(!isOpen())
        {
            cerr<<"对象没有打开文件 无法调用函数"<<endl;
            return -1;
        }
        if(linePos<1||static_cast<size_t>(linePos)>this->data.size())
            return -1;
        for(size_t pos=static_cast<size_t>(linePos);pos<=this->data.size();++pos)
        {
            if(this->data[pos-1].find(targetString)!=string::npos)
            {
                return pos<=static_cast<size_t>(std::numeric_limits<int>::max())?
                    static_cast<int>(pos):-1;
            }
        }
        return -1;
    }
    int stt::file::File::find(const string &targetString,const int linePos)
    {
        int rec;
        //fl1.lock();//不涉及内存改变 正常加锁就行
        unique_lock<mutex> lock1(fl1);
        //new:需要加锁（内存容器的） 因为调用了size函数
        toMemory();
        rec=findC(targetString,linePos);
        //lock1.unlock();
        return rec;
    }
    bool stt::file::File::appendLineC(const string &data,const int &linePos)
    {
        if(!isOpen())
        {
            cerr<<"对象没有打开文件 无法调用函数"<<endl;
            return false;
        }
        if(linePos==0)
        {
            this->data.push_back(data);
        }
        else
        {
            if(linePos<0||static_cast<size_t>(linePos)>this->data.size())
            {
                cerr<<"插入行无法定位"<<endl;
                return false;
            }
            this->data.insert(this->data.begin()+linePos-1,data);
        }
        return true;

    }
    bool stt::file::File::appendLine(const string &data,const int &linePos)
    {
        if(!lockMemory())
            return false;
        return unlockMemory(!appendLineC(data,linePos));
    }
    bool stt::file::File::deleteLineC(const int &linePos)
    {
        if(!isOpen())
        {
            cerr<<"对象没有打开文件 无法调用函数"<<endl;
            return false;
        }
        if(linePos==0)
        {
            if(this->data.empty())
                return false;
            this->data.pop_back();
        }
        else
        {
            if(linePos<0||static_cast<size_t>(linePos)>this->data.size())
            {
                cerr<<"删除行无法定位"<<endl;
                return false;
            }
            this->data.erase(this->data.begin()+linePos-1);
        }
        return true;
    }
    bool stt::file::File::deleteLine(const int &linePos)
    {
        if(!lockMemory())
            return false;
        return unlockMemory(!deleteLineC(linePos));
    }
    bool stt::file::File::deleteAllC()
    {
        if(!isOpen())
        {
            cerr<<"对象没有打开文件 无法调用函数"<<endl;
            return false;
        }
        this->data.clear();
        return true;
    }
    bool stt::file::File::deleteAll()
    {
        if(!lockMemory())
            return false;
        return unlockMemory(!deleteAllC());
    }
    bool stt::file::File::chgLineC(const string &data,const int &linePos)
    {
        if(!isOpen())
        {
            cerr<<"对象没有打开文件 无法调用函数"<<endl;
            return false;
        }
        if(linePos==0)
        {
            if(this->data.empty())
                return false;
            this->data[this->data.size()-1]=data;
        }
        else
        {
            if(linePos<0||static_cast<size_t>(linePos)>this->data.size())
            {
                cerr<<"修改行无法定位"<<endl;
                return false;
            }
            this->data[linePos-1]=data;
        }
        return true;
    }
    bool stt::file::File::chgLine(const string &data,const int &linePos)
    {
        if(!lockMemory())
            return false;
        return unlockMemory(!chgLineC(data,linePos));
    }
    /*
    char* File::read(char *data,const int &size,const int &pos)
    {
        string dataa;
        readAll(dataa);
        dataa=dataa.substr(pos,size);
        memcpy(data,dataa.data(),size);
        return data;
    }
    bool File::write(char *data,const int &size)
    {
        string dataa(data,size);
        return appendLine(dataa);
    }
    bool File::rewrite(char *data,const int &size)
    {
        lockMemory();
        if(!deleteAllC())
        {
            unlockMemory();
            return false;
        }
        string dataa(data,size);
        if(!appendLine(dataa))
        {
            unlockMemory();
            return false;
        }
        return unlockMemory();
    }
    */
    
    bool stt::file::File::read(char *data,const size_t &pos,const size_t &size)
    {
        unique_lock<mutex> lock1(fl1);
        //要上锁，防止容器的size改变了没及时发现，可能带来危险
        toMemory();
        return readC(data,pos,size);
    }
    bool stt::file::File::readC(char *data,const size_t &pos,const size_t &size)
    {
        //判断是否越界(是否在文件有效范围内)
        if((data!=nullptr||size==0)&&pos<=size1&&size<=size1-pos)
        {
            //读内存
            memcpy(data,data_binary+pos,size);
            return true;
        }
        return false;
    }
    bool stt::file::File::write(const char *data,const size_t &pos,const size_t &size)
    {
        if(!lockMemory())
            return false;
        return unlockMemory(!writeC(data,pos,size));
    }
    bool stt::file::File::writeC(const char *data,const size_t &pos,const size_t &size)
    {
        //判断是否越界(有没有超出限制)
        if((data!=nullptr||size==0)&&pos<=malloced&&size<=malloced-pos)
        {
            //写内存
            memcpy(data_binary+pos,data,size);
            //改变size1的值
            if(pos+size>size1)
                size1=pos+size;
            return true;
        }
        return false;
    }
    void stt::file::File::format()
    {
        if(!lockMemory())
            return;
        formatC();
        unlockMemory();
    }
    bool stt::file::File::formatC()
    {
        //格式化内存
        if(data_binary!=nullptr&&size1>0)
            memset(data_binary,0,size1);
        //改变size1的值
        size1=0;
        return true;
    }
    
    ostream& stt::time::operator<<(ostream &os,const Duration &a)
    {
        os<<"day="<<a.day<<" "<<a.hour<<":"<<a.min<<":"<<a.sec<<"."<<a.msec;
        return os;
    }

    bool stt::time::DateTime::strToTimePoint(const string &timeStr,const string &format,
                                              chrono::system_clock::time_point &result)
    {
        if(timeStr.size()!=format.size())
            return false;

        int year=0,month=0,day=0,hour=0,minute=0,second=0,millisecond=0;
        bool hasYear=false,hasMonth=false,hasDay=false,hasHour=false,hasMinute=false,hasSecond=false;

        const auto readNumber=[&timeStr](const size_t pos,const size_t length,int &value) {
            if(pos>timeStr.size()||length>timeStr.size()-pos)
                return false;
            const char *begin=timeStr.data()+pos;
            const char *end=begin+length;
            for(const char *current=begin;current!=end;++current)
            {
                if(*current<'0'||*current>'9')
                    return false;
            }
            const auto conversion=from_chars(begin,end,value);
            return conversion.ec==errc()&&conversion.ptr==end;
        };

        size_t pos=0;
        while(pos<format.size())
        {
            if(format.compare(pos,4,"yyyy")==0)
            {
                if(!readNumber(pos,4,year)) return false;
                hasYear=true;
                pos+=4;
            }
            else if(format.compare(pos,3,"sss")==0)
            {
                if(!readNumber(pos,3,millisecond)) return false;
                pos+=3;
            }
            else if(format.compare(pos,2,"mm")==0)
            {
                if(!readNumber(pos,2,month)) return false;
                hasMonth=true;
                pos+=2;
            }
            else if(format.compare(pos,2,"dd")==0)
            {
                if(!readNumber(pos,2,day)) return false;
                hasDay=true;
                pos+=2;
            }
            else if(format.compare(pos,2,"hh")==0)
            {
                if(!readNumber(pos,2,hour)) return false;
                hasHour=true;
                pos+=2;
            }
            else if(format.compare(pos,2,"mi")==0)
            {
                if(!readNumber(pos,2,minute)) return false;
                hasMinute=true;
                pos+=2;
            }
            else if(format.compare(pos,2,"ss")==0)
            {
                if(!readNumber(pos,2,second)) return false;
                hasSecond=true;
                pos+=2;
            }
            else
            {
                if(timeStr[pos]!=format[pos])
                    return false;
                ++pos;
            }
        }

        if(!hasYear||!hasMonth||!hasDay||!hasHour||!hasMinute||!hasSecond||
           month<1||month>12||day<1||day>31||hour>23||minute>59||second>59||
           millisecond>999)
            return false;

        tm parsed{};
        parsed.tm_year=year-1900;
        parsed.tm_mon=month-1;
        parsed.tm_mday=day;
        parsed.tm_hour=hour;
        parsed.tm_min=minute;
        parsed.tm_sec=second;
        parsed.tm_isdst=-1;
        const time_t value=mktime(&parsed);

        tm verified{};
        if(localtime_r(&value,&verified)==nullptr||
           verified.tm_year!=year-1900||verified.tm_mon!=month-1||verified.tm_mday!=day||
           verified.tm_hour!=hour||verified.tm_min!=minute||verified.tm_sec!=second)
            return false;

        result=chrono::system_clock::from_time_t(value)+chrono::milliseconds(millisecond);
        return true;
    }

    string& stt::time::DateTime::timePointToStr(const chrono::system_clock::time_point &tp,
                                                 string &timeStr,const string &format)
    {
        const time_t raw=chrono::system_clock::to_time_t(tp);
        tm local{};
        if(localtime_r(&raw,&local)==nullptr)
        {
            timeStr.clear();
            return timeStr;
        }

        long long epochMsec=chrono::duration_cast<chrono::milliseconds>(tp.time_since_epoch()).count();
        int millisecond=static_cast<int>(epochMsec%1000);
        if(millisecond<0)
            millisecond+=1000;

        const auto fixed=[](const int value,const int width) {
            ostringstream stream;
            stream<<setfill('0')<<setw(width)<<value;
            return stream.str();
        };

        timeStr.clear();
        timeStr.reserve(format.size()+8);
        size_t pos=0;
        while(pos<format.size())
        {
            if(format.compare(pos,4,"yyyy")==0)
            {
                timeStr+=fixed(local.tm_year+1900,4);
                pos+=4;
            }
            else if(format.compare(pos,3,"sss")==0)
            {
                timeStr+=fixed(millisecond,3);
                pos+=3;
            }
            else if(format.compare(pos,2,"mm")==0)
            {
                timeStr+=fixed(local.tm_mon+1,2);
                pos+=2;
            }
            else if(format.compare(pos,2,"dd")==0)
            {
                timeStr+=fixed(local.tm_mday,2);
                pos+=2;
            }
            else if(format.compare(pos,2,"hh")==0)
            {
                timeStr+=fixed(local.tm_hour,2);
                pos+=2;
            }
            else if(format.compare(pos,2,"mi")==0)
            {
                timeStr+=fixed(local.tm_min,2);
                pos+=2;
            }
            else if(format.compare(pos,2,"ss")==0)
            {
                timeStr+=fixed(local.tm_sec,2);
                pos+=2;
            }
            else
            {
                timeStr+=format[pos];
                ++pos;
            }
        }
        return timeStr;
    }

    string& stt::time::DateTime::getTime(string &timeStr,const string &format)
    {
        return timePointToStr(chrono::system_clock::now(),timeStr,format);
    }

    Duration& stt::time::DateTime::dTOD(const Milliseconds& d1,Duration &D1)
    {
        uint64_t remaining=d1.count();
        D1.day=static_cast<long long>(remaining/86400000ULL);
        remaining%=86400000ULL;
        D1.hour=static_cast<int>(remaining/3600000ULL);
        remaining%=3600000ULL;
        D1.min=static_cast<int>(remaining/60000ULL);
        remaining%=60000ULL;
        D1.sec=static_cast<int>(remaining/1000ULL);
        D1.msec=static_cast<int>(remaining%1000ULL);
        return D1;
    }

    bool stt::time::DateTime::DTOd(const Duration &D1,Milliseconds& d1)
    {
        const long long total=D1.convertToMsec();
        if(total<0)
        {
            d1=Milliseconds(0);
            return false;
        }
        d1=Milliseconds(static_cast<uint64_t>(total));
        return true;
    }

    bool stt::time::DateTime::convertFormat(string &timeStr,const string &oldFormat,const string &newFormat)
    {
        if(timeStr.size()!=oldFormat.size())
            return false;

        unordered_map<string,string> values;
        size_t pos=0;
        while(pos<oldFormat.size())
        {
            string token;
            if(oldFormat.compare(pos,4,"yyyy")==0) token="yyyy";
            else if(oldFormat.compare(pos,3,"sss")==0) token="sss";
            else if(oldFormat.compare(pos,2,"mm")==0) token="mm";
            else if(oldFormat.compare(pos,2,"dd")==0) token="dd";
            else if(oldFormat.compare(pos,2,"hh")==0) token="hh";
            else if(oldFormat.compare(pos,2,"mi")==0) token="mi";
            else if(oldFormat.compare(pos,2,"ss")==0) token="ss";

            if(!token.empty())
            {
                const string value=timeStr.substr(pos,token.size());
                if(!all_of(value.begin(),value.end(),[](const char ch){return ch>='0'&&ch<='9';}))
                    return false;
                values[token]=value;
                pos+=token.size();
            }
            else
            {
                if(timeStr[pos]!=oldFormat[pos])
                    return false;
                ++pos;
            }
        }

        string converted;
        converted.reserve(newFormat.size());
        pos=0;
        while(pos<newFormat.size())
        {
            string token;
            if(newFormat.compare(pos,4,"yyyy")==0) token="yyyy";
            else if(newFormat.compare(pos,3,"sss")==0) token="sss";
            else if(newFormat.compare(pos,2,"mm")==0) token="mm";
            else if(newFormat.compare(pos,2,"dd")==0) token="dd";
            else if(newFormat.compare(pos,2,"hh")==0) token="hh";
            else if(newFormat.compare(pos,2,"mi")==0) token="mi";
            else if(newFormat.compare(pos,2,"ss")==0) token="ss";

            if(!token.empty())
            {
                const auto found=values.find(token);
                if(found==values.end())
                    return false;
                converted+=found->second;
                pos+=token.size();
            }
            else
            {
                converted+=newFormat[pos];
                ++pos;
            }
        }
        timeStr=move(converted);
        return true;
    }

    Duration& stt::time::DateTime::calculateTime(const string &time1,const string &time2,
                                                  Duration &result,const string &format1,
                                                  const string &format2)
    {
        chrono::system_clock::time_point first;
        chrono::system_clock::time_point second;
        if(!strToTimePoint(time1,format1,first)||!strToTimePoint(time2,format2,second)||first<second)
        {
            result=Duration::invalid();
            return result;
        }
        const auto difference=chrono::duration_cast<chrono::milliseconds>(first-second).count();
        result.recoverForm(difference);
        return result;
    }

    string& stt::time::DateTime::calculateTime(const string &time1,const Duration &time2,
                                                string &result,const string &am,
                                                const string &format1,const string &format2)
    {
        chrono::system_clock::time_point first;
        Milliseconds interval;
        if(!strToTimePoint(time1,format1,first)||!DTOd(time2,interval)||(am!="+"&&am!="-"))
        {
            result.clear();
            return result;
        }
        const auto signedInterval=chrono::milliseconds(static_cast<long long>(interval.count()));
        return timePointToStr(am=="+"?first+signedInterval:first-signedInterval,result,format2);
    }

    bool stt::time::DateTime::startTiming()
    {
        if(isStart())
            return false;
        flag=true;
        start=chrono::steady_clock::now();
        return true;
    }

    Duration stt::time::DateTime::endTiming()
    {
        if(!isStart())
        {
            dt=Duration::invalid();
            return dt;
        }
        end=chrono::steady_clock::now();
        dTOD(chrono::duration_cast<Milliseconds>(end-start),dt);
        flag=false;
        return dt;
    }

    Duration stt::time::DateTime::checkTime()
    {
        if(!isStart())
        {
            dt=Duration::invalid();
            return dt;
        }
        end=chrono::steady_clock::now();
        dTOD(chrono::duration_cast<Milliseconds>(end-start),dt);
        return dt;
    }

    bool stt::time::DateTime::compareTime(const string &time1,const string &time2,
                                           const string &format1,const string &format2)
    {
        chrono::system_clock::time_point first;
        chrono::system_clock::time_point second;
        return strToTimePoint(time1,format1,first)&&strToTimePoint(time2,format2,second)&&first>=second;
    }

    stt::file::LogFile::~LogFile()
    {
        consumerGuard.store(false, std::memory_order_release);
        queueCV.notify_one();
        if(consumerThread.joinable())
            consumerThread.join();
    }
    bool stt::file::LogFile::openFile(const string &fileName,const string &timeFormat,const string &contentFormat)
    {
        this->timeFormat=timeFormat;
        this->contentFormat=contentFormat;
        return File::openFile(fileName,true,0,0,0664);
    }
    void stt::file::LogFile::writeLog(const string &data)
    {
        if(!consumerGuard.load(std::memory_order_acquire))
            return;
        //string content;
        //getTime(content,timeFormat);
        //content+=contentFormat+data;
        
        //{
        //    std::lock_guard<std::mutex> lock(queueMutex);
            if (logQueue.push(data))
            {
                if(!logWakePending.exchange(true,std::memory_order_acq_rel))
                    queueCV.notify_one();
            }
            else
                droppedLogs.fetch_add(1,std::memory_order_relaxed);
        //}
        //queueCV.notify_all();
        /*
        string content;
        getTime(content,timeFormat);
        content+=contentFormat+data;
        return appendLine(content);
        */
    }
    bool stt::file::LogFile::closeFile(const bool &del)
    {
        timeFormat.clear();
        contentFormat.clear();
        return File::closeFile(del);
    }
    bool stt::file::LogFile::clearLog()
    {
        return File::deleteAll();
    }
    bool stt::file::LogFile::deleteLogByTime(const string &date1,const string &date2)
    {
        if(!isOpen()||timeFormat.empty()||!lockMemory())
            return false;

        bool ok=true;
        int linePos=1;
        string data;
        const size_t timeSize=timeFormat.size();
        while(readLineC(data,linePos))
        {
            if(data.size()<timeSize)
            {
                ++linePos;
                continue;
            }
            const string time=data.substr(0,timeSize);
            const bool afterStart=date1=="1"||compareTime(time,date1,timeFormat,timeFormat);
            const bool beforeEnd=date2=="2"||!compareTime(time,date2,timeFormat,timeFormat);
            if(afterStart&&beforeEnd)
            {
                if(!deleteLineC(linePos))
                {
                    ok=false;
                    break;
                }
                continue;
            }
            ++linePos;
        }
        return unlockMemory(!ok)&&ok;
    }

    bool stt::data::CryptoUtil::encryptSymmetric(const unsigned char *before,const size_t &length,
                                                   const unsigned char *passwd,const unsigned char *iv,
                                                   unsigned char *after)
    {
        size_t outputLength=0;
        return encryptSymmetric(before,length,passwd,iv,after,outputLength);
    }

    bool stt::data::CryptoUtil::encryptSymmetric(const unsigned char *before,const size_t &length,
                                                   const unsigned char *passwd,const unsigned char *iv,
                                                   unsigned char *after,size_t &outputLength)
    {
        outputLength=0;
        if(before==nullptr||passwd==nullptr||iv==nullptr||after==nullptr||
           length>static_cast<size_t>(std::numeric_limits<int>::max()))
            return false;
        EVP_CIPHER_CTX *ctx=EVP_CIPHER_CTX_new();
        if(ctx==nullptr)
            return false;
        int written=0;
        int finalWritten=0;
        const bool ok=EVP_EncryptInit_ex(ctx,EVP_aes_256_cbc(),nullptr,passwd,iv)==1&&
            EVP_EncryptUpdate(ctx,after,&written,before,static_cast<int>(length))==1&&
            EVP_EncryptFinal_ex(ctx,after+written,&finalWritten)==1;
        EVP_CIPHER_CTX_free(ctx);
        if(!ok)
            return false;
        outputLength=static_cast<size_t>(written+finalWritten);
        return true;
    }

    bool stt::data::CryptoUtil::decryptSymmetric(const unsigned char *before,const size_t &length,
                                                   const unsigned char *passwd,const unsigned char *iv,
                                                   unsigned char *after)
    {
        size_t outputLength=0;
        return decryptSymmetric(before,length,passwd,iv,after,outputLength);
    }

    bool stt::data::CryptoUtil::decryptSymmetric(const unsigned char *before,const size_t &length,
                                                   const unsigned char *passwd,const unsigned char *iv,
                                                   unsigned char *after,size_t &outputLength)
    {
        outputLength=0;
        if(before==nullptr||passwd==nullptr||iv==nullptr||after==nullptr||
           length>static_cast<size_t>(std::numeric_limits<int>::max()))
            return false;
        EVP_CIPHER_CTX *ctx=EVP_CIPHER_CTX_new();
        if(ctx==nullptr)
            return false;
        int written=0;
        int finalWritten=0;
        const bool ok=EVP_DecryptInit_ex(ctx,EVP_aes_256_cbc(),nullptr,passwd,iv)==1&&
            EVP_DecryptUpdate(ctx,after,&written,before,static_cast<int>(length))==1&&
            EVP_DecryptFinal_ex(ctx,after+written,&finalWritten)==1;
        EVP_CIPHER_CTX_free(ctx);
        if(!ok)
            return false;
        outputLength=static_cast<size_t>(written+finalWritten);
        return true;
    }

string& stt::data::CryptoUtil::sha1(const string &ori_str,string &result)
{
	unsigned char hash[SHA_DIGEST_LENGTH];
	SHA1((unsigned char*)ori_str.c_str(),ori_str.length(),hash);
    char hash1[SHA_DIGEST_LENGTH];
    for(int i=0;i<SHA_DIGEST_LENGTH;i++)
    {
        hash1[i]=static_cast<char>(hash[i]);
    }
	result.assign(hash1,SHA_DIGEST_LENGTH);
	return result;
}
string& stt::data::CryptoUtil::sha11(const string &ori_str,string &result)
{
	unsigned char hash[SHA_DIGEST_LENGTH];
	SHA1((unsigned char*)ori_str.c_str(),ori_str.length(),hash);
	char buffer[2*SHA_DIGEST_LENGTH+1];
	buffer[2*SHA_DIGEST_LENGTH]=0;
	for(int i=0;i<SHA_DIGEST_LENGTH;i++)
	{
		snprintf(buffer+i*2,3,"%02x",hash[i]);
	}
	result.assign(buffer);
	return result;
}
string& stt::data::BitUtil::bitOutput(char input,string &result)
{
    result.clear();
    uint8_t value=static_cast<uint8_t>(input);
    for(int i=0;i<8;i++)
    {
        result+=(value&0x80U)?'1':'0';
        value=static_cast<uint8_t>(value<<1);
    }
    return result;
}
string& stt::data::BitUtil::bitOutput(const string &input,string &result)
{
    result.clear();
    for(const char byte:input)
    {
        uint8_t value=static_cast<uint8_t>(byte);
        for(int i=0;i<8;i++)
        {
            result+=(value&0x80U)?'1':'0';
            value=static_cast<uint8_t>(value<<1);
        }
    }
    return result;
}
char& stt::data::BitUtil::bitOutput_bit(char input,const int pos,char &result)
{
    if(pos<1||pos>8)
    {
        result='0';
        return result;
    }
    const uint8_t value=static_cast<uint8_t>(input);
    result=(value&(0x80U>>(pos-1)))?'1':'0';
    return result;
}
unsigned long& stt::data::BitUtil::bitStrToNumber(const string &input,unsigned long &result)
{
    result=0;
    if(input.empty()||input.size()>std::numeric_limits<unsigned long>::digits)
        return result;
    for(const char bit:input)
    {
        if(bit!='0'&&bit!='1')
        {
            result=0;
            return result;
        }
        result=(result<<1U)|static_cast<unsigned long>(bit-'0');
    }
    return result;
}
unsigned long& stt::data::BitUtil::bitToNumber(const string &input,unsigned long &result)
{
    if(input.size()>sizeof(unsigned long))
    {
        result=0;
        return result;
    }
    string bits;
    bitOutput(input,bits);
    return bitStrToNumber(bits,result);
}
char& stt::data::BitUtil::toBit(const string &input,char &result)
{
    result=0;
    if(input.empty()||input.size()>8||
       !all_of(input.begin(),input.end(),[](const char bit){return bit=='0'||bit=='1';}))
        return result;
    unsigned char value=0;
    for(const char bit:input)
        value=static_cast<unsigned char>((value<<1U)|static_cast<unsigned char>(bit-'0'));
    if(input.size()<8)
        value=static_cast<unsigned char>(value<<(8-input.size()));
    result=static_cast<char>(value);
    return result;
}
string& stt::data::BitUtil::toBit(const string &input,string &result)
{
    result.clear();
    if(input.empty()||input.size()%8!=0||
       !all_of(input.begin(),input.end(),[](const char bit){return bit=='0'||bit=='1';}))
        return result;
    result.reserve(input.size()/8);
    for(size_t offset=0;offset<input.size();offset+=8)
    {
        unsigned char value=0;
        for(size_t index=0;index<8;++index)
            value=static_cast<unsigned char>((value<<1U)|static_cast<unsigned char>(input[offset+index]-'0'));
        result.push_back(static_cast<char>(value));
    }
    return result;
}
    long stt::data::RandomUtil::getRandomNumber(const long &a,const long &b)
    {
        const long low=min(a,b);
        const long high=max(a,b);
        random_device rd;
        mt19937_64 generator(rd());
        uniform_int_distribution<long> distribution(low,high);
        return distribution(generator);
    }
    string& stt::data::RandomUtil::getRandomStr_base64(string &str,const int &length)
    {
        static constexpr string_view characters="0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz+/";
        str.clear();
        if(length<=0)
            return str;
        random_device rd;
        mt19937 generator(rd());
        uniform_int_distribution<size_t> distribution(0,characters.size()-1);
        str.reserve(static_cast<size_t>(length));
        for(int index=0;index<length;++index)
            str.push_back(characters[distribution(generator)]);
        return str;
    }
    string& stt::data::RandomUtil::generateMask_4(string &mask)
    {
        mask.assign(4,'\0');
        if(RAND_bytes(reinterpret_cast<unsigned char*>(mask.data()),4)!=1)
            mask.clear();
        return mask;
    }
unsigned long& stt::data::NetworkOrderUtil::htonl_ntohl_64(unsigned long &data)
{
    char swi;
    for(int i=0;i<4;i++)
    {
        memcpy(&swi,(char*)&data+i,1);
        memcpy((char*)&data+i,(char*)&data+7-i,1);
        memcpy((char*)&data+7-i,&swi,1);
    }
    return data;
}
    string& stt::data::PrecisionUtil::getPreciesFloat(const float &number,const int &bit,string &str)
    {
        stringstream ss;
        ss<<fixed<<setprecision(bit)<<number;
        str=ss.str();
        return str;
    }
    string& stt::data::PrecisionUtil::getPreciesDouble(const double &number,const int &bit,string &str)
    {
        stringstream ss;
        ss<<fixed<<setprecision(bit)<<number;
        str=ss.str();
        return str;
    }
    float& stt::data::PrecisionUtil::getPreciesFloat(float &number,const int &bit)
    {
        number=round(number*pow(10,bit))/pow(10,bit);
        return number;
    }
    double& stt::data::PrecisionUtil::getPreciesDouble(double &number,const int &bit)
    {
        number=round(number*pow(10,bit))/pow(10,bit);
        return number;
    }
    float& stt::data::PrecisionUtil::getValidFloat(float &number,const int &bit)
    {
        int bitt=0;

        while(((int)(number*pow(10,bitt))==0)&&bitt<=6)//如果是1以下的小数 确定位数
        {
            bitt++;
        }

        if(bitt==7)
            return number;
        if((int)number==0)
            bitt=bitt+bit-1;//1以下的
        else
            bitt=bitt+bit;//1以上的
        number=round(number*pow(10,bitt))/pow(10,bitt);
        return number;
    }

    size_t stt::data::HttpStringUtil::get_split_str(const string_view& ori_str,string_view &str,const string_view &a,const string_view &b,const size_t &pos)
    {
        size_t aa;
        size_t bb;
        if(a=="")
            aa=string::npos;
        else
            aa=ori_str.find(a,pos);
        if(b=="")
            bb=string::npos;
        else
            bb=ori_str.find(b,aa+1);
        if(aa==string::npos&&bb==string::npos)
        {
                str="";
                //return str;
                return bb;
        }
        else if(aa==string::npos&&bb!=string::npos)
        {
                str=ori_str.substr(0,bb);
                //return str;
                return bb;
        }
        else if(aa!=string::npos&&bb==string::npos)
        {
                aa+=a.length();
                str=ori_str.substr(aa);
                //return str;
                return bb;
        }
        aa+=a.length();
        str=ori_str.substr(aa,bb-aa);
        //return str;
        return bb;
    }
    string_view& stt::data::HttpStringUtil::get_value_header(const string_view& ori_str,
                                                                string_view &str,const string& name)
    {
        if(!getHttpHeaderValueCaseInsensitiveView(ori_str,name,str))
            str={};
        return str;
    }
    string_view& stt::data::HttpStringUtil::get_value_str(const string_view& ori_str,
                                                           string_view &str,const string& name)
    {
        str={};
        if(name.empty())
            return str;
        size_t queryStart=ori_str.find('?');
        queryStart=queryStart==string_view::npos?0:queryStart+1;
        const size_t fragment=ori_str.find('#',queryStart);
        const size_t queryEnd=fragment==string_view::npos?ori_str.size():fragment;
        size_t fieldStart=queryStart;
        while(fieldStart<=queryEnd)
        {
            size_t fieldEnd=ori_str.find('&',fieldStart);
            if(fieldEnd==string_view::npos||fieldEnd>queryEnd)
                fieldEnd=queryEnd;
            const string_view field=ori_str.substr(fieldStart,fieldEnd-fieldStart);
            const size_t equal=field.find('=');
            const string_view key=field.substr(0,equal);
            if(key==name)
            {
                str=equal==string_view::npos?string_view{}:field.substr(equal+1);
                return str;
            }
            if(fieldEnd==queryEnd)
                break;
            fieldStart=fieldEnd+1;
        }
        return str;
    }
    string_view& stt::data::HttpStringUtil::get_location_str(const string_view& ori_str,string_view &str)
{
	auto pos1=ori_str.find("://");
    if(pos1==string::npos)
        pos1=0;
    else
    {
        pos1=ori_str.find("/",pos1+3);
    }

    auto pos2=ori_str.find("?");

    str=ori_str.substr(pos1,pos2-pos1);
    return str;
    
}
    string_view& stt::data::HttpStringUtil::getLocPara(const string_view &url,string_view &locPara)
    {
        auto pos1=url.find("://");
        if(pos1==string::npos)
            pos1=0;
        else
        {
            pos1=url.find("/",pos1+3);
        }
        if(pos1==string::npos)
        {
            cerr<<"无效url"<<endl;
            locPara="";
            return locPara;
        }
        locPara=url.substr(pos1);
        return locPara;
    }
    string_view& stt::data::HttpStringUtil::getPara(const string_view &url,string_view &para)
    {
        para="";
        auto pos=url.find("?");
        if(pos==string::npos)
            return para;
        para=url.substr(pos);
        return para;
    }


    size_t stt::data::HttpStringUtil::get_split_str(const string_view& ori_str,string &str,const string_view &a,const string_view &b,const size_t &pos)
    {
        size_t aa;
        size_t bb;
        if(a=="")
            aa=string::npos;
        else
            aa=ori_str.find(a,pos);
        if(b=="")
            bb=string::npos;
        else
            bb=ori_str.find(b,aa+1);
        if(aa==string::npos&&bb==string::npos)
        {
                str="";
                //return str;
                return bb;
        }
        else if(aa==string::npos&&bb!=string::npos)
        {
                str=ori_str.substr(0,bb);
                //return str;
                return bb;
        }
        else if(aa!=string::npos&&bb==string::npos)
        {
                aa+=a.length();
                str=ori_str.substr(aa);
                //return str;
                return bb;
        }
        aa+=a.length();
        str=ori_str.substr(aa,bb-aa);
        //return str;
        return bb;
    }
    string& stt::data::HttpStringUtil::get_value_header(const string& ori_str,string &str,const string& name)
    {
        string_view value;
        get_value_header(string_view(ori_str),value,name);
        str.assign(value.data(),value.size());
        return str;
    }
    string& stt::data::HttpStringUtil::get_value_str(const string& ori_str,string &str,const string& name)
    {
        string_view value;
        get_value_str(string_view(ori_str),value,name);
        str.assign(value.data(),value.size());
        return str;
    }
    string& stt::data::HttpStringUtil::get_location_str(const string& ori_str,string &str)
{
	auto pos1=ori_str.find("://");
    if(pos1==string::npos)
        pos1=0;
    else
    {
        pos1=ori_str.find("/",pos1+3);
    }

    auto pos2=ori_str.find("?");

    str=ori_str.substr(pos1,pos2-pos1);
    return str;
    
}
    string& stt::data::HttpStringUtil::getLocPara(const string &url,string &locPara)
    {
        auto pos1=url.find("://");
        if(pos1==string::npos)
            pos1=0;
        else
        {
            pos1=url.find("/",pos1+3);
        }
        if(pos1==string::npos)
        {
            cerr<<"无效url"<<endl;
            locPara="";
            return locPara;
        }
        locPara=url.substr(pos1);
        return locPara;
    }
    string& stt::data::HttpStringUtil::getPara(const string &url,string &para)
    {
        para="";
        auto pos=url.find("?");
        if(pos==string::npos)
            return para;
        para=url.substr(pos);
        return para;
    }

    string& stt::data::HttpStringUtil::getIP(const string &url,string &IP)
    {
        HttpStringUtil::get_split_str(url,IP,"//",":");
        return IP;
    }
    int& stt::data::HttpStringUtil::getPort(const string &url,int &port)
    {
        auto pos=url.find(":");
        string pport;
        HttpStringUtil::get_split_str(url,pport,":","/",pos+1);
        return stt::data::NumberStringConvertUtil::toInt(pport,port);
    }
    string stt::data::HttpStringUtil::createHeader(const string& first,const string& second)
	{
		string cf=first+": "+second+"\r\n";
		return cf;
	}
string& stt::data::WebsocketStringUtil::transfer_websocket_key(string &str)
{
    str=str+"258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    string b="";
    CryptoUtil::sha1(str,b);
    str=EncodingUtil::base64_encode(b);
    return str;
}
    int& stt::data::NumberStringConvertUtil::toInt(const string_view& ori_str,int &result,const int &fallback)
    {
        const auto conversion=from_chars(ori_str.data(),ori_str.data()+ori_str.size(),result);
        if(ori_str.empty()||conversion.ec!=errc()||conversion.ptr!=ori_str.data()+ori_str.size())
            result=fallback;
        return result;
    }
    long& stt::data::NumberStringConvertUtil::toLong(const string_view& ori_str,long &result,const long &fallback)
    {
        const auto conversion=from_chars(ori_str.data(),ori_str.data()+ori_str.size(),result);
        if(ori_str.empty()||conversion.ec!=errc()||conversion.ptr!=ori_str.data()+ori_str.size())
            result=fallback;
        return result;
    }
    float& stt::data::NumberStringConvertUtil::toFloat(const string& ori_str,float &result,const float &fallback)
    {
        const auto conversion=from_chars(ori_str.data(),ori_str.data()+ori_str.size(),result,chars_format::general);
        if(ori_str.empty()||conversion.ec!=errc()||conversion.ptr!=ori_str.data()+ori_str.size()||!isfinite(result))
            result=fallback;
        return result;
    }
    double& stt::data::NumberStringConvertUtil::toDouble(const string& ori_str,double &result,const double &fallback)
    {
        const auto conversion=from_chars(ori_str.data(),ori_str.data()+ori_str.size(),result,chars_format::general);
        if(ori_str.empty()||conversion.ec!=errc()||conversion.ptr!=ori_str.data()+ori_str.size()||!isfinite(result))
            result=fallback;
        return result;
    }
    bool& stt::data::NumberStringConvertUtil::toBool(const string_view& ori_str,bool &result)
    {
        result=ori_str=="true"||ori_str=="TRUE"||ori_str=="True";
        return result;
    }


string& stt::data::NumberStringConvertUtil::strto16(const string &ori_str,string &result)//字符串转化为16进制字符串
{
    static constexpr char hex[]="0123456789abcdef";
    result.resize(ori_str.size()*2);
    for(size_t i=0;i<ori_str.size();++i)
    {
        const unsigned char value=static_cast<unsigned char>(ori_str[i]);
        result[i*2]=hex[value>>4];
        result[i*2+1]=hex[value&0x0f];
    }
    return result;
}
    int& stt::data::NumberStringConvertUtil::str16toInt(const string_view& ori_str,int &result,const int &fallback)
    {
        const auto conversion=from_chars(ori_str.data(),ori_str.data()+ori_str.size(),result,16);
        if(ori_str.empty()||conversion.ec!=errc()||conversion.ptr!=ori_str.data()+ori_str.size())
            result=fallback;
        return result;
    }
    // Base64 编码；输入可以包含任意二进制字节。
    std::string stt::data::EncodingUtil::base64_encode(const std::string &input)
    {
        if(input.empty())
            return {};
        const size_t outputSize=4*((input.size()+2)/3);
        string output(outputSize,'\0');
        const int written=EVP_EncodeBlock(reinterpret_cast<unsigned char*>(output.data()),
            reinterpret_cast<const unsigned char*>(input.data()),static_cast<int>(input.size()));
        if(written<0)
            return {};
        output.resize(static_cast<size_t>(written));
        return output;
    }

    // 严格 Base64 解码；格式非法时返回空字符串。
    std::string stt::data::EncodingUtil::base64_decode(const std::string &input)
    {
        if(input.empty())
            return {};
        if(input.size()%4!=0||input.size()>static_cast<size_t>(std::numeric_limits<int>::max()))
            return {};
        size_t padding=0;
        if(input.back()=='=') ++padding;
        if(input.size()>1&&input[input.size()-2]=='=') ++padding;
        for(size_t index=0;index<input.size();++index)
        {
            const unsigned char ch=static_cast<unsigned char>(input[index]);
            const bool alphabet=(ch>='A'&&ch<='Z')||(ch>='a'&&ch<='z')||
                                (ch>='0'&&ch<='9')||ch=='+'||ch=='/';
            if(alphabet)
            {
                if(index>=input.size()-padding)
                    return {};
            }
            else if(ch=='=')
            {
                if(index<input.size()-padding)
                    return {};
            }
            else
                return {};
        }
        string output((input.size()/4)*3,'\0');
        const int decoded=EVP_DecodeBlock(reinterpret_cast<unsigned char*>(output.data()),
            reinterpret_cast<const unsigned char*>(input.data()),static_cast<int>(input.size()));
        if(decoded<0||static_cast<size_t>(decoded)<padding)
            return {};
        output.resize(static_cast<size_t>(decoded)-padding);
        return output;
    }

    string& stt::data::EncodingUtil::maskCalculate(string &data,const string &mask)
    {
        if(mask.size()<4)
            return data;
        for(size_t index=0;index<data.size();++index)
            data[index]=static_cast<char>(static_cast<unsigned char>(data[index])^
                                          static_cast<unsigned char>(mask[index%4]));
        return data;
    }
string& stt::data::EncodingUtil::transfer_websocket_key(string &str)
{
    str=str+"258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    string b="";
    CryptoUtil::sha1(str,b);
    str=base64_encode(b);
    return str;
}
string& stt::data::EncodingUtil::generateMask_4(string &mask)
{
    mask.assign(4,'\0');
    if(RAND_bytes(reinterpret_cast<unsigned char*>(mask.data()),4)!=1)
        mask.clear();
    return mask;
}
    int stt::data::JsonHelper::getValue(const string &oriStr,string &result,const string &type,const string &name,const int &num)
    {
        Json::Value root;
        Json::CharReaderBuilder reader;
        string errs;
        istringstream s(oriStr);
        bool is;

        if(type=="arrayvalue")//数组中的数据
        {
            is=Json::parseFromStream(reader,s,&root,&errs);
            if(!is||!root.isArray()||root.isNull())
            {   
                return -1;
            }
            Json::Value next=root[num];
            if(next.isNull())
            {
                return -1;
            }

            if(next.isObject()||next.isArray())//嵌套了数组或者json对象
            {
                Json::StreamWriterBuilder writerBuilder;
                result = Json::writeString(writerBuilder, next);
                return 1;
            }
            else//普通数据
            {
                result=root[num].asString();
                return 0;
            }
        }
        
        else if(type=="value")//json中的普通数据
        {
            is=Json::parseFromStream(reader,s,&root,&errs);
            if(!is||!root.isObject()||root.isNull())
            {   
                return -1;
            }
            Json::Value next=root[name];
            if(next.isNull())
                return -1;
            if(next.isObject()||next.isArray())//对象
            {
                Json::StreamWriterBuilder writerBuilder;
                result = Json::writeString(writerBuilder, next);
                return 1;
            }
            else//普通数据
            {
                result=root[name].asString();
                return 0;
            }
            
        }    
        else
        {   
            return -1;
        }
    }
    string stt::data::JsonHelper::toString(const Json::Value &val)
    {
        Json::StreamWriterBuilder writer;
        writer["indentation"]="";
        return Json::writeString(writer,val);
    }
    Json::Value stt::data::JsonHelper::toJsonArray(const string & str)
    {
        Json::Value root;
        Json::CharReaderBuilder reader;
        string errs;
        istringstream s(str);
        
        if(!Json::parseFromStream(reader,s,&root,&errs))
        {
            cerr<<"json转换错误 : "<<errs<<endl;
        }
        return root;
    }
    string stt::data::JsonHelper::jsonAdd(const string &a,const string &b)
    {
        Json::CharReaderBuilder reader;
        Json::Value first;
        Json::Value second;
        string errors;
        istringstream firstStream(a);
        if(!Json::parseFromStream(reader,firstStream,&first,&errors))
            return {};
        errors.clear();
        istringstream secondStream(b);
        if(!Json::parseFromStream(reader,secondStream,&second,&errors))
            return {};

        if(first.isObject()&&second.isObject())
        {
            for(const string &name:second.getMemberNames())
                first[name]=second[name];
        }
        else if(first.isArray()&&second.isArray())
        {
            for(const Json::Value &value:second)
                first.append(value);
        }
        else
            return {};
        return toString(first);
    }
    string& stt::data::JsonHelper::jsonFormatify(const string &a,string &b)
    {
        Json::CharReaderBuilder reader;
        Json::Value root;
        string errs;
        istringstream s(a);
        if(!Json::parseFromStream(reader,s,&root,&errs))
        {
            cerr<<"json转换错误 : "<<errs<<endl;
            b="";
            return b;
        }
        Json::StreamWriterBuilder writer;
        writer["indentation"]="";
        b=Json::writeString(writer,root);
        return b;
    }
    string& stt::data::JsonHelper::jsonToUTF8(const string &input,string &output)
    {
        output.clear();
        regex unicode_regex("\\\\u([0-9a-fA-F]{4})");
    
        // 使用sregex_iterator遍历匹配的结果
        sregex_iterator begin(input.begin(), input.end(), unicode_regex);
        sregex_iterator end;
    
        string::size_type last_pos = 0;
        for (sregex_iterator i = begin; i != end; ++i) 
        {
            smatch match = *i;
            output += input.substr(last_pos, match.position() - last_pos);  // 添加前面未匹配的部分

            // 提取匹配到的十六进制 Unicode 码
            unsigned int codepoint;
            stringstream ss;
            ss << hex << match.str(1);
            ss >> codepoint;

            // 将 Unicode 码转换为 UTF-8 字符
            char utf8_char[5] = {0};
            if (codepoint <= 0x7F) 
            {
                utf8_char[0] = codepoint;
            } 
            else if (codepoint <= 0x7FF) 
            {
                utf8_char[0] = 0xC0 | ((codepoint >> 6) & 0x1F);
                utf8_char[1] = 0x80 | (codepoint & 0x3F);
            } 
            else if (codepoint <= 0xFFFF) 
            {
                utf8_char[0] = 0xE0 | ((codepoint >> 12) & 0x0F);
                utf8_char[1] = 0x80 | ((codepoint >> 6) & 0x3F);
                utf8_char[2] = 0x80 | (codepoint & 0x3F);
            }

            output += std::string(utf8_char);  // 添加转换后的 UTF-8 字符
            last_pos = match.position() + match.length();
        }
    
        // 添加最后一部分未匹配的字符串
        output += input.substr(last_pos);
        return output;
    
    }

    
    void stt::network::TcpFDHandler::blockSet(const int &sec)
    {
        int flags=fcntl(fd,F_GETFL,0);//获取当前标志
        if(flags>=0)
            (void)fcntl(fd,F_SETFL,flags&~O_NONBLOCK);
        flag1=false;
        if(sec!=-1)
        {
            struct timeval tv;
            tv.tv_sec = sec;  // 设置接收超时时间为 5 秒
            tv.tv_usec = 0;

            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        }
        this->sec=sec;
    }
    void stt::network::UdpFDHandler::blockSet(const int &sec)
    {
        int flags=fcntl(fd,F_GETFL,0);//获取当前标志
        if(flags>=0)
            (void)fcntl(fd,F_SETFL,flags&~O_NONBLOCK);
        flag1=false;
        if(sec!=-1)
        {
            struct timeval tv;
            tv.tv_sec = sec;  // 设置接收超时时间为 5 秒
            tv.tv_usec = 0;

            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        }
        this->sec=sec;
    }
    void stt::network::TcpFDHandler::unblockSet()
    {
        int flags=fcntl(fd,F_GETFL,0);//获取当前标志
        if(flags>=0&&fcntl(fd,F_SETFL,flags|O_NONBLOCK)==0)
            flag1=true;
    }
    void stt::network::UdpFDHandler::unblockSet()
    {
        int flags=fcntl(fd,F_GETFL,0);//获取当前标志
        if(flags>=0&&fcntl(fd,F_SETFL,flags|O_NONBLOCK)==0)
            flag1=true;
    }
    bool stt::network::TcpFDHandler::multiUseSet()
    {
        int opt;
        if(!flag2)//设置为multiuse
            opt=1;
        else
            opt=0;
        if(setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt))<0||
           setsockopt(fd,SOL_SOCKET,SO_REUSEPORT,&opt,sizeof(opt))<0)
        {
            cerr<<"set multi failed"<<endl;
            perror("setsockopt");
            return false;
        }
        flag2=!flag2;
        return true;
    }
    bool stt::network::UdpFDHandler::multiUseSet()
    {
        int opt;
        if(!flag2)//设置为multiuse
            opt=1;
        else
            opt=0;
        if(setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt))<0||
           setsockopt(fd,SOL_SOCKET,SO_REUSEPORT,&opt,sizeof(opt))<0)
        {
            cerr<<"set multi failed"<<endl;
            perror("setsockopt");
            return false;
        }
        flag2=!flag2;
        return true;
    }
    void stt::network::TcpFDHandler::setFD(const int &fd,SSL *ssl,const bool &flag1,const bool &flag2,const int &sec)
    {
        (void)sec;
        this->fd=fd;
        this->flag1=flag1;
        this->flag2=flag2;
        this->ssl=ssl;
    }
    void stt::network::UdpFDHandler::setFD(const int &fd,const bool &flag1,const int &sec,const bool &flag2)
    {
        this->fd=fd;
        this->flag1=flag1;
        this->flag2=flag2;
        if(flag1==true)
            unblockSet();
        else 
            blockSet(sec);
        if(flag2==true)
            multiUseSet();
    }
    void stt::network::TcpFDHandler::close(const bool &cle)
    {
        flag1=false;
        flag2=false;
        if(cle&&queuedCloseFunction)
        {
            queuedCloseFunction();
        }
        else if(cle)
        {
            if(ssl!=nullptr)
            {
                SSL_shutdown(ssl);
                SSL_free(ssl);
            }
            ::close(fd);
        }
        ssl=nullptr;
        fd=-1;
        queuedSendFunction={};
        queuedCloseFunction={};
    }
    void stt::network::UdpFDHandler::close(const bool &cle)
    {
        flag1=false;
        flag2=false;
        sec=-1;
        if(cle)
        {
            ::close(fd);
        }
        fd=-1;
    }
    bool stt::network::TcpClient::createFD()
    {
        //准备socket
        fd=socket(AF_INET,SOCK_STREAM|SOCK_CLOEXEC,0);
        if(fd<0)
        {
            perror("socket");
            return false;
        }
        flag=false;
        serverIP="";
        serverPort=-1;
        flag1=false;
        flag2=false;
        if(TLS)
        {
            this->TLS=true;
            if(!initCTX(ca,cert,key,passwd))
            {
                this->TLS=false;
                ::close(fd);
                fd=-1;
                return false;
            }
            ssl=SSL_new(ctx);
            if(ssl==NULL)
            {
                perror("new ssl wrong");
                ERR_print_errors_fp(stderr);
                this->TLS=false;
                if(ctx!=nullptr)
                {
                    SSL_CTX_free(ctx);
                    ctx=nullptr;
                }
                ::close(fd);
                fd=-1;
                return false;
            }
        }
        else
        {
            this->TLS=false;
            ssl=nullptr;
        }
        return true;
    }
    void stt::network::TcpClient::closeAndUnCreate()
    {
        if(ssl!=nullptr)
        {
            SSL_shutdown(ssl);
            SSL_free(ssl);
            ssl=nullptr;
        }
        if(fd>=0)
        {
            ::close(fd);
            fd=-1;
        }
        if(ctx!=nullptr)
        {
            SSL_CTX_free(ctx);
            ctx=nullptr;
        }  
    }
    bool stt::network::TcpClient::initCTX(const char *ca,const char *cert,const char *key,const char *passwd)
    {
        // 初始化
        SSLeay_add_ssl_algorithms();
        OpenSSL_add_all_algorithms();
        SSL_load_error_strings();
        //SSL_library_init();

        // 我们使用SSL V3,V2
        #if OPENSSL_VERSION_NUMBER < 0x10100000L
        if((ctx = SSL_CTX_new(SSLv23_client_method()))==NULL)
            return false;
        #else
        if((ctx = SSL_CTX_new(TLS_client_method())) == NULL)
            return false;
        #endif
        SSL_CTX_set_min_proto_version(ctx,TLS1_2_VERSION);
        //校验对方证书，使用系统自带的权威的ca证书
        SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);
        const bool hasExplicitCA=ca!=nullptr&&ca[0]!='\0';
        if((hasExplicitCA&&SSL_CTX_load_verify_locations(ctx,ca,nullptr)<=0)||
           (!hasExplicitCA&&SSL_CTX_set_default_verify_paths(ctx)<=0))
        {
            cerr<<"ca false"<<endl;
            SSL_CTX_free(ctx);
            ctx=nullptr;
            return false;
        }
        //可能的话加载自己的证书和私钥
        if(cert!=nullptr&&cert[0]!='\0'&&key!=nullptr&&key[0]!='\0')
        {
            if(SSL_CTX_use_certificate_chain_file(ctx, cert) <=0)
            {
                cerr<<"加载证书失败"<<endl;
                SSL_CTX_free(ctx);
                ctx=nullptr;
                return false;
            }
            SSL_CTX_set_default_passwd_cb_userdata(ctx, (void*)passwd);
            if(SSL_CTX_use_PrivateKey_file(ctx, key, SSL_FILETYPE_PEM) <=0)
            {
                cerr<<"密码失败"<<endl;
                SSL_CTX_free(ctx);
                ctx=nullptr;
                return false;
            }
            //判断私钥是否正确
            if(!SSL_CTX_check_private_key(ctx))
            {
                cerr<<"密码错误"<<endl;
                SSL_CTX_free(ctx);
                ctx=nullptr;
                return false;
            }
        }
        return true;
    }
    stt::network::TcpClient::TcpClient(const bool &TLS,const char *ca,const char *cert,const char *key,const char *passwd)
    {
        this->ca=ca;
        this->cert=cert;
        this->key=key;
        this->passwd=passwd;
        this->TLS=TLS;
        createFD();
    }
    void stt::network::TcpClient::resetCTX(const bool &TLS,const char *ca,const char *cert,const char *key,const char *passwd)
    {
        this->ca=ca;
        this->cert=cert;
        this->key=key;
        this->passwd=passwd;
        this->TLS=TLS;
    }
    bool stt::network::TcpClient::connect(const string &ip,const int &port)
    {
        if(isConnect())
        {
          if(!close())
            return false;
        }
        if(fd<0||port<0||port>65535)
            return false;
        addrinfo hints{};
        hints.ai_family=AF_INET;
        hints.ai_socktype=SOCK_STREAM;
        hints.ai_protocol=IPPROTO_TCP;
        addrinfo *addresses=nullptr;
        const string service=to_string(port);
        if(getaddrinfo(ip.c_str(),service.c_str(),&hints,&addresses)!=0)
        {
            close();
            return false;
        }
        bool connected=false;
        for(addrinfo *address=addresses;address!=nullptr;address=address->ai_next)
        {
            if(::connect(fd,address->ai_addr,address->ai_addrlen)==0)
            {
                connected=true;
                break;
            }
        }
        freeaddrinfo(addresses);
        if(!connected)
        {
            close();
            return false;
        }
        if(TLS)//套上TLS加密
        {
            if(ssl==nullptr)
            {
                close();
                return false;
            }
            if(SSL_set_tlsext_host_name(ssl,ip.c_str())!=1||SSL_set1_host(ssl,ip.c_str())!=1)
            {
                close();
                return false;
            }
            SSL_set_connect_state(ssl);
            SSL_set_fd(ssl,fd);
            int ret=SSL_connect(ssl);
            if(ret != 1)
            {
                close();
                return false;
            }
        }
        serverIP=ip;
        serverPort=port;
        flag=true;
        return true;
    }
    bool stt::network::TcpClient::close()
    {
        closeAndUnCreate();
        return createFD();
    }
    int stt::network::TcpFDHandler::sendData(const string &data,const bool &block)
    {
        if(queuedSendFunction)
            return queuedSendFunction(data);
        return sendData(data.data(),data.size(),block);
    }
    
    int stt::network::UdpFDHandler::sendData(const string &data,const string &ip,const int &port,const bool &block)
    {
        return sendData(data.data(),data.size(),ip,port,block);
    }
    
    int stt::network::TcpFDHandler::sendData(const char *data,const uint64_t &length,const bool &block)
    {
        if(!isConnect())
            return -99;
        if(data==nullptr||length>static_cast<uint64_t>(std::numeric_limits<int>::max()))
            return -98;
        if(queuedSendFunction)
            return queuedSendFunction(std::string(data,static_cast<size_t>(length)));

        size_t totalSize=0;
        do
        {
            int result=0;
            int waitEvents=POLLOUT;
            if(ssl==nullptr)
            {
                result=static_cast<int>(::send(fd,data+totalSize,length-totalSize,MSG_NOSIGNAL));
                if(result<0)
                {
                    if(errno==EINTR)
                        continue;
                    if(errno!=EAGAIN&&errno!=EWOULDBLOCK)
                        return totalSize==0?result:static_cast<int>(totalSize);
                    result=-1;
                }
            }
            else
            {
                result=SSL_write(ssl,data+totalSize,static_cast<int>(length-totalSize));
                if(result<=0)
                {
                    const int sslError=SSL_get_error(ssl,result);
                    if(sslError==SSL_ERROR_WANT_READ)
                        waitEvents=POLLIN;
                    else if(sslError!=SSL_ERROR_WANT_WRITE)
                        return totalSize==0?result:static_cast<int>(totalSize);
                    result=-1;
                }
            }

            if(result>0)
            {
                totalSize+=static_cast<size_t>(result);
                if(!block)
                    break;
                continue;
            }
            if(!block)
                return totalSize==0?-100:static_cast<int>(totalSize);

            pollfd writable{fd,static_cast<short>(waitEvents),0};
            int pollResult;
            do { pollResult=::poll(&writable,1,-1); }
            while(pollResult<0&&errno==EINTR);
            if(pollResult<=0)
                return totalSize==0?-1:static_cast<int>(totalSize);
        }
        while(totalSize<length);

        return static_cast<int>(totalSize);
    }
    
    int stt::network::UdpFDHandler::sendData(const char *data,const uint64_t &length,const string &ip,const int &port,const bool &block)
    {
        if(fd==-1)
            return -99;
        if(data==nullptr||length>static_cast<uint64_t>(std::numeric_limits<int>::max())||port<0||port>65535)
            return -98;

        addrinfo hints{};
        hints.ai_family=AF_INET;
        hints.ai_socktype=SOCK_DGRAM;
        addrinfo *addresses=nullptr;
        const string service=to_string(port);
        if(getaddrinfo(ip.c_str(),service.c_str(),&hints,&addresses)!=0)
            return -98;

        int result;
        do
        {
            result=static_cast<int>(::sendto(fd,data,length,MSG_NOSIGNAL,addresses->ai_addr,addresses->ai_addrlen));
            if(result>=0||(errno!=EAGAIN&&errno!=EWOULDBLOCK)||!block)
                break;
            pollfd writable{fd,POLLOUT,0};
            do { result=::poll(&writable,1,-1); }
            while(result<0&&errno==EINTR);
            if(result<=0)
                break;
        }
        while(true);
        freeaddrinfo(addresses);
        if(result<0&&(errno==EAGAIN||errno==EWOULDBLOCK))
            return -100;
        return result;
    }
    
    int stt::network::TcpFDHandler::recvDataByLength(string &data,const uint64_t &length,const int &sec)
    {
        if(!isConnect())
            return -99;
        if(flag1&&sec!=-1)//非阻塞模式且sec设置为非无限等待
        {
            DateTime timer;
            Duration dt{0,0,0,sec,0};
            unique_ptr<char[]> buffer(new char[length]);
            data.clear();
            uint64_t size=length;
            int64_t recvSize;
            while(size>0)
            {
                timer.startTiming();
                if(ssl==nullptr)
                    recvSize=::recv(fd,buffer.get(),size,0);
                else
                    recvSize=SSL_read(ssl,buffer.get(),size);
                if(recvSize<=0)
                {
                    if(recvSize<0&&(errno==EAGAIN||errno==EWOULDBLOCK))
                    {
                        //cout<<"ok"<<endl;
                        if(dt>=timer.checkTime())
                            continue;
                        else
                        {
                            return -100;
                        }
                    }
                    return recvSize;
                }
                timer.endTiming(); 
                data+=string(buffer.get(),recvSize);
                size-=recvSize;
            }
        }
        else//阻塞模式
        {
            int sec_backup=this->sec;
            blockSet(sec);
            unique_ptr<char[]> buffer(new char[length]);
            data.clear();
            uint64_t size=length;
            int64_t recvSize;
            while(size>0)
            {
                if(ssl==nullptr)
                    recvSize=::recv(fd,buffer.get(),size,0);
                else
                    recvSize=SSL_read(ssl,buffer.get(),size);
                if(recvSize<=0)
                {
                    blockSet(sec_backup);
                    if(recvSize<0&&(errno==EAGAIN||errno==EWOULDBLOCK))
                        return -100;
                    return recvSize;
                }
                data+=string(buffer.get(),recvSize);
                size-=recvSize;
            }
        }

        
        return length;
    }
    /*
    int UdpFDHandler::recvDataByLength(string &data,const uint64_t &length,string &ip,int &port,const int &sec)
    {
        if(fd==-1)
            return -99;
        int sec_back_up;
        if(!flag1)//如果是阻塞模式要设置新的超时时间
        {
            sec_back_up=this->sec;
            blockSet(sec);
        }
        //预备接收对端的信息
        struct sockaddr_in chenfan;
	    socklen_t tanziliang=sizeof(chenfan);
        //接收
        char buffer[length];
        data.clear();
        uint64_t size=length;
        int64_t recvSize;
        if(flag1)//非阻塞模式
        {
            DateTime timer;
            Duration dt{0,0,0,sec,0};
            while(size>0)
            {
                timer.startTiming();
                recvSize=::recvfrom(fd,buffer,size,0,(struct sockaddr*)&chenfan,&tanziliang);
                if(recvSize<0)
                {
                    if(recvSize<0&&(errno==EAGAIN||errno==EWOULDBLOCK))
                    {
                        //cout<<"ok"<<endl;
                        if(dt<timer.checkTime())
                            continue;
                        else
                        {
                            port=chenfan.sin_port;
	                        ip.assign(inet_ntoa(chenfan.sin_addr));
                            if(!flag1)
                                blockSet(sec_back_up);
                            return -100;
                        }
                    }
                    perror("recv()");
                    break;
                }
                timer.endTiming(); 
                data+=string(buffer,recvSize);
                size-=recvSize;
            }
        }
        else//阻塞模式
        {
            while(size>0)
            {
                recvSize=::recvfrom(fd,buffer,size,0,(struct sockaddr*)&chenfan,&tanziliang);
                if(recvSize<0)
                {
                    perror("recv()");
                    break;
                }
                data+=string(buffer,recvSize);
                size-=recvSize;
            }
        }
        //ip和port
        port=chenfan.sin_port;
	    ip.assign(inet_ntoa(chenfan.sin_addr));
        //如果是阻塞模式 接收完毕还原超时时间
        if(!flag1)
            blockSet(sec_back_up);
        if(recvSize<0)
            return recvSize;
        return length;
    }
    */
    int stt::network::TcpFDHandler::recvDataByLength(char *data,const uint64_t &length,const int &sec)
    {
        if(!isConnect())
            return -99;
        
        if(flag1&&sec!=-1)//非阻塞模式
        {
            DateTime timer;
            Duration dt{0,0,0,sec,0};
            memset(data,0,length);
            uint64_t size=length;
            int64_t recvSize=0;
            while(size>0)
            {
                timer.startTiming();
                if(ssl==nullptr)
                    recvSize=::recv(fd,data+(length-size),size,0);
                else
                    recvSize=SSL_read(ssl,data+(length-size),size);
                if(recvSize<=0)
                {
                    if(recvSize<0&&(errno==EAGAIN||errno==EWOULDBLOCK))
                    {
                        if(dt>=timer.checkTime())
                            continue;
                        else
                        {
                            return -100;
                        }
                    }

                    return recvSize;
                }
                timer.endTiming();

                size-=recvSize;
            }
        }
        else
        {
            int sec_backup=this->sec;
            blockSet(sec);
            memset(data,0,length);
            uint64_t size=length;
            int64_t recvSize=0;
            while(size>0)
            {
                if(ssl==nullptr)
                    recvSize=::recv(fd,data+(length-size),size,0);
                else
                    recvSize=SSL_read(ssl,data+(length-size),size);
                if(recvSize<=0)
                {
                    blockSet(sec_backup);
                    if(recvSize<0&&(errno==EAGAIN||errno==EWOULDBLOCK))
                    {
                        return -100;
                    }

                    return recvSize;
                }

                size-=recvSize;
            }
        }

        return length;
    }
    int stt::network::TcpFDHandler::recvData(string &data,const uint64_t &length)
    {
        if(!isConnect())
            return -99;
        unique_ptr<char[]> buffer(new char[length]);
        int size;
        if(ssl==nullptr)
            size=::recv(fd,buffer.get(),length,0);
        else
            size=SSL_read(ssl,buffer.get(),length);
        if(size>0)
            data=string(buffer.get(),size);
        else if(ssl!=nullptr)
        {
            const int sslError=SSL_get_error(ssl,size);
            if(sslError==SSL_ERROR_WANT_READ||sslError==SSL_ERROR_WANT_WRITE)
                return -100;
            if(sslError==SSL_ERROR_ZERO_RETURN)
                return 0;
        }
        else
        {
            if(flag1==true&&size<0&&(errno==EAGAIN||errno==EWOULDBLOCK))
                return -100;
            //perror("recv()");
        }
        return size;
    }
    int stt::network::UdpFDHandler::recvData(string &data,const uint64_t &length,string &ip,int &port)
    {
        if(fd==-1)
            return -99;
        unique_ptr<char[]> buffer(new char[length]);
        int size;
        struct sockaddr_in chenfan;
	    socklen_t tanziliang=sizeof(chenfan);
        size=::recvfrom(fd,buffer.get(),length,0,(struct sockaddr*)&chenfan,&tanziliang);
        if(size>=0)
        {
            data.assign(buffer.get(),static_cast<size_t>(size));
            port=ntohs(chenfan.sin_port);
	        ip.assign(inet_ntoa(chenfan.sin_addr));
        }
        else
        {
            if((errno==EAGAIN||errno==EWOULDBLOCK))
                return -100;
            //perror("recv()");
        }
        return size;
    }
    int stt::network::TcpFDHandler::recvData(char *data,const uint64_t &length)
    {
        if(!isConnect())
            return -99;
        int size;
        if(ssl==nullptr)
            size=::recv(fd,data,length,0);
        else
            size=SSL_read(ssl,data,length);
        if(size<=0&&ssl!=nullptr)
        {
            const int sslError=SSL_get_error(ssl,size);
            if(sslError==SSL_ERROR_WANT_READ||sslError==SSL_ERROR_WANT_WRITE)
                return -100;
            if(sslError==SSL_ERROR_ZERO_RETURN)
                return 0;
            return -1;
        }
        if(size<0)
        {
            if(flag1==true&&(errno==EAGAIN||errno==EWOULDBLOCK))
                return -100;
            //perror("recv()");
        }
        return size;
    }
    int stt::network::UdpFDHandler::recvData(char *data,const uint64_t &length,string &ip,int &port)
    {
        if(fd==-1)
            return -99;
        int size;
        struct sockaddr_in chenfan;
	    socklen_t tanziliang=sizeof(chenfan);
        size=::recvfrom(fd,data,length,0,(struct sockaddr*)&chenfan,&tanziliang);
        if(size<0)
        {
            if((errno==EAGAIN||errno==EWOULDBLOCK))
                return -100;
            //perror("recv()");
            return size;
        }
        port=ntohs(chenfan.sin_port);
	    ip.assign(inet_ntoa(chenfan.sin_addr));
        return size;
    }
    stt::network::UdpClient::UdpClient(const bool &flag1,const int &sec)
    {
        fd=socket(AF_INET,SOCK_DGRAM,0);
        if(fd<0)
        {
            cerr<<" udp fd failed"<<endl;;
            fd=-1;
        }
        else
        {
            if(flag1)
                unblockSet();
            else
                blockSet(sec);
            this->flag1=flag1;
            this->sec=sec;
            this->flag2=false;
        }
    }
    bool stt::network::UdpClient::createFD(const bool &flag1,const int &sec)
    {
        if(fd!=-1)
            close();
        fd=socket(AF_INET,SOCK_DGRAM,0);
        if(fd<0)
        {
            cerr<<" udp fd failed"<<endl;;
            fd=-1;
            return false;
        }
        else
        {
            if(flag1)
                unblockSet();
            else
                blockSet(sec);
            this->flag1=flag1;
            this->sec=sec;
            this->flag2=false;
        }
        return true;
    }
    stt::network::UdpServer::UdpServer(const int &port,const bool &flag1,const int &sec,const bool &flag2)
    {
        fd=socket(AF_INET,SOCK_DGRAM,0);
        if(fd<0)
        {
            cerr<<" udp fd failed"<<endl;;
            fd=-1;
        }
        else
        {
            if(flag1)
                unblockSet();
            else
                blockSet(sec);
            if(flag2)
                multiUseSet();
            this->flag1=flag1;
            this->sec=sec;
            this->flag2=flag2;
            struct sockaddr_in k;
	        memset(&k,0,sizeof(k));
	        k.sin_family=AF_INET;
	        k.sin_addr.s_addr=htonl(INADDR_ANY);
	        k.sin_port=htons(port);
	        if(::bind(fd,(struct sockaddr*)&k,sizeof(k))!=0)//成
	        {
		        perror("bind");
		        close(fd);
	        }
        }
    }
    bool stt::network::UdpServer::createFD(const int &port,const bool &flag1,const int &sec,const bool &flag2)
    {
        if(fd!=-1)
            close();
        fd=socket(AF_INET,SOCK_DGRAM,0);
        if(fd<0)
        {
            cerr<<" udp fd failed"<<endl;;
            fd=-1;
            return false;
        }
        else
        {
            if(flag1)
                unblockSet();
            else
                blockSet(sec);
            if(flag2)
                multiUseSet();
            this->flag1=flag1;
            this->sec=sec;
            this->flag2=flag2;
            struct sockaddr_in k;
	        memset(&k,0,sizeof(k));
	        k.sin_family=AF_INET;
	        k.sin_addr.s_addr=htonl(INADDR_ANY);
	        k.sin_port=htons(port);
	        if(::bind(fd,(struct sockaddr*)&k,sizeof(k))!=0)//成
	        {
		        perror("bind");
		        close(fd);
                return false;
	        }
        }
        return true;
    }
    
    
    bool stt::network::HttpClient::getRequest(const string &url,const string &header,const string &header1,const int &sec)
    {
        this->flag=false;
        this->header="";
        this->body="";
        string ip;
        int port;
        string locPara;
        HttpStringUtil::getIP(url,ip);
        HttpStringUtil::getPort(url,port);
        HttpStringUtil::getLocPara(url,locPara);
        blockSet(sec);
        if(!isConnect()||getServerIP()!=ip||getServerPort()!=port)//没有连接或者服务器变更需要重新连接
        {
            if(isConnect())//如果是变更服务器 需要先关闭原有的连接
            {
                if(!close())
                {
                    cerr<<"http无法关闭前一个连接"<<endl;
                    return false;
                }
            }
            blockSet(sec);
            if(!connect(ip,port))
            {
                cerr<<"http无法连接到服务器"<<endl;
                return false;
            }
        }

        //连接到服务器完成，后面进行发送请求报文操作

        //拼接报文
        string ss;
        ss="GET "+locPara+" HTTP/1.1\r\n"\
            +"Host: "+ip+":"+to_string(port)+"\r\n"\
            +header1+"\r\n"\
            +header+"\r\n";

        //发送
        const int sentBytes=sendData(ss);
        if(sentBytes<0||static_cast<size_t>(sentBytes)!=ss.length())
            return false;
        //接收
        string totalRecv="";
        string recv;
        while(totalRecv.find("\r\n\r\n")==string::npos)
        {
            if(recvData(recv,1024)<=0)
            {
                flag=false;
                this->header="";
                body="";
                return true;
            }
            totalRecv+=recv;
        }
        auto pos=totalRecv.find("\r\n\r\n");
        recv.clear();
        if(totalRecv.find("Transfer-Encoding: chunked")!=string::npos)//chunked先全部接受完
        {
            while(totalRecv.find("0\r\n\r\n",pos)==string::npos)
            {
                if(recvData(recv,1024)<=0)
                {
                    flag=false;
                    body="";
                    totalRecv.erase(pos);
                    this->header=totalRecv;
                    return true;
                }
                totalRecv+=recv;
            }
            //cout<<totalRecv<<endl;
            string size;
            int ssize;
            string chunk;
            auto ppos=pos+2;
            while(1)
            {
                HttpStringUtil::get_split_str(totalRecv,size,"\r\n","\r\n",ppos);
                ppos=totalRecv.find("\r\n",ppos+2);
                //cout<<size<<endl;
                NumberStringConvertUtil::str16toInt(size,ssize);
                if(ssize==-1)
                {
                    flag=false;
                    totalRecv.erase(pos);
                    this->header=totalRecv;
                    body="";
                    return true;
                }
                else if(ssize==0)
                {
                    flag=true;
                    totalRecv.erase(pos);
                    this->header=totalRecv;
                    return true;
                }
                else//读取数据
                {
                    //chunk=HttpStringUtil::get_split_str(totalRecv,size,"\r\n","\r\n",ppos);
                    chunk=totalRecv.substr(ppos+2,ssize);
                    body+=chunk;
                    ppos=ppos+2+ssize;
                }
            }
        }
        else//按照指定字节接收
        {
            int hasReceived=totalRecv.length()-(pos+4);
            string size;
            int ssize;
            HttpStringUtil::get_split_str(totalRecv,size,"Content-Length: ","\r\n");
            NumberStringConvertUtil::toInt(size,ssize);
            if(ssize==-1)
            {
                flag=true;
                totalRecv.erase(pos);
                this->header=totalRecv;
                body="";
                return true;
            }
            else if(ssize==0)
            {
                flag=true;
                totalRecv.erase(pos);
                this->header=totalRecv;
                body="";
                return true;
            }
            else
            {
                if(ssize-hasReceived<=0)
                {
                    body=totalRecv.substr(pos+4);
                    totalRecv.erase(pos);
                    this->header=totalRecv;
                    flag=true;
                    return true;
                }
                else
                {  
                    if(recvDataByLength(recv,ssize-hasReceived,sec)<=0)
                    {
                        flag=false;
                        totalRecv.erase(pos);
                        this->header=totalRecv;
                        body="";
                        return true;
                    }
                    else
                    {
                        totalRecv+=recv;
                        body=totalRecv.substr(pos+4);
                        totalRecv.erase(pos);
                        this->header=totalRecv;
                        flag=true;
                        return true;
                    }
                }
            }
        }
    }
    bool stt::network::HttpClient::getRequestFromFD(const int &fd,SSL *ssl,const string &url,const string &header,const string &header1,const int &sec)
    {
        this->flag=false;
        this->header="";
        this->body="";
        string ip;
        int port;
        string locPara;
        HttpStringUtil::getIP(url,ip);
        HttpStringUtil::getPort(url,port);
        HttpStringUtil::getLocPara(url,locPara);
        TcpFDHandler k;
        k.setFD(fd,ssl,false,false,sec);
        k.blockSet(sec);
        //套上套接字完成，后面进行发送请求报文操作

        //拼接报文
        string ss;
        ss="GET "+locPara+" HTTP/1.1\r\n"\
            +"Host: "+ip+":"+to_string(port)+"\r\n"\
            +header1+"\r\n"\
            +header+"\r\n";

        //发送
        const int sentBytes=k.sendData(ss);
        if(sentBytes<0||static_cast<size_t>(sentBytes)!=ss.length())
            return false;
        //接收
        string totalRecv="";
        string recv;
        while(totalRecv.find("\r\n\r\n")==string::npos)
        {
            if(k.recvData(recv,1024)<=0)
            {
                flag=false;
                this->header="";
                body="";
                return true;
            }
            totalRecv+=recv;
        }
        auto pos=totalRecv.find("\r\n\r\n");
        recv.clear();
        if(totalRecv.find("Transfer-Encoding: chunked")!=string::npos)//chunked先全部接受完
        {
            while(totalRecv.find("0\r\n\r\n",pos)==string::npos)
            {
                if(recvData(recv,1024)<=0)
                {
                    flag=false;
                    body="";
                    totalRecv.erase(pos);
                    this->header=totalRecv;
                    return true;
                }
                totalRecv+=recv;
            }
            //cout<<totalRecv<<endl;
            string size;
            int ssize;
            string chunk;
            auto ppos=pos+2;
            while(1)
            {
                HttpStringUtil::get_split_str(totalRecv,size,"\r\n","\r\n",ppos);
                ppos=totalRecv.find("\r\n",ppos+2);
                //cout<<size<<endl;
                NumberStringConvertUtil::str16toInt(size,ssize);
                if(ssize==-1)
                {
                    flag=false;
                    totalRecv.erase(pos);
                    this->header=totalRecv;
                    body="";
                    return true;
                }
                else if(ssize==0)
                {
                    flag=true;
                    totalRecv.erase(pos);
                    this->header=totalRecv;
                    return true;
                }
                else//读取数据
                {
                    //chunk=HttpStringUtil::get_split_str(totalRecv,size,"\r\n","\r\n",ppos);
                    chunk=totalRecv.substr(ppos+2,ssize);
                    body+=chunk;
                    ppos=ppos+2+ssize;
                }
            }
        }
        else//按照指定字节接收
        {
            int hasReceived=totalRecv.length()-(pos+4);
            string size;
            int ssize;
            HttpStringUtil::get_split_str(totalRecv,size,"Content-Length: ","\r\n");
            NumberStringConvertUtil::toInt(size,ssize);
            if(ssize==-1)
            {
                flag=true;
                totalRecv.erase(pos);
                this->header=totalRecv;
                body="";
                return true;
            }
            else if(ssize==0)
            {
                flag=true;
                totalRecv.erase(pos);
                this->header=totalRecv;
                body="";
                return true;
            }
            else
            {
                if(ssize-hasReceived<=0)
                {
                    body=totalRecv.substr(pos+4);
                    totalRecv.erase(pos);
                    this->header=totalRecv;
                    flag=true;
                    return true;
                }
                else
                {  
                    if(k.recvDataByLength(recv,ssize-hasReceived,sec)<=0)
                    {
                        flag=false;
                        totalRecv.erase(pos);
                        this->header=totalRecv;
                        body="";
                        return true;
                    }
                    else
                    {
                        totalRecv+=recv;
                        body=totalRecv.substr(pos+4);
                        totalRecv.erase(pos);
                        this->header=totalRecv;
                        flag=true;
                        return true;
                    }
                }
            }
        }
    }
    bool stt::network::HttpClient::postRequest(const string &url,const string &body,const string &header,const string &header1,const int &sec)
    {
        this->flag=false;
        this->header="";
        this->body="";
        string ip;
        int port;
        string locPara;
        HttpStringUtil::getIP(url,ip);
        HttpStringUtil::getPort(url,port);
        HttpStringUtil::getLocPara(url,locPara);
        blockSet(sec);
        if(!isConnect()||getServerIP()!=ip||getServerPort()!=port)//没有连接或者服务器变更需要重新连接
        {
            if(isConnect())//如果是变更服务器 需要先关闭原有的连接
            {
                if(!close())
                {
                    cerr<<"http无法关闭前一个连接"<<endl;
                    return false;
                }
            }
            blockSet(sec);
            if(!connect(ip,port))
            {
                //cout<<ip<<endl;
                //cout<<port<<endl;
                cerr<<"http无法连接到服务器"<<endl;
                return false;
            }
        }

        //连接到服务器完成，后面进行发送请求报文操作

        //拼接报文
        string ss;
        ss="POST "+locPara+" HTTP/1.1\r\n"\
            +"Host: "+ip+":"+to_string(port)+"\r\n"\
            +"Content-Length: "+to_string(body.length())+"\r\n"\
            +header1+"\r\n"\
            +header+"\r\n"\
            +body;

        //发送
        const int sentBytes=sendData(ss);
        if(sentBytes<0||static_cast<size_t>(sentBytes)!=ss.length())
            return false;
        //接收
        string totalRecv="";
        string recv;
        while(totalRecv.find("\r\n\r\n")==string::npos)
        {
            if(recvData(recv,1024)<=0)
            {
                flag=false;
                this->header="";
                this->body="";
                return true;
            }
            totalRecv+=recv;
        }
        auto pos=totalRecv.find("\r\n\r\n");

        if(totalRecv.find("Transfer-Encoding: chunked")!=string::npos)//chunked先全部接受完
        {
            while(totalRecv.find("0\r\n\r\n",pos)==string::npos)
            {
                if(recvData(recv,1024)<=0)
                {
                    flag=false;
                    this->body="";
                    totalRecv.erase(pos);
                    this->header=totalRecv;
                    return true;
                }
                totalRecv+=recv;
            }
            //cout<<totalRecv<<endl;
            string size;
            int ssize;
            string chunk;
            auto ppos=pos+2;
            while(1)
            {
                HttpStringUtil::get_split_str(totalRecv,size,"\r\n","\r\n",ppos);
                ppos=totalRecv.find("\r\n",ppos+2);
                //cout<<size<<endl;
                NumberStringConvertUtil::str16toInt(size,ssize);
                if(ssize==-1)
                {
                    flag=false;
                    totalRecv.erase(pos);
                    this->header=totalRecv;
                    this->body="";
                    return true;
                }
                else if(ssize==0)
                {
                    flag=true;
                    totalRecv.erase(pos);
                    this->header=totalRecv;
                    return true;
                }
                else//读取数据
                {
                    //chunk=HttpStringUtil::get_split_str(totalRecv,size,"\r\n","\r\n",ppos);
                    chunk=totalRecv.substr(ppos+2,ssize);
                    this->body+=chunk;
                    ppos=ppos+2+ssize;
                }
            }
        }
        else//按照指定字节接收
        {
            int hasReceived=totalRecv.length()-(pos+4);
            string size;
            int ssize;
            HttpStringUtil::get_split_str(totalRecv,size,"Content-Length: ","\r\n");
            NumberStringConvertUtil::toInt(size,ssize);
            if(ssize==-1)
            {
                flag=false;
                totalRecv.erase(pos);
                this->header=totalRecv;
                this->body="";
                return true;
            }
            else if(ssize==0)
            {
                flag=true;
                totalRecv.erase(pos);
                this->header=totalRecv;
                this->body="";
                return true;
            }
            else
            {
                if(ssize-hasReceived<=0)
                {
                    this->body=totalRecv.substr(pos+4);
                    totalRecv.erase(pos);
                    this->header=totalRecv;
                    flag=true;
                    return true;
                }
                else
                {
                    if(recvDataByLength(recv,ssize-hasReceived,sec)<=0)
                    {
                        flag=false;
                        totalRecv.erase(pos);
                        this->header=totalRecv;
                        this->body="";
                        return true;
                    }
                    else
                    {
                        totalRecv+=recv;
                        this->body=totalRecv.substr(pos+4);
                        totalRecv.erase(pos);
                        this->header=totalRecv;
                        flag=true;
                        return true;
                    }
                }
            }
        }
    }
    bool stt::network::HttpClient::postRequestFromFD(const int &fd,SSL *ssl,const string &url,const string &body,const string &header,const string &header1,const int &sec)
    {
        this->flag=false;
        this->header="";
        this->body="";
        string ip;
        int port;
        string locPara;
        HttpStringUtil::getIP(url,ip);
        HttpStringUtil::getPort(url,port);
        HttpStringUtil::getLocPara(url,locPara);
        TcpFDHandler k;
        k.setFD(fd,ssl,false,false,sec);
        k.blockSet(sec);
        //套上套接字完成，后面进行发送请求报文操作

        //拼接报文
        string ss;
        ss="POST "+locPara+" HTTP/1.1\r\n"\
            +"Host: "+ip+":"+to_string(port)+"\r\n"\
            +"Content-Length: "+to_string(body.length())+"\r\n"\
            +header1+"\r\n"\
            +header+"\r\n"\
            +body;

        //发送
        const int sentBytes=k.sendData(ss);
        if(sentBytes<0||static_cast<size_t>(sentBytes)!=ss.length())
            return false;
        //接收
        string totalRecv="";
        string recv;
        while(totalRecv.find("\r\n\r\n")==string::npos)
        {
            if(k.recvData(recv,1024)<=0)
            {
                flag=false;
                this->header="";
                this->body="";
                return true;
            }
            totalRecv+=recv;
        }
        auto pos=totalRecv.find("\r\n\r\n");

        if(totalRecv.find("Transfer-Encoding: chunked")!=string::npos)//chunked先全部接受完
        {
            while(totalRecv.find("0\r\n\r\n",pos)==string::npos)
            {
                if(recvData(recv,1024)<=0)
                {
                    flag=false;
                    this->body="";
                    totalRecv.erase(pos);
                    this->header=totalRecv;
                    return true;
                }
                totalRecv+=recv;
            }
            //cout<<totalRecv<<endl;
            string size;
            int ssize;
            string chunk;
            auto ppos=pos+2;
            while(1)
            {
                HttpStringUtil::get_split_str(totalRecv,size,"\r\n","\r\n",ppos);
                ppos=totalRecv.find("\r\n",ppos+2);
                //cout<<size<<endl;
                NumberStringConvertUtil::str16toInt(size,ssize);
                if(ssize==-1)
                {
                    flag=false;
                    totalRecv.erase(pos);
                    this->header=totalRecv;
                    this->body="";
                    return true;
                }
                else if(ssize==0)
                {
                    flag=true;
                    totalRecv.erase(pos);
                    this->header=totalRecv;
                    return true;
                }
                else//读取数据
                {
                    //chunk=HttpStringUtil::get_split_str(totalRecv,size,"\r\n","\r\n",ppos);
                    chunk=totalRecv.substr(ppos+2,ssize);
                    this->body+=chunk;
                    ppos=ppos+2+ssize;
                }
            }
        }
        else//按照指定字节接收
        {
            int hasReceived=totalRecv.length()-(pos+4);
            string size;
            int ssize;
            HttpStringUtil::get_split_str(totalRecv,size,"Content-Length: ","\r\n");
            NumberStringConvertUtil::toInt(size,ssize);
            if(ssize==-1)
            {
                flag=false;
                totalRecv.erase(pos);
                this->header=totalRecv;
                this->body="";
                return true;
            }
            else if(ssize==0)
            {
                flag=true;
                totalRecv.erase(pos);
                this->header=totalRecv;
                this->body="";
                return true;
            }
            else
            {
                if(ssize-hasReceived<=0)
                {
                    this->body=totalRecv.substr(pos+4);
                    totalRecv.erase(pos);
                    this->header=totalRecv;
                    flag=true;
                    return true;
                }
                else
                {
                    if(k.recvDataByLength(recv,ssize-hasReceived,sec)<=0)
                    {
                        flag=false;
                        totalRecv.erase(pos);
                        this->header=totalRecv;
                        this->body="";
                        return true;
                    }
                    else
                    {
                        totalRecv+=recv;
                        this->body=totalRecv.substr(pos+4);
                        totalRecv.erase(pos);
                        this->header=totalRecv;
                        flag=true;
                        return true;
                    }
                }
            }
        }
    }
    void stt::network::TcpServer::publishWorkerResult(WorkerMessage message)
    {
        if (!finishQueue.push(std::move(message)))
        {
            metricWorkerQueueOverflows.fetch_add(1,std::memory_order_relaxed);
            std::lock_guard<std::mutex> lock(overflowFinishMutex);
            overflowFinishQueue.push_back(std::move(message));
        }
        notifyReactor();
    }

    void stt::network::TcpServer::notifyReactor() noexcept
    {
        if(workerWakePending.exchange(true,std::memory_order_acq_rel))
        {
            metricReactorWakeupsCoalesced.fetch_add(1,std::memory_order_relaxed);
            return;
        }
        const int eventFD=workerEventFD.load(std::memory_order_acquire);
        if(eventFD<0)
        {
            workerWakePending.store(false,std::memory_order_release);
            return;
        }
        const uint64_t one=1;
        ssize_t result=0;
        do {result=::write(eventFD,&one,sizeof(one));}
        while(result<0&&errno==EINTR);
        if(result==static_cast<ssize_t>(sizeof(one)))
            metricReactorWakeups.fetch_add(1,std::memory_order_relaxed);
        else if(result<0&&errno!=EAGAIN&&errno!=EWOULDBLOCK)
            workerWakePending.store(false,std::memory_order_release);
    }

    void stt::network::TcpServer::reportReactorStartup(const bool success)
    {
        {
            std::lock_guard<std::mutex> lock(reactorStartupMutex);
            reactorStartupSuccess=success;
            reactorStartupComplete=true;
        }
        reactorStartupCV.notify_all();
    }

    void stt::network::TcpServer::advanceGracefulDrain()
    {
        if(!gracefulDrainRequested.load(std::memory_order_acquire))
            return;
        for(auto &entry:clientfd)
        {
            TcpFDInf &connection=entry.second;
            if(connection.fd<0||connection.closing||connection.active_workers>0||
               !connection.pendindQueue.empty())
                continue;
            const auto state=connection.write_state;
            if(!state)
            {
                TcpServer::close(connection.fd);
                continue;
            }
            bool hasPendingWrites=false;
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                if(state->closed)
                    continue;
                hasPendingWrites=state->queued_bytes>0;
                if(hasPendingWrites)
                    state->close_after_flush=true;
            }
            if(hasPendingWrites)
                publishSendReady(state);
            else
                TcpServer::close(connection.fd);
        }
        if(metricActiveConnections.load(std::memory_order_acquire)==0)
            gracefulShutdownCV.notify_all();
    }

    bool stt::network::TcpServer::hasPendingReactorWork()
    {
        if(finishQueue.possibly_nonempty()||sendReadyQueue.possibly_nonempty())
            return true;
        {
            std::lock_guard<std::mutex> lock(overflowFinishMutex);
            if(!overflowFinishQueue.empty())
                return true;
        }
        std::lock_guard<std::mutex> lock(overflowSendReadyMutex);
        return !overflowSendReadyQueue.empty();
    }

    void stt::network::TcpServer::clearReactorQueues()
    {
        WorkerMessage discardedMessage;
        while(finishQueue.pop(discardedMessage)) {}
        SendReadyMessage discardedSendReady;
        while(sendReadyQueue.pop(discardedSendReady)) {}
        {
            std::lock_guard<std::mutex> lock(overflowFinishMutex);
            overflowFinishQueue.clear();
        }
        {
            std::lock_guard<std::mutex> lock(overflowSendReadyMutex);
            overflowSendReadyQueue.clear();
        }
        bufferedReadQueue.clear();
        timeoutCandidates.clear();
        std::lock_guard<std::mutex> lock(writeRegistryMutex);
        writeRegistry.clear();
    }

    size_t stt::network::TcpServer::drainWorkerResults(const size_t budget)
    {
        size_t processed=0;
        WorkerMessage message;
        const size_t ringBudget=budget-(budget/4);
        while(processed<ringBudget&&finishQueue.pop(message))
        {
            handler_workerevent(std::move(message));
            ++processed;
        }
        std::deque<WorkerMessage> overflow;
        {
            std::lock_guard<std::mutex> lock(overflowFinishMutex);
            while(processed+overflow.size()<budget&&!overflowFinishQueue.empty())
            {
                overflow.push_back(std::move(overflowFinishQueue.front()));
                overflowFinishQueue.pop_front();
            }
        }
        while(!overflow.empty())
        {
            handler_workerevent(std::move(overflow.front()));
            overflow.pop_front();
            ++processed;
        }
        while(processed<budget&&finishQueue.pop(message))
        {
            handler_workerevent(std::move(message));
            ++processed;
        }
        return processed;
    }

    size_t stt::network::TcpServer::drainSendReady(const int &epollFD,const size_t budget)
    {
        size_t processed=0;
        SendReadyMessage message;
        const size_t ringBudget=budget-(budget/4);
        while(processed<ringBudget&&sendReadyQueue.pop(message))
        {
            handleSendReady(epollFD,std::move(message));
            ++processed;
        }
        std::deque<SendReadyMessage> overflow;
        {
            std::lock_guard<std::mutex> lock(overflowSendReadyMutex);
            while(processed+overflow.size()<budget&&!overflowSendReadyQueue.empty())
            {
                overflow.push_back(std::move(overflowSendReadyQueue.front()));
                overflowSendReadyQueue.pop_front();
            }
        }
        while(!overflow.empty())
        {
            handleSendReady(epollFD,std::move(overflow.front()));
            overflow.pop_front();
            ++processed;
        }
        while(processed<budget&&sendReadyQueue.pop(message))
        {
            handleSendReady(epollFD,std::move(message));
            ++processed;
        }
        return processed;
    }

    void stt::network::TcpServer::drainReactorWork(const int &epollFD)
    {
        const int eventFD=workerEventFD.load(std::memory_order_acquire);
        if(eventFD>=0)
        {
            uint64_t value=0;
            while(::read(eventFD,&value,sizeof(value))==static_cast<ssize_t>(sizeof(value))) {}
        }
        drainWorkerResults(workerCompletionBudgetPerWake);
        drainSendReady(epollFD,sendReadyBudgetPerWake);

        // 先开放下一次门铃，再检查是否有生产者在本轮 drain 尾部提交，避免丢唤醒。
        workerWakePending.store(false,std::memory_order_release);
        if(hasPendingReactorWork())
            notifyReactor();
    }

    void stt::network::TcpServer::scheduleBufferedRead(const int &fd,const uint64_t connection)
    {
        bufferedReadQueue.push_back({fd,connection});
    }

    void stt::network::TcpServer::publishSendReady(const std::shared_ptr<ConnectionWriteState> &state)
    {
        if(!state)
            return;
        SendReadyMessage message;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            if(state->closed||state->notification_pending)
                return;
            state->notification_pending=true;
            message={state->fd,state->connection_obj_fd};
        }
        if(!sendReadyQueue.push(std::move(message)))
        {
            metricSendReadyQueueOverflows.fetch_add(1,std::memory_order_relaxed);
            std::lock_guard<std::mutex> lock(overflowSendReadyMutex);
            overflowSendReadyQueue.push_back(std::move(message));
        }
        notifyReactor();
    }

    int stt::network::TcpServer::enqueueWrite(const std::shared_ptr<ConnectionWriteState> &state,std::string data)
    {
        if(!state)
            return -99;
        if(data.size()>static_cast<size_t>(std::numeric_limits<int>::max()))
            return -98;
        const int acceptedBytes=static_cast<int>(data.size());
        bool accepted=true;
        bool shouldNotify=false;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            if(state->closed||state->close_requested)
                return -99;
            if(data.size()>state->max_queued_bytes||state->queued_bytes>state->max_queued_bytes-data.size())
            {
                state->overflowed=true;
                state->close_requested=true;
                metricWriteOverflows.fetch_add(1,std::memory_order_relaxed);
                accepted=false;
                shouldNotify=true;
            }
            else if(!data.empty())
            {
                state->queued_bytes+=data.size();
                state->queue.push_back(std::move(data));
                metricQueuedWriteBytes.fetch_add(static_cast<uint64_t>(acceptedBytes),std::memory_order_relaxed);
                const uint64_t pending=metricPendingWriteBytes.fetch_add(
                    static_cast<uint64_t>(acceptedBytes),std::memory_order_relaxed)+
                    static_cast<uint64_t>(acceptedBytes);
                recordAtomicMaximum(metricPeakPendingWriteBytes,pending);
                shouldNotify=true;
            }
        }
        if(shouldNotify)
            publishSendReady(state);
        return accepted?acceptedBytes:-101;
    }

    void stt::network::TcpServer::requestQueuedClose(const std::shared_ptr<ConnectionWriteState> &state)
    {
        if(!state)
            return;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            if(state->closed)
                return;
            state->close_requested=true;
        }
        publishSendReady(state);
    }

    void stt::network::TcpServer::requestCloseAfterFlush(const int &fd,const uint64_t expectedConnection)
    {
        std::shared_ptr<ConnectionWriteState> state;
        {
            std::lock_guard<std::mutex> lock(writeRegistryMutex);
            const auto stateIt=writeRegistry.find(fd);
            if(stateIt!=writeRegistry.end())
                state=stateIt->second.lock();
        }
        if(!state||(expectedConnection!=0&&state->connection_obj_fd!=expectedConnection))
            return;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            if(state->closed)
                return;
            state->close_after_flush=true;
        }
        publishSendReady(state);
    }

    void stt::network::TcpServer::prepareHandler(TcpFDHandler &handler,const int &fd)
    {
        auto connectionIt=clientfd.find(fd);
        if(connectionIt==clientfd.end()||connectionIt->second.fd!=fd||connectionIt->second.closing)
        {
            handler.setFD(-1,nullptr,true);
            return;
        }
        TcpFDInf &connection=connectionIt->second;
        handler.setFD(fd,connection.ssl,unblock);
        const std::weak_ptr<ConnectionWriteState> weakState=connection.write_state;
        handler.setTransportFunctions(
            [this,weakState](std::string data) {
                return enqueueWrite(weakState.lock(),std::move(data));
            },
            [this,weakState] {
                requestQueuedClose(weakState.lock());
            }
        );
    }

    void stt::network::TcpServer::prepareQueuedHandler(TcpFDHandler &handler,const int &fd)
    {
        prepareQueuedHandler(handler,fd,0);
    }

    void stt::network::TcpServer::prepareQueuedHandler(TcpFDHandler &handler,const int &fd,const uint64_t expectedConnection)
    {
        std::shared_ptr<ConnectionWriteState> state;
        {
            std::lock_guard<std::mutex> lock(writeRegistryMutex);
            auto stateIt=writeRegistry.find(fd);
            if(stateIt!=writeRegistry.end())
                state=stateIt->second.lock();
        }
        if(!state||(expectedConnection!=0&&state->connection_obj_fd!=expectedConnection))
        {
            handler.setFD(-1,nullptr,true);
            return;
        }
        handler.setFD(fd,nullptr,true);
        const std::weak_ptr<ConnectionWriteState> weakState=state;
        handler.setTransportFunctions(
            [this,weakState](std::string data) {
                return enqueueWrite(weakState.lock(),std::move(data));
            },
            [this,weakState] {
                requestQueuedClose(weakState.lock());
            }
        );
    }

    bool stt::network::TcpServer::updateConnectionEvents(const int &epollFD,TcpFDInf &connection,const bool &wantWrite)
    {
        if(connection.fd<0||connection.write_interest==wantWrite)
            return true;
        epoll_event event{};
        event.data.fd=connection.fd;
        event.events=EPOLLIN|EPOLLERR|EPOLLHUP|EPOLLRDHUP|EPOLLET;
        if(wantWrite)
            event.events|=EPOLLOUT;
        if(epoll_ctl(epollFD,EPOLL_CTL_MOD,connection.fd,&event)<0)
            return false;
        connection.write_interest=wantWrite;
        return true;
    }

    stt::network::TcpServer::WriteFlushResult stt::network::TcpServer::flushConnectionWrites(TcpFDInf &connection)
    {
        const auto state=connection.write_state;
        if(!state)
            return WriteFlushResult::Error;
        size_t sentThisTurn=0;
        while(sentThisTurn<writeBudgetPerEvent)
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            if(state->closed||state->close_requested||state->overflowed)
                return WriteFlushResult::Error;
            if(state->queue.empty())
            {
                state->front_offset=0;
                return state->close_after_flush?WriteFlushResult::Error:WriteFlushResult::Drained;
            }

            ssize_t result=0;
            if(connection.ssl==nullptr)
            {
                // 普通 TCP 使用 scatter/gather，一次系统调用覆盖多个排队数据块。
                constexpr size_t maxBatchBuffers=64;
                std::array<iovec,maxBatchBuffers> vectors{};
                size_t vectorCount=0;
                size_t plannedBytes=0;
                bool first=true;
                for(std::string &entry:state->queue)
                {
                    const size_t offset=first?state->front_offset:0;
                    first=false;
                    if(offset>=entry.size())
                        continue;
                    const size_t allowance=std::min(entry.size()-offset,
                        writeBudgetPerEvent-sentThisTurn-plannedBytes);
                    vectors[vectorCount].iov_base=const_cast<char*>(entry.data()+offset);
                    vectors[vectorCount].iov_len=allowance;
                    ++vectorCount;
                    plannedBytes+=allowance;
                    if(vectorCount==maxBatchBuffers||sentThisTurn+plannedBytes>=writeBudgetPerEvent)
                        break;
                }
                if(vectorCount==0)
                    return WriteFlushResult::Error;
                msghdr message{};
                message.msg_iov=vectors.data();
                message.msg_iovlen=vectorCount;
                metricWriteSyscalls.fetch_add(1,std::memory_order_relaxed);
                result=::sendmsg(connection.fd,&message,MSG_NOSIGNAL);
                if(result<0)
                {
                    if(errno==EINTR)
                        continue;
                    if(errno==EAGAIN||errno==EWOULDBLOCK)
                        return WriteFlushResult::WaitWrite;
                    return WriteFlushResult::Error;
                }
                if(result>0&&vectorCount>1)
                {
                    metricBatchedWriteSyscalls.fetch_add(1,std::memory_order_relaxed);
                    metricBatchedWriteBuffers.fetch_add(vectorCount,std::memory_order_relaxed);
                }
            }
            else
            {
                std::string &front=state->queue.front();
                const size_t remaining=front.size()-state->front_offset;
                const size_t allowance=std::min({remaining,writeBudgetPerEvent-sentThisTurn,
                    static_cast<size_t>(std::numeric_limits<int>::max())});
                metricWriteSyscalls.fetch_add(1,std::memory_order_relaxed);
                result=SSL_write(connection.ssl,front.data()+state->front_offset,static_cast<int>(allowance));
                if(result<=0)
                {
                    const int sslError=SSL_get_error(connection.ssl,static_cast<int>(result));
                    if(sslError==SSL_ERROR_WANT_WRITE)
                        return WriteFlushResult::WaitWrite;
                    if(sslError==SSL_ERROR_WANT_READ)
                        return WriteFlushResult::WaitRead;
                    return WriteFlushResult::Error;
                }
            }
            if(result==0)
                return WriteFlushResult::Error;

            const size_t written=static_cast<size_t>(result);
            size_t remainingWritten=written;
            while(remainingWritten>0&&!state->queue.empty())
            {
                const size_t available=state->queue.front().size()-state->front_offset;
                const size_t consumed=std::min(available,remainingWritten);
                state->front_offset+=consumed;
                state->queued_bytes-=consumed;
                remainingWritten-=consumed;
                if(state->front_offset==state->queue.front().size())
                {
                    state->queue.pop_front();
                    state->front_offset=0;
                }
            }
            metricSentBytes.fetch_add(written,std::memory_order_relaxed);
            metricPendingWriteBytes.fetch_sub(written,std::memory_order_relaxed);
            sentThisTurn+=written;
        }
        return WriteFlushResult::Reschedule;
    }

    void stt::network::TcpServer::handleSendReady(const int &epollFD,SendReadyMessage message)
    {
        auto connectionIt=clientfd.find(message.fd);
        if(connectionIt==clientfd.end()||connectionIt->second.fd!=message.fd||
           connectionIt->second.connection_obj_fd!=message.connection_obj_fd)
            return;
        TcpFDInf &connection=connectionIt->second;
        const auto state=connection.write_state;
        if(!state)
            return;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->notification_pending=false;
            if(state->closed)
                return;
        }

        const WriteFlushResult result=flushConnectionWrites(connection);
        connection.write_waiting_for_read=result==WriteFlushResult::WaitRead;
        if(result==WriteFlushResult::Error)
        {
            TcpServer::close(message.fd);
            return;
        }
        if(!updateConnectionEvents(epollFD,connection,result==WriteFlushResult::WaitWrite))
        {
            TcpServer::close(message.fd);
            return;
        }
        if(result==WriteFlushResult::Reschedule)
            publishSendReady(state);
    }
    void stt::network::TcpServer::putTask(const std::function<int(TcpFDHandler &k,TcpInformation &inf)> &fun,TcpFDHandler &k,TcpInformation &inf)
    {
        auto connectionIt=clientfd.find(inf.fd);
        if(connectionIt==clientfd.end()||connectionIt->second.closing||
           connectionIt->second.connection_obj_fd!=inf.connection_obj_fd)
            return;
        ++connectionIt->second.active_workers;
        auto request=std::make_shared<TcpInformation>(inf);
        TcpFDHandler handler=k;
        const bool submitted=workpool!=nullptr&&workpool->submit(
            [this,handler,request,fun]() mutable ->void
            {
                int ret=-2;
                try { ret=fun(handler,*request); }
                catch (...) { ret=-2; }
                publishWorkerResult({request->fd,request->connection_obj_fd,ret,request});
            });
        if(submitted)
            recordAtomicMaximum(metricPeakPendingWorkerTasks,workpool->peakPendingTasks());
        else
        {
            metricWorkerTaskRejections.fetch_add(1,std::memory_order_relaxed);
            publishWorkerResult({request->fd,request->connection_obj_fd,-2,request});
        }
    }
    void stt::network::HttpServer::putTask(const std::function<int(HttpServerFDHandler &k,HttpRequestInformation &inf)> &fun,HttpServerFDHandler &k,HttpRequestInformation &inf)
    {
        auto connectionIt=clientfd.find(inf.fd);
        if(connectionIt==clientfd.end()||connectionIt->second.closing||
           connectionIt->second.connection_obj_fd!=inf.connection_obj_fd)
            return;
        auto request=std::make_shared<HttpRequestInformation>(inf);
        ++connectionIt->second.active_workers;
        HttpServerFDHandler handler=k;
        const bool submitted=workpool!=nullptr&&workpool->submit(
            [this,handler,request,fun]() mutable ->void
            {
                int ret=-2;
                try { ret=fun(handler,*request); }
                catch (...) { ret=-2; }
                publishWorkerResult({request->fd,request->connection_obj_fd,ret,request});
            });
        if(submitted)
            recordAtomicMaximum(metricPeakPendingWorkerTasks,workpool->peakPendingTasks());
        else
        {
            metricWorkerTaskRejections.fetch_add(1,std::memory_order_relaxed);
            publishWorkerResult({request->fd,request->connection_obj_fd,-2,request});
        }
    }
    void stt::network::WebSocketServer::putTask(const std::function<int(WebSocketServerFDHandler &k,WebSocketFDInformation &inf)> &fun,WebSocketServerFDHandler &k,WebSocketFDInformation &inf)
    {
        auto connectionIt=clientfd.find(inf.fd);
        if(connectionIt==clientfd.end()||connectionIt->second.closing||
           connectionIt->second.connection_obj_fd!=inf.connection_obj_fd)
            return;
        auto request=std::make_shared<WebSocketFDInformation>(inf);
        ++connectionIt->second.active_workers;
        WebSocketServerFDHandler handler=k;
        const bool submitted=workpool!=nullptr&&workpool->submit(
            [this,handler,request,fun]() mutable ->void
            {
                int ret=-2;
                try { ret=fun(handler,*request); }
                catch (...) { ret=-2; }
                publishWorkerResult({request->fd,request->connection_obj_fd,ret,request});
            });
        if(submitted)
            recordAtomicMaximum(metricPeakPendingWorkerTasks,workpool->peakPendingTasks());
        else
        {
            metricWorkerTaskRejections.fetch_add(1,std::memory_order_relaxed);
            publishWorkerResult({request->fd,request->connection_obj_fd,-2,request});
        }
    }
    bool stt::network::TcpServer::setTLS(const char *cert,const char *key,const char *passwd,const char *ca)
    {
        return setTLS(cert,key,passwd,ca,TLSClientAuthMode::Required);
    }

    bool stt::network::TcpServer::setTLS(const char *cert,const char *key,const char *passwd)
    {
        return setTLS(cert,key,passwd,"",TLSClientAuthMode::None);
    }

    bool stt::network::TcpServer::setTLS(const char *cert,const char *key,const char *passwd,
                                         const char *ca,const TLSClientAuthMode clientAuth)
    {
        if(cert==nullptr||cert[0]=='\0'||key==nullptr||key[0]=='\0')
            return false;
        const bool verifiesClient=clientAuth!=TLSClientAuthMode::None;
        if(verifiesClient&&(ca==nullptr||ca[0]=='\0'))
            return false;

        SSLeay_add_ssl_algorithms();
        OpenSSL_add_all_algorithms();
        SSL_load_error_strings();
        SSL_CTX *newContext=SSL_CTX_new(TLS_server_method());
        if(newContext==nullptr)
            return false;

        const auto fail=[&newContext]() {
            SSL_CTX_free(newContext);
            newContext=nullptr;
            return false;
        };
        if(SSL_CTX_set_min_proto_version(newContext,TLS1_2_VERSION)!=1)
            return fail();
        SSL_CTX_set_options(newContext,SSL_OP_NO_COMPRESSION);
#ifdef SSL_MODE_RELEASE_BUFFERS
        SSL_CTX_set_mode(newContext,SSL_MODE_RELEASE_BUFFERS);
#endif
        SSL_CTX_set_session_cache_mode(newContext,SSL_SESS_CACHE_SERVER);

        int verifyMode=SSL_VERIFY_NONE;
        if(clientAuth==TLSClientAuthMode::Optional)
            verifyMode=SSL_VERIFY_PEER;
        else if(clientAuth==TLSClientAuthMode::Required)
            verifyMode=SSL_VERIFY_PEER|SSL_VERIFY_FAIL_IF_NO_PEER_CERT;
        SSL_CTX_set_verify(newContext,verifyMode,nullptr);
        if(ca!=nullptr&&ca[0]!='\0'&&SSL_CTX_load_verify_locations(newContext,ca,nullptr)!=1)
            return fail();
        if(SSL_CTX_use_certificate_chain_file(newContext,cert)<=0)
            return fail();
        if(passwd!=nullptr&&passwd[0]!='\0')
            SSL_CTX_set_default_passwd_cb_userdata(newContext,const_cast<char*>(passwd));
        const int keyResult=SSL_CTX_use_PrivateKey_file(newContext,key,SSL_FILETYPE_PEM);
        SSL_CTX_set_default_passwd_cb_userdata(newContext,nullptr);
        if(keyResult<=0||SSL_CTX_check_private_key(newContext)!=1)
            return fail();

        SSL_CTX *oldContext=nullptr;
        {
            std::lock_guard<std::mutex> lock(tlsContextMutex);
            oldContext=ctx;
            ctx=newContext;
            TLS=true;
        }
        if(oldContext!=nullptr)
            SSL_CTX_free(oldContext);
        return true;
    }

    void stt::network::TcpServer::redrawTLS()
    {
        SSL_CTX *oldContext=nullptr;
        {
            std::lock_guard<std::mutex> lock(tlsContextMutex);
            oldContext=ctx;
            ctx=nullptr;
            TLS=false;
        }
        if(oldContext!=nullptr)
            SSL_CTX_free(oldContext);
    }
    bool stt::network::TcpServer::startListen(const int &port,const int &threads)
    {
        std::lock_guard<std::recursive_mutex> lifecycleLock(lifecycleMutex);
        if(port<0||port>65535||threads<=0)
            return false;
        if(isListen())
        {
            //是否是改变端口的监听？
            if(port==0||this->port==port)
                return true;
            else
            {
                if(!close())
                    return false;
            }
        }
        //this->logfile=logfile;
        //memset(solvingFD,0,sizeof(int)*maxFD);
        //fdQueue=new queue<QueueFD>[threads];
        //cv=new condition_variable[threads];
        //lq1=new mutex[threads];
        clientfd.clear();
        timeoutCandidates.clear();
        {
            std::lock_guard<std::mutex> lock(writeRegistryMutex);
            writeRegistry.clear();
        }
        clientfd.reserve(4096);
        //socket准备
        // The listener itself must be non-blocking when used with edge-triggered
        // epoll. accept4() flags only affect the returned client socket.
        fd=socket(AF_INET,SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0);
        if(fd<0)
        {
            perror("socket");
            return false;
        }
        //设置unblock和mutiuse
        int opt=1;
        if(setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt))<0)
        {
            cerr<<"set SO_REUSEADDR failed"<<endl;
            perror("setsockopt");
            ::close(fd);
            fd=-1;
            return false;
        }
#ifdef SO_REUSEPORT
        if(socketOptions.reuse_port&&setsockopt(fd,SOL_SOCKET,SO_REUSEPORT,&opt,sizeof(opt))<0)
        {
            cerr<<"set SO_REUSEPORT failed"<<endl;
            perror("setsockopt");
            ::close(fd);
            fd=-1;
            return false;
        }
#endif
#ifdef TCP_DEFER_ACCEPT
        if(socketOptions.defer_accept_seconds>0)
            (void)setsockopt(fd,IPPROTO_TCP,TCP_DEFER_ACCEPT,&socketOptions.defer_accept_seconds,
                             sizeof(socketOptions.defer_accept_seconds));
#endif
#ifdef TCP_FASTOPEN
        if(socketOptions.fast_open_queue>0)
            (void)setsockopt(fd,IPPROTO_TCP,TCP_FASTOPEN,&socketOptions.fast_open_queue,
                             sizeof(socketOptions.fast_open_queue));
#endif
        //bind
        struct sockaddr_in k;
        memset(&k,0,sizeof(k));
        k.sin_family=AF_INET;
        k.sin_port=htons(port);
        k.sin_addr.s_addr=htonl(INADDR_ANY);
        if(::bind(fd,(struct sockaddr*)&k,sizeof(k))!=0)
        {
            perror("bind");
            ::close(fd);
            fd=-1;
            return false;
        }
        sockaddr_in boundAddress{};
        socklen_t boundAddressLength=sizeof(boundAddress);
        if(getsockname(fd,reinterpret_cast<sockaddr*>(&boundAddress),&boundAddressLength)==0)
            this->port=ntohs(boundAddress.sin_port);
        else
            this->port=port;
        //listen
        uint64_t backlog=socketOptions.listen_backlog>0?
            static_cast<uint64_t>(socketOptions.listen_backlog):maxFD/50;
        //backlog = std::clamp(backlog, 128, 4096);
        if(backlog<128)
            backlog=128;
        else if(backlog>4096)
            backlog=4096;
        if(listen(fd,backlog)!=0)
        {
            perror("listen");
            ::close(fd);
            fd=-1;
            this->port=-1;
            return false;
        }
        //this->logfile=logfile;
        this->unblock=true;
        
        try
        {
            workpool=new WorkerPool(static_cast<size_t>(threads),maxPendingWorkerTasks);
        }
        catch (...)
        {
            ::close(fd);
            fd=-1;
            this->port=-1;
            return false;
        }
        //for(int sj=0;sj<threads;sj++)
        //    thread(&TcpServer::consumer,this,sj).detach();
        flag1.store(true, std::memory_order_release);
        flag2.store(false, std::memory_order_release);
        flag.store(true, std::memory_order_release);
        gracefulDrainRequested.store(false,std::memory_order_release);
        workerWakePending.store(false,std::memory_order_release);
        {
            std::lock_guard<std::mutex> lock(reactorStartupMutex);
            reactorStartupComplete=false;
            reactorStartupSuccess=false;
        }
        try
        {
            reactorThread=thread(&TcpServer::epolll,this,4096,fd);
        }
        catch (...)
        {
            flag1.store(false,std::memory_order_release);
            flag.store(false,std::memory_order_release);
            workpool->stop();
            delete workpool;
            workpool=nullptr;
            ::close(fd);
            fd=-1;
            this->port=-1;
            return false;
        }
        bool startupSuccess=false;
        {
            std::unique_lock<std::mutex> lock(reactorStartupMutex);
            reactorStartupCV.wait(lock,[this] {return reactorStartupComplete;});
            startupSuccess=reactorStartupSuccess;
        }
        if(!startupSuccess)
        {
            flag1.store(false,std::memory_order_release);
            if(reactorThread.joinable())
                reactorThread.join();
            workpool->stop();
            delete workpool;
            workpool=nullptr;
            const int eventFD=workerEventFD.exchange(-1,std::memory_order_acq_rel);
            if(eventFD>=0) ::close(eventFD);
            if(fd>=0) ::close(fd);
            fd=-1;
            this->port=-1;
            flag.store(false,std::memory_order_release);
            flag2.store(true,std::memory_order_release);
            return false;
        }
        //flag_detect=true;
        //thread(&security::ConnectionLimiter::connectionDetect,&connectionLimiter).detach();
        //this->consumerNum=threads;

        //this->logfile=logfile;
        return true;
    }
    bool stt::network::TcpServer::stopListen()
    {
        std::lock_guard<std::recursive_mutex> lifecycleLock(lifecycleMutex);
        if(!isListen()&&!reactorThread.joinable()&&workpool==nullptr&&fd<0&&
           workerEventFD.load(std::memory_order_acquire)<0)
        {
            return true;
        }
        if(reactorThread.joinable()&&reactorThread.get_id()==std::this_thread::get_id())
        {
            // Reactor 不能 join 自己；只提交异步停止请求，由拥有 Server 的控制线程随后完成 close()。
            flag.store(false,std::memory_order_release);
            gracefulDrainRequested.store(false,std::memory_order_release);
            flag1.store(false,std::memory_order_release);
            port=-1;
            notifyReactor();
            return false;
        }
        flag.store(false,std::memory_order_release);
        bool gracefulTimedOut=false;
        const bool canDrain=reactorThread.joinable()&&
            !flag2.load(std::memory_order_acquire)&&gracefulShutdownTimeoutMs>0;
        gracefulDrainRequested.store(canDrain,std::memory_order_release);
        const bool reactorOwnsListener=reactorThread.joinable()&&
            !flag2.load(std::memory_order_acquire);
        if(!reactorOwnsListener&&fd>=0)
        {
            shutdown(fd,SHUT_RDWR);
            ::close(fd);
            fd=-1;
        }
        port=-1;
        if(canDrain)
        {
            notifyReactor();
            std::unique_lock<std::mutex> lock(gracefulShutdownMutex);
            const bool drained=gracefulShutdownCV.wait_for(
                lock,std::chrono::milliseconds(gracefulShutdownTimeoutMs),[this] {
                    return metricActiveConnections.load(std::memory_order_acquire)==0;
                });
            if(!drained)
            {
                gracefulTimedOut=true;
                metricGracefulShutdownTimeouts.fetch_add(1,std::memory_order_relaxed);
            }
        }
        flag1.store(false,std::memory_order_release);
        notifyReactor();
        if(reactorThread.joinable())
            reactorThread.join();
        gracefulDrainRequested.store(false,std::memory_order_release);
        // Interrupt blocking socket/TLS operations before joining workers.
        // Resource ownership stays intact until WorkerPool has fully stopped.
        for(auto &entry:clientfd)
        {
            TcpFDInf &connection=entry.second;
            if(connection.fd>=0)
            {
                connection.closing=true;
                if(connection.write_state)
                {
                    std::lock_guard<std::mutex> lock(connection.write_state->mutex);
                    const size_t discardedBytes=connection.write_state->queued_bytes;
                    connection.write_state->closed=true;
                    connection.write_state->queue.clear();
                    connection.write_state->queued_bytes=0;
                    metricPendingWriteBytes.fetch_sub(discardedBytes,std::memory_order_relaxed);
                }
                shutdown(connection.fd,SHUT_RDWR);
            }
        }
        //for(int ii=0;ii<consumerNum;ii++)
        //    cv[ii].notify_all();
        //while(consumerNum!=0||!flag2)
        //{
        //    flag1=false;//不断提醒关闭
        //    for(int ii=0;ii<consumerNum;ii++)
        //        cv[ii].notify_all();
        //}
        if(workpool!=nullptr)
        {
            workpool->stop(!gracefulTimedOut);
            delete workpool;
            workpool=nullptr;
        }
        // Worker 已全部退出；即使发生排空超时，也可以安全完成底层资源释放。
        for(auto &entry:clientfd)
        {
            TcpFDInf &connection=entry.second;
            if(connection.fd>=0)
            {
                connection.active_workers=0;
                connection.closing=false;
                TcpServer::close(connection.fd);
            }
        }
        const int eventFD=workerEventFD.exchange(-1,std::memory_order_acq_rel);
        if(eventFD>=0)
            ::close(eventFD);
        workerWakePending.store(false,std::memory_order_release);
        // stopListen() 本身也允许同一个 Server 对象再次 startListen()。丢弃上一轮
        // Reactor/Worker 的代次消息，避免重启后处理已经失效的 fd 通知。
        clearReactorQueues();
        flag.store(false, std::memory_order_release);
        //关闭监听连接的消息活动的线程
        //do
        //{
        //    flag_detect=false;
        //}while(flag_detect_status);
        return true;
    }
    bool stt::network::TcpServer::close()
    {
        std::lock_guard<std::recursive_mutex> lifecycleLock(lifecycleMutex);
 
        if(isListen()||reactorThread.joinable()||workpool!=nullptr||fd>=0||
           workerEventFD.load(std::memory_order_acquire)>=0)
        {
            if(!stopListen())
                return false;
        }

        clearReactorQueues();
  
        //unique_lock<mutex> lock2(lc1);
        //unique_lock<mutex> lock1(ltl1);
        for(auto &entry:clientfd)
        {
            const int ii=entry.first;
            TcpFDInf &connection=entry.second;
            if(connection.fd!=-1)
            {
            onConnectionClosed(ii);

            if(connection.write_state)
            {
                std::lock_guard<std::mutex> lock(connection.write_state->mutex);
                const size_t discardedBytes=connection.write_state->queued_bytes;
                connection.write_state->closed=true;
                connection.write_state->queue.clear();
                connection.write_state->queued_bytes=0;
                metricPendingWriteBytes.fetch_sub(discardedBytes,std::memory_order_relaxed);
            }

            if(this->security_open)
                connectionLimiter.clearIP(connection.ip,ii);
            //auto jj=tlsfd.find(ii.first);

            if(connection.ssl!=nullptr)//这个套接字启用了tls
            {
                SSL_shutdown(connection.ssl);
                SSL_free(connection.ssl);
                connection.ssl=nullptr;
            }
  
            shutdown(ii,SHUT_RDWR);
            ::close(ii);
            metricActiveConnections.fetch_sub(1,std::memory_order_relaxed);
            metricClosedConnections.fetch_add(1,std::memory_order_relaxed);
            gracefulShutdownCV.notify_all();
            //clientfd[ii].fd=-1;
            //clientfd[ii].pendindQueue.clear();
            delete[] connection.buffer;
            connection.buffer=nullptr;
            connection.buffer_capacity=0;
            //delete clientfd[ii];
            }
        }
        //std::cout<<"tcp died"<<std::endl;
        clientfd.clear();
        {
            std::lock_guard<std::mutex> lock(writeRegistryMutex);
            writeRegistry.clear();
        }
        //delete[] fdQueue;
        //delete[] lq1;
        //delete[] cv;
        //clientfd.clear();
        //key.clear();
        //functionT.clear();
        //redrawTLS();
        return true;
    }
    bool stt::network::TcpServer::close(const int &fd)
    {
        if(reactorThread.joinable()&&reactorThread.get_id()!=std::this_thread::get_id())
        {
            std::shared_ptr<ConnectionWriteState> state;
            {
                std::lock_guard<std::mutex> lock(writeRegistryMutex);
                const auto stateIt=writeRegistry.find(fd);
                if(stateIt!=writeRegistry.end())
                    state=stateIt->second.lock();
            }
            if(!state)
                return false;
            requestQueuedClose(state);
            return true;
        }
       
        //unique_lock<mutex> lock2(lc1);
        //unique_lock<mutex> lock1(ltl1);
        //auto ii=clientfd.find(fd);
        
        auto connectionIt=clientfd.find(fd);
        if(fd<0||connectionIt==clientfd.end()||connectionIt->second.fd==-1)
        {
            
            return false;
        }
        else
        {
            
            TcpFDInf &connection=connectionIt->second;
            if(connection.closing)
                return true;
            {
                std::lock_guard<std::mutex> lock(writeRegistryMutex);
                writeRegistry.erase(fd);
            }
            if(connection.write_state)
            {
                std::lock_guard<std::mutex> lock(connection.write_state->mutex);
                const size_t discardedBytes=connection.write_state->queued_bytes;
                connection.write_state->closed=true;
                connection.write_state->queue.clear();
                connection.write_state->queued_bytes=0;
                metricPendingWriteBytes.fetch_sub(discardedBytes,std::memory_order_relaxed);
            }
            if(connection.active_workers>0)
            {
                connection.closing=true;
                shutdown(connection.fd,SHUT_RDWR);
                return true;
            }
            if(this->security_open)
                connectionLimiter.clearIP(connection.ip,connection.fd);
            
            //auto jj=tlsfd.find(fd);
            
            if(connection.ssl!=nullptr)
            {
                if(connection.tls_state != TLSState::HANDSHAKING)
                {
                    SSL_shutdown(connection.ssl);
                }
                SSL_free(connection.ssl);
                connection.ssl=nullptr;
            }
            
            const int closedFD=connection.fd;
            ::close(closedFD);
            metricActiveConnections.fetch_sub(1,std::memory_order_relaxed);
            metricClosedConnections.fetch_add(1,std::memory_order_relaxed);
            gracefulShutdownCV.notify_all();

            onConnectionClosed(closedFD);
            closeFun(closedFD);
            
            connection.fd=-1;
            
            delete[] connection.buffer;
            connection.buffer=nullptr;
            connection.buffer_capacity=0;
            
            connection.pendindQueue= std::queue<std::any>();
            
        }
        
        return true;
    }

    void stt::network::TcpServer::applyAcceptedSocketOptions(const int &acceptedFD) const noexcept
    {
        const int noDelay=socketOptions.tcp_no_delay?1:0;
        (void)setsockopt(acceptedFD,IPPROTO_TCP,TCP_NODELAY,&noDelay,sizeof(noDelay));
        const int keepAlive=socketOptions.keep_alive?1:0;
        (void)setsockopt(acceptedFD,SOL_SOCKET,SO_KEEPALIVE,&keepAlive,sizeof(keepAlive));
        if(socketOptions.receive_buffer_bytes>0)
            (void)setsockopt(acceptedFD,SOL_SOCKET,SO_RCVBUF,&socketOptions.receive_buffer_bytes,
                             sizeof(socketOptions.receive_buffer_bytes));
        if(socketOptions.send_buffer_bytes>0)
            (void)setsockopt(acceptedFD,SOL_SOCKET,SO_SNDBUF,&socketOptions.send_buffer_bytes,
                             sizeof(socketOptions.send_buffer_bytes));
#ifdef TCP_KEEPIDLE
        if(socketOptions.keep_alive&&socketOptions.keep_alive_idle_seconds>0)
            (void)setsockopt(acceptedFD,IPPROTO_TCP,TCP_KEEPIDLE,&socketOptions.keep_alive_idle_seconds,
                             sizeof(socketOptions.keep_alive_idle_seconds));
#endif
#ifdef TCP_KEEPINTVL
        if(socketOptions.keep_alive&&socketOptions.keep_alive_interval_seconds>0)
            (void)setsockopt(acceptedFD,IPPROTO_TCP,TCP_KEEPINTVL,&socketOptions.keep_alive_interval_seconds,
                             sizeof(socketOptions.keep_alive_interval_seconds));
#endif
#ifdef TCP_KEEPCNT
        if(socketOptions.keep_alive&&socketOptions.keep_alive_probe_count>0)
            (void)setsockopt(acceptedFD,IPPROTO_TCP,TCP_KEEPCNT,&socketOptions.keep_alive_probe_count,
                             sizeof(socketOptions.keep_alive_probe_count));
#endif
    }
    chrono::high_resolution_clock::time_point start;
    chrono::high_resolution_clock::time_point endd;
    chrono::microseconds duration;
    unsigned long op=0;
    int times=0;
    
    void stt::network::TcpServer::epolll(const int &evsNum,const int &listenFD)
    {
    
        int epollFD=epoll_create1(EPOLL_CLOEXEC);//创建epoll句柄
        if(epollFD<0)
        {
            reportReactorStartup(false);
            flag2.store(true, std::memory_order_release);
            flag.store(false, std::memory_order_release);
            return;
        }
        epoll_event ev;//epoll事件的数据结构
        ev.data.fd=listenFD;
        ev.events=EPOLLIN|EPOLLET;//边缘触发
        if(epoll_ctl(epollFD,EPOLL_CTL_ADD,listenFD,&ev)<0)
        {
            ::close(epollFD);
            reportReactorStartup(false);
            flag2.store(true, std::memory_order_release);
            flag.store(false, std::memory_order_release);
            return;
        }

        //加入worker线程fd
        const int eventFD=eventfd(0,EFD_NONBLOCK|EFD_CLOEXEC);
        workerEventFD.store(eventFD,std::memory_order_release);
        ev.events = EPOLLIN;
        ev.data.fd = eventFD;

        if(eventFD<0||epoll_ctl(epollFD,EPOLL_CTL_ADD,eventFD,&ev)<0)
        {
            workerEventFD.store(-1,std::memory_order_release);
            if(eventFD>=0) ::close(eventFD);
            ::close(epollFD);
            reportReactorStartup(false);
            flag2.store(true, std::memory_order_release);
            flag.store(false, std::memory_order_release);
            return;
        }
        if(hasPendingReactorWork())
            notifyReactor();

        //加入时间事件fd
        int hbTimerFD=-1;
        int securityTimerFD=-1;
        const auto failTimerSetup=[&]() {
            if(hbTimerFD>=0) ::close(hbTimerFD);
            if(securityTimerFD>=0) ::close(securityTimerFD);
            workerEventFD.store(-1,std::memory_order_release);
            ::close(eventFD);
            ::close(epollFD);
            reportReactorStartup(false);
            flag.store(false,std::memory_order_release);
            flag2.store(true,std::memory_order_release);
        };
        if(serverType==3)//加入websocket心跳时间事件
        {
            hbTimerFD = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
            itimerspec its{};
            its.it_interval.tv_sec = 30;   // 每 30 秒触发一次
            its.it_value.tv_sec    = 30;   // 首次 30 秒后触发
            if(hbTimerFD<0||timerfd_settime(hbTimerFD,0,&its,nullptr)<0)
            {
                failTimerSetup();
                return;
            }
            //丢进epoll
            epoll_event ev;
            ev.data.fd = hbTimerFD;
            ev.events  = EPOLLIN;
            if(epoll_ctl(epollFD,EPOLL_CTL_ADD,hbTimerFD,&ev)<0)
            {
                failTimerSetup();
                return;
            }
        }
        if(this->security_open && this->checkFrequency>0)//加入信息安全的时间事件
        {
            securityTimerFD = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
            itimerspec its{};
            // 将一次 O(n) 全表扫描拆成每秒一小批，避免在 Reactor 中制造周期性延迟尖峰。
            its.it_interval.tv_sec = 1;
            its.it_value.tv_sec    = 1;
            if(securityTimerFD<0||timerfd_settime(securityTimerFD,0,&its,nullptr)<0)
            {
                failTimerSetup();
                return;
            }
            //丢进epoll
            epoll_event ev;
            ev.data.fd = securityTimerFD;
            ev.events  = EPOLLIN;
            if(epoll_ctl(epollFD,EPOLL_CTL_ADD,securityTimerFD,&ev)<0)
            {
                failTimerSetup();
                return;
            }
        }


        //检查连接表里面是不是有套接字，有的话加入
       
        for(auto &entry:clientfd)
        {
            //unique_lock<mutex> lock6(lc1);
                TcpFDInf &connection=entry.second;
                if(connection.fd!=-1)
                {
                //cout<<"has:"<<connection.fd<<endl;
                ev.data.fd=connection.fd;
                
                    ev.events=EPOLLIN|EPOLLERR | EPOLLHUP | EPOLLRDHUP|EPOLLET;//边缘触发

                epoll_ctl(epollFD,EPOLL_CTL_ADD,connection.fd,&ev);//把事件放入epoll中
                }
        }
       //cout<<"ok"<<endl;
       
        const int maxEvents=evsNum<64?64:(evsNum>4096?4096:evsNum);
        std::vector<epoll_event> evs;
        try
        {
            evs.resize(static_cast<size_t>(maxEvents));//存放epoll返回的事件
        }
        catch (...)
        {
            if(hbTimerFD>=0) ::close(hbTimerFD);
            if(securityTimerFD>=0) ::close(securityTimerFD);
            workerEventFD.store(-1,std::memory_order_release);
            ::close(eventFD);
            ::close(epollFD);
            reportReactorStartup(false);
            flag.store(false,std::memory_order_release);
            flag2.store(true,std::memory_order_release);
            return;
        }
        reportReactorStartup(true);

        //用来accept的
        struct sockaddr_in k;
        socklen_t k_len=sizeof(k);
        //用来加密accept的
        SSL *ssl;
        if(stt::system::ServerSetting::logfile!=nullptr)
        {
            if(stt::system::ServerSetting::language=="Chinese")
                stt::system::ServerSetting::logfile->writeLog("tcp server epoll打开");
            else
                stt::system::ServerSetting::logfile->writeLog("tcp server epoll has opened");
        }
        
        int activeListenFD=listenFD;
        const auto stopAccepting=[this,epollFD,&activeListenFD]() {
            if(activeListenFD<0||flag.load(std::memory_order_acquire))
                return;
            (void)epoll_ctl(epollFD,EPOLL_CTL_DEL,activeListenFD,nullptr);
            shutdown(activeListenFD,SHUT_RDWR);
            ::close(activeListenFD);
            if(fd==activeListenFD)
                fd=-1;
            activeListenFD=-1;
        };

        while(flag1)
        {
            stopAccepting();
            //监听等待，一秒钟检查一次flag条件是否满足
            int infds=epoll_wait(epollFD,evs.data(),maxEvents,bufferedReadQueue.empty()?1000:0);
            if(infds<=0)//<0失败=0超时
            {
                if(infds<0&&errno!=EINTR&&flag1.load(std::memory_order_acquire))
                    perror("epoll_wait");
                advanceGracefulDrain();
                continue;
            }
            else//有事发生
            {
                stopAccepting();
                for(int ii=0;ii<infds;ii++)
                {
                    if(activeListenFD>=0&&evs[ii].data.fd==activeListenFD)//有新的连接
                    {
                        while(1)
                        {  

                            k_len = sizeof(k);
                            int cfd=accept4(activeListenFD,(struct sockaddr*)&k,&k_len,SOCK_NONBLOCK|SOCK_CLOEXEC);
                            if(cfd<0)
                            {
                                if(errno==EAGAIN||errno==EWOULDBLOCK)
                                    break;//全部连接都accept了
                                if(errno==EINTR)
                                    continue;
                                else//真的失败
                                {
                                    metricAcceptErrors.fetch_add(1,std::memory_order_relaxed);
                                    if(stt::system::ServerSetting::logfile!=nullptr)
                                    {
                                        if(stt::system::ServerSetting::language=="Chinese")
                                            stt::system::ServerSetting::logfile->writeLog("tcp server epoll:accept错误 error="+to_string(errno));
                                        else
                                            stt::system::ServerSetting::logfile->writeLog("tcp server epoll:accept failed error="+to_string(errno));
                                    }
                                    break;
                                }
                            }
                            string ip(inet_ntoa(k.sin_addr));//获取客户端的ip
                            if(metricActiveConnections.load(std::memory_order_relaxed)>=maxFD)
                            {
                                metricRejectedConnections.fetch_add(1,std::memory_order_relaxed);
                                ::close(cfd);
                                if(stt::system::ServerSetting::logfile!=nullptr)
                                {
                                        if(stt::system::ServerSetting::language=="Chinese")
                                            stt::system::ServerSetting::logfile->writeLog("tcp server epoll:fd="+to_string(cfd)+" ip="+ip+" ip连接数量达到系统上限，已经关闭这个连接");
                                        else
                                            stt::system::ServerSetting::logfile->writeLog("tcp server epoll:fd="+to_string(cfd)+" ip="+ip+" this connection has been closed because system connection has been reached limit");
                                }
                                continue;
                            }
                            applyAcceptedSocketOptions(cfd);
                            
                            
                            if(this->security_open)
                            {
                                int ret=connectionLimiter.allowConnect(ip,cfd,connectionTimes,connectionSecs);
                                if(ret==stt::security::DefenseDecision::CLOSE)
                                {
                                ::close(cfd);
                                if(stt::system::ServerSetting::logfile!=nullptr)
                                {
                                        if(stt::system::ServerSetting::language=="Chinese")
                                            stt::system::ServerSetting::logfile->writeLog("tcp server epoll:fd="+to_string(cfd)+" ip="+ip+" 此ip连接数量或者速度达到上限，已经关闭这个连接");
                                        else
                                            stt::system::ServerSetting::logfile->writeLog("tcp server epoll:fd="+to_string(cfd)+" ip="+ip+" this connection has been closed because this ip has reached connection num's or rate's limit");
                                }
                                continue;
                                }
                            }
                            TcpFDInf &connection=clientfd[cfd];
                            connection=TcpFDInf{};
                            {
                                // SSL_new() increments the context reference count. Keeping
                                // this short lock around the snapshot makes certificate reloads
                                // safe without moving SSL I/O out of the Reactor thread.
                                std::lock_guard<std::mutex> lock(tlsContextMutex);
                                ssl=(TLS&&ctx!=nullptr)?SSL_new(ctx):nullptr;
                                if(TLS&&ssl==nullptr)
                                {
                                    if(this->security_open) connectionLimiter.clearIP(ip,cfd);
                                    ::close(cfd);
                                    continue;
                                }
                            }
                            if(ssl!=nullptr)
                            {
                                SSL_set_accept_state(ssl);
                                SSL_set_fd(ssl,cfd);
                                connection.tls_state=TLSState::HANDSHAKING;
                            }
                            else
                                connection.tls_state=TLSState::NONE;
                            connection.ssl=ssl;
                            //cout<<"ok"<<endl;
                            //epoll注册
                            ev.data.fd=cfd;
                            
                                ev.events=EPOLLIN|EPOLLERR | EPOLLHUP | EPOLLRDHUP|EPOLLET;//边缘触发

                            if(epoll_ctl(epollFD,EPOLL_CTL_ADD,cfd,&ev)<0)
                            {
                                if(ssl!=nullptr) SSL_free(ssl);
                                connection.ssl=nullptr;
                                if(this->security_open) connectionLimiter.clearIP(ip,cfd);
                                ::close(cfd);
                                continue;
                            }
                            //cout<<"listen:"<<cfd<<endl;
                            //对象表注册
                            string port=to_string(ntohs(k.sin_port));//获取客户端的端口
                            //string ip(inet_ntoa(k.sin_addr));//获取客户端的ip
                            connection.fd=cfd;
                            connection.ip=ip;
                            connection.port=port;
                            connection.status=0;
                            connection.data="";
                            connection.buffer=nullptr;
                            connection.buffer_capacity=0;
                            connection.p_buffer_now=0;
                            connection.FDStatus=-1;
                            connection.connection_obj_fd=this->connection_obj_fd++;
                            connection.write_state=std::make_shared<ConnectionWriteState>();
                            connection.write_state->fd=cfd;
                            connection.write_state->connection_obj_fd=connection.connection_obj_fd;
                            connection.write_state->max_queued_bytes=maxPendingWriteBytes;
                            connection.write_interest=false;
                            connection.write_waiting_for_read=false;
                            if(this->security_open&&this->checkFrequency>0)
                                timeoutCandidates.push_back({cfd,connection.connection_obj_fd});
                            metricAcceptedConnections.fetch_add(1,std::memory_order_relaxed);
                            metricActiveConnections.fetch_add(1,std::memory_order_relaxed);
                            {
                                std::lock_guard<std::mutex> lock(writeRegistryMutex);
                                writeRegistry[cfd]=connection.write_state;
                            }
                            //clientfd[cfd].p_request_now=0;
                            //unique_lock<mutex> lock6(lc1);
                            //clientfd.emplace(cfd,inf);
                            
                            //lock6.unlock();
                            //写入日志
                            if(stt::system::ServerSetting::logfile!=nullptr)
                            {
                                if(stt::system::ServerSetting::language=="Chinese")
                                    stt::system::ServerSetting::logfile->writeLog("tcp server epoll:收到新的连接："+ip+":"+port+"存入fd= "+to_string(cfd));
                                else
                                    stt::system::ServerSetting::logfile->writeLog("tcp server epoll:has received a new connection: "+ip+":"+port+"save as fd= "+to_string(cfd));
                            }
                        }                                                
                    }
                    else if(evs[ii].data.fd==hbTimerFD)//websocket时间事件
                    {
                        uint64_t exp;
                        read(hbTimerFD, &exp, sizeof(exp)); // 必须读，清事件
                        if(!gracefulDrainRequested.load(std::memory_order_acquire))
                            handleHeartbeat();
                    }
                    else if(evs[ii].data.fd==securityTimerFD)//信息安全时间事件
                    {
                        uint64_t expirations=1;
                        (void)read(securityTimerFD,&expirations,sizeof(expirations));
                        const size_t candidateCount=timeoutCandidates.size();
                        if(candidateCount>0)
                        {
                            const size_t scanWindow=static_cast<size_t>(std::max(1,checkFrequency));
                            const size_t perTick=std::max<size_t>(64,(candidateCount+scanWindow-1)/scanWindow);
                            const size_t maxRounds=(candidateCount+perTick-1)/perTick;
                            const size_t rounds=static_cast<size_t>(std::min<uint64_t>(expirations,maxRounds));
                            const size_t scanBudget=std::min(candidateCount,perTick*std::max<size_t>(1,rounds));
                            for(size_t scanned=0;scanned<scanBudget;++scanned)
                            {
                                const SendReadyMessage candidate=timeoutCandidates.front();
                                timeoutCandidates.pop_front();
                                auto candidateIt=clientfd.find(candidate.fd);
                                if(candidateIt==clientfd.end()||candidateIt->second.fd!=candidate.fd||
                                   candidateIt->second.connection_obj_fd!=candidate.connection_obj_fd)
                                    continue;
                                TcpFDInf &connection=candidateIt->second;
                                metricIdleTimeoutChecks.fetch_add(1,std::memory_order_relaxed);
                                if(this->connectionLimiter.connectionDetect(connection.ip,candidate.fd))
                                {
                                    metricIdleTimeoutCloses.fetch_add(1,std::memory_order_relaxed);
                                    if(stt::system::ServerSetting::logfile!=nullptr)
                                    {
                                        if(stt::system::ServerSetting::language=="Chinese")
                                            stt::system::ServerSetting::logfile->writeLog("tcp server epoll:监测到僵尸连接：fd= "+to_string(candidate.fd)+" 已关闭");
                                        else
                                            stt::system::ServerSetting::logfile->writeLog("tcp server epoll : idle connection timed out: fd= "+to_string(candidate.fd)+" and it has been closed");
                                    }
                                    close(candidate.fd);
                                }
                                else
                                    timeoutCandidates.push_back(candidate);
                            }
                        }
                    }
                    else if(evs[ii].data.fd==eventFD)//worker事件
                    {
                        drainReactorWork(epollFD);
                    }
                    else//有数据上来了
                    {
                        auto eventConnection=clientfd.find(evs[ii].data.fd);
                        if(eventConnection==clientfd.end()||eventConnection->second.fd!=evs[ii].data.fd)
                            continue;
                       //start=chrono::high_resolution_clock::now();
                        if((evs[ii].events&(EPOLLERR | EPOLLHUP | EPOLLRDHUP))&&
                           !(evs[ii].events&(EPOLLIN|EPOLLOUT)))
                        {

                                
                                if(stt::system::ServerSetting::logfile!=nullptr)
                                {
                                    if(stt::system::ServerSetting::language=="Chinese")
                                        stt::system::ServerSetting::logfile->writeLog("tcp server epoll:收到连接关闭消息：fd= "+to_string(evs[ii].data.fd)+" 已关闭");
                                    else
                                        stt::system::ServerSetting::logfile->writeLog("tcp server epoll:receive connection close message: fd= "+to_string(evs[ii].data.fd)+" and it has been pushed into queue");
                                }
                                

                                close(evs[ii].data.fd);
                                //{
                                //std::lock_guard<std::mutex> lock(lq1[evs[ii].data.fd%consumerNum]);
                                //fdQueue[evs[ii].data.fd%consumerNum].push(QueueFD{evs[ii].data.fd,true});
                               //}
                                //cv[evs[ii].data.fd%consumerNum].notify_one();
                            

                        }
                        else
                        {
                            //tls状态
                            if (clientfd[evs[ii].data.fd].tls_state == TLSState::HANDSHAKING) 
                            {
                                //tls accept
                                int ret = SSL_accept(clientfd[evs[ii].data.fd].ssl);
                                if (ret == 1) 
                                {
                                    clientfd[evs[ii].data.fd].tls_state = TLSState::ESTABLISHED;
                                    {
                                        std::lock_guard<std::mutex> lock(writeRegistryMutex);
                                        writeRegistry[evs[ii].data.fd]=clientfd[evs[ii].data.fd].write_state;
                                    }
                                    epoll_event tlsEvent{};
                                    tlsEvent.data.fd=evs[ii].data.fd;
                                    tlsEvent.events=EPOLLIN|EPOLLERR|EPOLLHUP|EPOLLRDHUP|EPOLLET;
                                    epoll_ctl(epollFD,EPOLL_CTL_MOD,evs[ii].data.fd,&tlsEvent);
                                    // TLS 握手完成
                                    if(stt::system::ServerSetting::logfile!=nullptr)
                                    {
                                        if(stt::system::ServerSetting::language=="Chinese")
                                            stt::system::ServerSetting::logfile->writeLog("tcp server epoll:收到TLS握手数据：fd= "+to_string(evs[ii].data.fd)+",完成!");
                                        else
                                            stt::system::ServerSetting::logfile->writeLog("tcp server epoll:has received tls handshake data: fd= "+to_string(evs[ii].data.fd)+",finish!");
                                    }
                                } 
                                else 
                                {
                                    int err = SSL_get_error(clientfd[evs[ii].data.fd].ssl, ret);
                                    if (err == SSL_ERROR_WANT_READ) 
                                    {
                                        // 等下一个 EPOLLIN
                                        
                                    } 
                                    else if (err == SSL_ERROR_WANT_WRITE) 
                                    {
                                        epoll_event tlsEvent{};
                                        tlsEvent.data.fd=evs[ii].data.fd;
                                        tlsEvent.events=EPOLLIN|EPOLLOUT|EPOLLERR|EPOLLHUP|EPOLLRDHUP|EPOLLET;
                                        epoll_ctl(epollFD,EPOLL_CTL_MOD,evs[ii].data.fd,&tlsEvent);
                                    }
                                    else 
                                    {
                                        metricTLSHandshakeFailures.fetch_add(1,std::memory_order_relaxed);
                                        // 真错误
                                        //printf("SSL_accept error: %s\n", ERR_error_string(ERR_get_error(), nullptr));
    	                                //printf("SSL_accept returned %d, SSL error code: %d\n", ret, SSL_get_error(ssl, ret));
                                        //SSL_free(clientfd[evs[ii].data.fd].ssl);
                                        close(evs[ii].data.fd);
                                        if(stt::system::ServerSetting::logfile!=nullptr)
                                        {
                                            if(stt::system::ServerSetting::language=="Chinese")
                                                stt::system::ServerSetting::logfile->writeLog("tcp server epoll:TLS握手：fd= "+to_string(evs[ii].data.fd)+"错误");
                                            else
                                                stt::system::ServerSetting::logfile->writeLog("tcp server epoll: received tls handshake data: fd= "+to_string(evs[ii].data.fd)+" fail");
                                        }
                                    }
                                }
                                continue;
                            }
                            TcpFDInf &eventState=eventConnection->second;
                            if(evs[ii].events&EPOLLOUT)
                            {
                                const WriteFlushResult writeResult=flushConnectionWrites(eventState);
                                eventState.write_waiting_for_read=writeResult==WriteFlushResult::WaitRead;
                                if(writeResult==WriteFlushResult::Error)
                                {
                                    close(evs[ii].data.fd);
                                    continue;
                                }
                                if(!updateConnectionEvents(epollFD,eventState,writeResult==WriteFlushResult::WaitWrite))
                                {
                                    close(evs[ii].data.fd);
                                    continue;
                                }
                                if(writeResult==WriteFlushResult::Reschedule)
                                    publishSendReady(eventState.write_state);
                            }

                            if((evs[ii].events&EPOLLIN)&&
                               !gracefulDrainRequested.load(std::memory_order_acquire))
                            {
                                //普通数据
                                if(stt::system::ServerSetting::logfile!=nullptr)
                                {
                                    if(stt::system::ServerSetting::language=="Chinese")
                                        stt::system::ServerSetting::logfile->writeLog("tcp server epoll:收到新数据：fd= "+to_string(evs[ii].data.fd));
                                    else
                                        stt::system::ServerSetting::logfile->writeLog("tcp server epoll:has received new data: fd= "+to_string(evs[ii].data.fd));
                                }
                                handler_netevent(evs[ii].data.fd);
                            }

                            auto afterRead=clientfd.find(evs[ii].data.fd);
                            if(afterRead!=clientfd.end()&&afterRead->second.fd==evs[ii].data.fd&&afterRead->second.write_waiting_for_read)
                            {
                                TcpFDInf &connectionAfterRead=afterRead->second;
                                const WriteFlushResult writeResult=flushConnectionWrites(connectionAfterRead);
                                connectionAfterRead.write_waiting_for_read=writeResult==WriteFlushResult::WaitRead;
                                if(writeResult==WriteFlushResult::Error)
                                {
                                    close(evs[ii].data.fd);
                                    continue;
                                }
                                if(!updateConnectionEvents(epollFD,connectionAfterRead,writeResult==WriteFlushResult::WaitWrite))
                                {
                                    close(evs[ii].data.fd);
                                    continue;
                                }
                                if(writeResult==WriteFlushResult::Reschedule)
                                    publishSendReady(connectionAfterRead.write_state);
                            }
                             //{
                             //   std::lock_guard<std::mutex> lock(lq1[evs[ii].data.fd%consumerNum]);
                             //   fdQueue[evs[ii].data.fd%consumerNum].push(QueueFD{evs[ii].data.fd,false});
                             //}
                            //cv[evs[ii].data.fd%consumerNum].notify_one();
                        }
                        //endd=chrono::high_resolution_clock::now();
                        //duration=chrono::duration_cast<chrono::microseconds>(endd-start);
                        //cout<<"入队用时"<<duration.count()<<endl;
                    }
                }
                size_t continuationBudget=64;
                while(!gracefulDrainRequested.load(std::memory_order_acquire)&&
                      continuationBudget>0&&!bufferedReadQueue.empty())
                {
                    const SendReadyMessage continuation=bufferedReadQueue.front();
                    bufferedReadQueue.pop_front();
                    const auto continuationIt=clientfd.find(continuation.fd);
                    if(continuationIt!=clientfd.end()&&continuationIt->second.fd==continuation.fd&&
                       continuationIt->second.connection_obj_fd==continuation.connection_obj_fd)
                        handler_netevent(continuation.fd);
                    --continuationBudget;
                }
                if(gracefulDrainRequested.load(std::memory_order_acquire))
                    bufferedReadQueue.clear();
                advanceGracefulDrain();
            }
        }
        if(activeListenFD>=0)
        {
            (void)epoll_ctl(epollFD,EPOLL_CTL_DEL,activeListenFD,nullptr);
            shutdown(activeListenFD,SHUT_RDWR);
            ::close(activeListenFD);
            if(fd==activeListenFD)
                fd=-1;
        }
        if(hbTimerFD>=0) ::close(hbTimerFD);
        if(securityTimerFD>=0) ::close(securityTimerFD);
        // eventFD 的生命周期延长到 WorkerPool 停止之后，避免 Worker 在 Reactor
        // 退出窗口向已关闭且可能被复用的描述符写入。stopListen() 负责最终关闭。
        ::close(epollFD);
        if(stt::system::ServerSetting::logfile!=nullptr)
        {
            if(stt::system::ServerSetting::language=="Chinese")
                stt::system::ServerSetting::logfile->writeLog("tcp服务器监听的epoll退出");
            else
                stt::system::ServerSetting::logfile->writeLog("tcp server's listening epoll quit");
        }
        //cout<<"epoll quit"<<endl;
        flag2.store(true, std::memory_order_release);
    }
    void stt::network::TcpServer::handler_workerevent(WorkerMessage message)
    {
        const int fd=message.fd;
        const int ret=message.ret;
        auto connectionIt=clientfd.find(fd);
        if(connectionIt==clientfd.end()||connectionIt->second.fd!=fd||
           connectionIt->second.connection_obj_fd!=message.connection_obj_fd)
            return;
        if(connectionIt->second.active_workers>0)
            --connectionIt->second.active_workers;
        if(connectionIt->second.closing)
        {
            if(connectionIt->second.active_workers==0)
            {
                connectionIt->second.closing=false;
                TcpServer::close(fd);
            }
            return;
        }
        if(ret>=0&&message.request&&!connectionIt->second.pendindQueue.empty())
        {
            auto request=std::static_pointer_cast<TcpInformation>(message.request);
            std::any_cast<TcpInformation&>(connectionIt->second.pendindQueue.front())=std::move(*request);
        }
        if(ret==-2)
        {
            TcpServer::close(fd);
                    if(stt::system::ServerSetting::logfile!=nullptr)
                    {
                        if(stt::system::ServerSetting::language=="Chinese")
                            stt::system::ServerSetting::logfile->writeLog("tcp server : worker处理失败 fd= "+to_string(fd)+" ，已经关闭连接");
                        else
                            stt::system::ServerSetting::logfile->writeLog("tcp server : worker solve fail fd= "+to_string(fd)+" ,now has closed this connection");
                    }
            return;
        }
        TcpFDHandler k;
        prepareHandler(k,fd);
        if(ret==-1)
        {
            clientfd[fd].pendindQueue.pop();
            if(stt::system::ServerSetting::logfile!=nullptr)
                    {
                        if(stt::system::ServerSetting::language=="Chinese")
                            stt::system::ServerSetting::logfile->writeLog("tcp server : worker处理失败 fd= "+to_string(fd)+" ，跳过本次请求");
                        else
                            stt::system::ServerSetting::logfile->writeLog("tcp server : worker solve fail fd= "+to_string(fd)+" ,skip this request");
                    }
        }
        else
        {
            if(clientfd[fd].pendindQueue.empty())
            {
                if(stt::system::ServerSetting::logfile!=nullptr)
                {
                    if(stt::system::ServerSetting::language=="Chinese")
                        stt::system::ServerSetting::logfile->writeLog("tcp server : worker处理成功,但是上一个连接已经关闭，所以不予继续处理 fd= "+to_string(fd));
                    else
                        stt::system::ServerSetting::logfile->writeLog("tcp server : worker solve sucessfully.but the last connection has been closed so stop solving this request. fd= "+to_string(fd));
                }
                return;
            }
            TcpInformation &inf=std::any_cast<TcpInformation&>(clientfd[fd].pendindQueue.front());
            if(inf.connection_obj_fd!=clientfd[fd].connection_obj_fd)
            {
                if(stt::system::ServerSetting::logfile!=nullptr)
                {
                    if(stt::system::ServerSetting::language=="Chinese")
                        stt::system::ServerSetting::logfile->writeLog("tcp server : worker处理成功,但是上一个连接已经关闭，所以不予继续处理 fd= "+to_string(fd));
                    else
                        stt::system::ServerSetting::logfile->writeLog("tcp server : worker solve sucessfully.but the last connection has been closed so stop solving this request. fd= "+to_string(fd));
                }
                return;
            }
            auto ii=solveFun.find(std::any_cast<const std::string&>(inf.ctx["key"]));//对应的任务
            if(stt::system::ServerSetting::logfile!=nullptr)
            {
                if(stt::system::ServerSetting::language=="Chinese")
                    stt::system::ServerSetting::logfile->writeLog("tcp server : worker处理成功 fd= "+to_string(fd));
                else
                    stt::system::ServerSetting::logfile->writeLog("tcp server : worker solve sucessfully fd= "+to_string(fd));
            }
            if(ii==solveFun.end())//找不到
                {
                    //TcpServer::close(fd);
                    if(stt::system::ServerSetting::logfile!=nullptr)
                    {
                        if(stt::system::ServerSetting::language=="Chinese")
                            stt::system::ServerSetting::logfile->writeLog("tcp server : 找不到处理函数 fd= "+to_string(fd)+"。调用全局备用处理函数");
                        else
                            stt::system::ServerSetting::logfile->writeLog("tcp server : can not find solve function fd= "+to_string(fd)+" . use global backup slove function.");
                    }
                    if(!globalSolveFun(k,inf))
                    {
                        TcpServer::close(fd);
                        if(stt::system::ServerSetting::logfile!=nullptr)
                        {
                            if(stt::system::ServerSetting::language=="Chinese")
                                stt::system::ServerSetting::logfile->writeLog("tcp server : 调用全局备用函数失败 fd= "+to_string(fd)+"已经关闭连接.");
                            else
                                stt::system::ServerSetting::logfile->writeLog("tcp server : use global backup slove function fail. fd= "+to_string(fd)+". has closed this connection.");
                        }
                        return;
                    }
                    clientfd[fd].pendindQueue.pop();
                }
            else
            {
            //继续做
            for(;static_cast<size_t>(clientfd[fd].FDStatus)<ii->second.size();)
            {
                int rett=ii->second[clientfd[fd].FDStatus](k,inf);
                clientfd[fd].FDStatus++;
                        if(rett==1)
                        {
                            if(stt::system::ServerSetting::logfile!=nullptr)
                            {
                                if(stt::system::ServerSetting::language=="Chinese")
                                    stt::system::ServerSetting::logfile->writeLog("tcp server : 处理fd= "+to_string(fd)+" 第"+ to_string(clientfd[fd].FDStatus)+  "次完成");
                                else
                                    stt::system::ServerSetting::logfile->writeLog("tcp server : handled fd= "+to_string(fd)+" sucessfully.It's the "+to_string(clientfd[fd].FDStatus)+"times");
                            }
                        }
                        else if(rett==0)
                        {
                            if(stt::system::ServerSetting::logfile!=nullptr)
                            {
                                if(stt::system::ServerSetting::language=="Chinese")
                                    stt::system::ServerSetting::logfile->writeLog("tcp server : 处理fd= "+to_string(fd)+" 第"+ to_string(clientfd[fd].FDStatus)+  "次.等待任务完成.");
                                else
                                    stt::system::ServerSetting::logfile->writeLog("tcp server : handled fd= "+to_string(fd)+" .It's the "+to_string(clientfd[fd].FDStatus)+"times job. now is waitting it to be finish.");
                            }
                            return;
                        }
                        else
                        {
                            TcpServer::close(fd);
                            if(stt::system::ServerSetting::logfile!=nullptr)
                            {
                                if(stt::system::ServerSetting::language=="Chinese")
                                    stt::system::ServerSetting::logfile->writeLog("tcp server : 处理fd= "+to_string(fd)+" 第"+ to_string(clientfd[fd].FDStatus)+  "次失败。已经关闭连接。");
                                else
                                    stt::system::ServerSetting::logfile->writeLog("tcp server : handled fd= "+to_string(fd)+" fail.It's the "+to_string(clientfd[fd].FDStatus)+"times and now has closed this connection.");
                            }
                            //clientfd[fd].pendindQueue.pop();
                            return;
                        }
            }
            }
            clientfd[fd].pendindQueue.pop();
        }

        TcpFDInf &Tcpinf=clientfd[fd];
        //检查是否有下一轮
        if(Tcpinf.pendindQueue.size()>=1)//只有一个 说明没有任务没做完 直接执行
            {
                TcpInformation &inff=std::any_cast<TcpInformation&>(clientfd[fd].pendindQueue.front());
                Tcpinf.FDStatus=-1;
                int ret;
                //获取key,自动解析到ctx的key键
                ++Tcpinf.FDStatus;
                ret=parseKey(k,inff);

                if(ret==0) //慢任务
                    return;
                else if(ret<=-1)
                {
                    //清掉任务后返回
                    Tcpinf.pendindQueue.pop();
                    //-2要关闭连接
                    if(ret==-2)
                    {
                        TcpServer::close(fd);
                        if(stt::system::ServerSetting::logfile!=nullptr)
                        {
                            if(stt::system::ServerSetting::language=="Chinese")
                                stt::system::ServerSetting::logfile->writeLog("tcp server : parsekey的时候失败 fd= "+to_string(fd)+" ，已经关闭连接");
                            else
                                stt::system::ServerSetting::logfile->writeLog("tcp server : parsekey fail fd= "+to_string(fd)+",now has closed this connection");
                        }
                    }
                    else
                    {
                        if(stt::system::ServerSetting::logfile!=nullptr)
                        {
                            if(stt::system::ServerSetting::language=="Chinese")
                                stt::system::ServerSetting::logfile->writeLog("tcp server : parsekey的时候失败 fd= "+to_string(fd)+" ，已扔掉本次任务");
                            else
                                stt::system::ServerSetting::logfile->writeLog("tcp server : parsekey fail fd= "+to_string(fd)+",now has throwed this task");
                        }
                    }
                    return;
                }
                
                //遍历任务
                auto ii=solveFun.find(std::any_cast<const std::string&>(inff.ctx["key"]));
                if(ii==solveFun.end())//找不到
                {
                    //TcpServer::close(fd);
                    if(stt::system::ServerSetting::logfile!=nullptr)
                    {
                        if(stt::system::ServerSetting::language=="Chinese")
                            stt::system::ServerSetting::logfile->writeLog("tcp server : 找不到处理函数 fd= "+to_string(fd)+"。调用全局备用处理函数");
                        else
                            stt::system::ServerSetting::logfile->writeLog("tcp server : can not find solve function fd= "+to_string(fd)+" . use global backup slove function.");
                    }
                    if(!globalSolveFun(k,inff))
                    {
                        TcpServer::close(fd);
                        if(stt::system::ServerSetting::logfile!=nullptr)
                        {
                            if(stt::system::ServerSetting::language=="Chinese")
                                stt::system::ServerSetting::logfile->writeLog("tcp server : 调用全局备用函数失败 fd= "+to_string(fd)+"已经关闭连接.");
                            else
                                stt::system::ServerSetting::logfile->writeLog("tcp server : use global backup slove function fail. fd= "+to_string(fd)+". has closed this connection.");
                        }
                        return;
                    }
                    Tcpinf.pendindQueue.pop();
                }
                else//找得到处理函数
                {
                    for(auto &f:ii->second)
                    {
                        int rett=f(k,inff);
                        ++Tcpinf.FDStatus;
                        if(rett==1)
                        {
                            if(stt::system::ServerSetting::logfile!=nullptr)
                            {
                                if(stt::system::ServerSetting::language=="Chinese")
                                    stt::system::ServerSetting::logfile->writeLog("tcp server : 处理fd= "+to_string(fd)+" 第"+ to_string(Tcpinf.FDStatus)+  "次完成");
                                else
                                    stt::system::ServerSetting::logfile->writeLog("tcp server : handled fd= "+to_string(fd)+" sucessfully.It's the "+to_string(Tcpinf.FDStatus)+"times");
                            }
                        }
                        else if(rett==0)
                        {
                            if(stt::system::ServerSetting::logfile!=nullptr)
                            {
                                if(stt::system::ServerSetting::language=="Chinese")
                                    stt::system::ServerSetting::logfile->writeLog("tcp server : 处理fd= "+to_string(fd)+" 第"+ to_string(Tcpinf.FDStatus)+  "次.等待任务完成.");
                                else
                                    stt::system::ServerSetting::logfile->writeLog("tcp server : handled fd= "+to_string(fd)+" .It's the "+to_string(Tcpinf.FDStatus)+"times job. now is waitting it to be finish.");
                            }
                            return;
                        }
                        else if(rett==-1)
                        {
                            if(stt::system::ServerSetting::logfile!=nullptr)
                            {
                                if(stt::system::ServerSetting::language=="Chinese")
                                    stt::system::ServerSetting::logfile->writeLog("tcp server : 处理fd= "+to_string(fd)+" 第"+ to_string(Tcpinf.FDStatus)+  "次失败。");
                                else
                                    stt::system::ServerSetting::logfile->writeLog("tcp server : handled fd= "+to_string(fd)+" fail.It's the "+to_string(Tcpinf.FDStatus)+"times.");
                            }
                            //Tcpinf.pendindQueue.pop();
                            return;
                        }
                        else
                        {
                            TcpServer::close(fd);
                            if(stt::system::ServerSetting::logfile!=nullptr)
                            {
                                if(stt::system::ServerSetting::language=="Chinese")
                                    stt::system::ServerSetting::logfile->writeLog("tcp server : 处理fd= "+to_string(fd)+" 第"+ to_string(Tcpinf.FDStatus)+  "次失败。已经关闭连接。");
                                else
                                    stt::system::ServerSetting::logfile->writeLog("tcp server : handled fd= "+to_string(fd)+" fail.It's the "+to_string(Tcpinf.FDStatus)+"times and now has closed this connection.");
                            }
                            //Tcpinf.pendindQueue.pop();
                            return;
                        }
                    }
                    
                }
                Tcpinf.pendindQueue.pop();
                    if(stt::system::ServerSetting::logfile!=nullptr)
                    {
                        if(stt::system::ServerSetting::language=="Chinese")
                            stt::system::ServerSetting::logfile->writeLog("tcp server : 处理fd= "+to_string(fd)+" 完成");
                        else
                            stt::system::ServerSetting::logfile->writeLog("tcp server : handled fd= "+to_string(fd)+" sucessfully");
                    }
                
            }

    }
    static bool ensureReceiveBuffer(stt::network::TcpFDInf &connection,const unsigned long maxCapacity,const unsigned long requiredCapacity)
    {
        if(requiredCapacity>maxCapacity)
            return false;
        if(connection.buffer_capacity>=requiredCapacity)
            return true;
        unsigned long newCapacity=connection.buffer_capacity==0?std::min(8192UL,maxCapacity):connection.buffer_capacity;
        while(newCapacity<requiredCapacity)
            newCapacity=std::min(maxCapacity,newCapacity*2UL);
        char *newBuffer=new(std::nothrow) char[newCapacity];
        if(newBuffer==nullptr)
            return false;
        if(connection.buffer!=nullptr&&connection.p_buffer_now>0)
            memcpy(newBuffer,connection.buffer,connection.p_buffer_now);
        delete[] connection.buffer;
        connection.buffer=newBuffer;
        connection.buffer_capacity=newCapacity;
        return true;
    }

    void stt::network::TcpServer::handler_netevent(const int &fd)
    {
        TcpFDHandler k;
        TcpInformation inf;
        if(clientfd[fd].fd!=-1)//can not find fd information,we need to writedown this error and close this fd
        {
            
                if(stt::system::ServerSetting::logfile!=nullptr)
                {
                    if(stt::system::ServerSetting::language=="Chinese")
                        stt::system::ServerSetting::logfile->writeLog("tcp server : 正在处理fd= "+to_string(fd));
                    else
                        stt::system::ServerSetting::logfile->writeLog("tcp server : now handleing fd= "+to_string(fd));
                }
            
            TcpFDInf &Tcpinf=clientfd[fd];
            prepareHandler(k,fd);
            
            int ret=1;
            Tcpinf.p_buffer_now=0;
            if(!ensureReceiveBuffer(Tcpinf,buffer_size,std::min(8192UL,buffer_size)))
            {
                TcpServer::close(fd);
                return;
            }
            while(ret>0)
            {
                if(Tcpinf.p_buffer_now==Tcpinf.buffer_capacity)
                {
                    if(Tcpinf.buffer_capacity>=buffer_size||!ensureReceiveBuffer(Tcpinf,buffer_size,Tcpinf.buffer_capacity+1))
                        break;
                }
                ret=k.recvData(Tcpinf.buffer+Tcpinf.p_buffer_now,Tcpinf.buffer_capacity-Tcpinf.p_buffer_now);
                if(ret>0)
                    Tcpinf.p_buffer_now+=ret;
            }
            if(Tcpinf.p_buffer_now>=buffer_size)
            {
                TcpServer::close(fd);
                if(stt::system::ServerSetting::logfile!=nullptr)
                    {
                        if(stt::system::ServerSetting::language=="Chinese")
                            stt::system::ServerSetting::logfile->writeLog("tcp server : 缓冲区容量不足 读取数据fd= "+to_string(fd)+" 失败，已经关闭连接");
                        else
                            stt::system::ServerSetting::logfile->writeLog("tcp server : buffer size is not enough,read data from fd= "+to_string(fd)+" fail,now has closed this connection");
                    }
                return;
            }
            if(ret<=0)
            {
                if(ret!=-100)
                {
                    TcpServer::close(fd);
                    if(stt::system::ServerSetting::logfile!=nullptr)
                    {
                        if(stt::system::ServerSetting::language=="Chinese")
                            stt::system::ServerSetting::logfile->writeLog("tcp server : 读取数据fd= "+to_string(fd)+" 失败，已经关闭连接");
                        else
                            stt::system::ServerSetting::logfile->writeLog("tcp server : read data from fd= "+to_string(fd)+" fail,now has closed this connection");
                    }
                    return;
                }
            }
        //}
        
            inf.fd=fd;
            inf.connection_obj_fd=clientfd[fd].connection_obj_fd;
            inf.data=string(Tcpinf.buffer,Tcpinf.p_buffer_now);

            
            //lock6.unlock();
            //开始处理
                //入队
            
            Tcpinf.pendindQueue.push(std::move(inf));
            
            if(Tcpinf.pendindQueue.size()==1)//只有一个 说明没有任务没做完 直接执行
            {
                TcpInformation &inff=std::any_cast<TcpInformation&>(clientfd[fd].pendindQueue.front());
                Tcpinf.FDStatus=-1;
                int ret;
                //获取key,自动解析到ctx的key键
                ++Tcpinf.FDStatus;
                ret=parseKey(k,inff);
                
                if(ret==0) //慢任务
                {
                    return;
                }
                else if(ret<=-1)
                {
                    //清掉任务后返回
                    //Tcpinf.pendindQueue.pop();
                    //-2要关闭连接
                    if(ret==-2)
                    {
                        TcpServer::close(fd);
                        if(stt::system::ServerSetting::logfile!=nullptr)
                        {
                            if(stt::system::ServerSetting::language=="Chinese")
                                stt::system::ServerSetting::logfile->writeLog("tcp server : parsekey的时候失败 fd= "+to_string(fd)+" ，已经关闭连接");
                            else
                                stt::system::ServerSetting::logfile->writeLog("tcp server : parsekey fail fd= "+to_string(fd)+",now has closed this connection");
                        }
                    }
                    else
                    {
                        if(stt::system::ServerSetting::logfile!=nullptr)
                        {
                            if(stt::system::ServerSetting::language=="Chinese")
                                stt::system::ServerSetting::logfile->writeLog("tcp server : parsekey的时候失败 fd= "+to_string(fd)+" ，已扔掉本次任务");
                            else
                                stt::system::ServerSetting::logfile->writeLog("tcp server : parsekey fail fd= "+to_string(fd)+",now has throwed this task");
                        }
                    }
                    return;
                }
                if(security_open)
                {
                    int ret=connectionLimiter.allowRequest(clientfd[fd].ip,fd,std::any_cast<const std::string&>(inff.ctx["key"]),requestTimes,requestSecs);
                    if(ret!=stt::security::ALLOW)
                    {
                        securitySendBackFun(k,inff);
                        if(ret==stt::security::CLOSE)
                        {
                            TcpServer::close(fd);
                            if(stt::system::ServerSetting::logfile!=nullptr)
                            {
                                if(stt::system::ServerSetting::language=="Chinese")
                                    stt::system::ServerSetting::logfile->writeLog("tcp server : fd="+to_string(fd)+"请求太频繁，已经关闭连接");
                                else
                                    stt::system::ServerSetting::logfile->writeLog("tcp server : fd="+to_string(fd)+"request are too frequent,now has closed this connection");
                            }
                        }
                        else
                        {
                            if(stt::system::ServerSetting::logfile!=nullptr)
                            {
                                if(stt::system::ServerSetting::language=="Chinese")
                                    stt::system::ServerSetting::logfile->writeLog("tcp server : fd="+to_string(fd)+"请求太频繁，已经忽略请求");
                                else
                                    stt::system::ServerSetting::logfile->writeLog("tcp server : fd="+to_string(fd)+"request are too frequent,now has ignored this request");
                            }
                        }
                        return;
                    }
                }
                //遍历任务
                auto ii=solveFun.find(std::any_cast<const std::string&>(inff.ctx["key"]));
               
                if(ii==solveFun.end())//找不到
                {
                    
                    //TcpServer::close(fd);
                    if(stt::system::ServerSetting::logfile!=nullptr)
                    {
                        if(stt::system::ServerSetting::language=="Chinese")
                            stt::system::ServerSetting::logfile->writeLog("tcp server : 找不到处理函数 fd= "+to_string(fd)+"。调用全局备用处理函数");
                        else
                            stt::system::ServerSetting::logfile->writeLog("tcp server : can not find solve function fd= "+to_string(fd)+" . use global backup slove function.");
                    }
                    if(!globalSolveFun(k,inff))
                    {
                        TcpServer::close(fd);
                        if(stt::system::ServerSetting::logfile!=nullptr)
                        {
                            if(stt::system::ServerSetting::language=="Chinese")
                                stt::system::ServerSetting::logfile->writeLog("tcp server : 调用全局备用函数失败 fd= "+to_string(fd)+"已经关闭连接.");
                            else
                                stt::system::ServerSetting::logfile->writeLog("tcp server : use global backup slove function fail. fd= "+to_string(fd)+". has closed this connection.");
                        }
                        return;
                    }
                    Tcpinf.pendindQueue.pop();
                }
                else//找得到处理函数
                {
                    
                    for(auto &f:ii->second)
                    {
                        int rett=f(k,inff);
                        ++Tcpinf.FDStatus;
                        if(rett==1)
                        {
                            if(stt::system::ServerSetting::logfile!=nullptr)
                            {
                                if(stt::system::ServerSetting::language=="Chinese")
                                    stt::system::ServerSetting::logfile->writeLog("tcp server : 处理fd= "+to_string(fd)+" 第"+ to_string(Tcpinf.FDStatus)+  "次完成");
                                else
                                    stt::system::ServerSetting::logfile->writeLog("tcp server : handled fd= "+to_string(fd)+" sucessfully.It's the "+to_string(Tcpinf.FDStatus)+"times");
                            }
                        }
                        else if(rett==0)
                        {
                            if(stt::system::ServerSetting::logfile!=nullptr)
                            {
                                if(stt::system::ServerSetting::language=="Chinese")
                                    stt::system::ServerSetting::logfile->writeLog("tcp server : 处理fd= "+to_string(fd)+" 第"+ to_string(Tcpinf.FDStatus)+  "次.等待任务完成.");
                                else
                                    stt::system::ServerSetting::logfile->writeLog("tcp server : handled fd= "+to_string(fd)+" .It's the "+to_string(Tcpinf.FDStatus)+"times job. now is waitting it to be finish.");
                            }
                            return;
                        }
                        else if(rett==-1)
                        {
                        
                            if(stt::system::ServerSetting::logfile!=nullptr)
                            {
                                if(stt::system::ServerSetting::language=="Chinese")
                                    stt::system::ServerSetting::logfile->writeLog("tcp server : 处理fd= "+to_string(fd)+" 第"+ to_string(Tcpinf.FDStatus)+  "次失败。");
                                else
                                    stt::system::ServerSetting::logfile->writeLog("tcp server : handled fd= "+to_string(fd)+" fail.It's the "+to_string(Tcpinf.FDStatus)+"times.");
                            }
                            //Tcpinf.pendindQueue.pop();
                            return;
                        }
                        else
                        {
                            TcpServer::close(fd);
                            if(stt::system::ServerSetting::logfile!=nullptr)
                            {
                                if(stt::system::ServerSetting::language=="Chinese")
                                    stt::system::ServerSetting::logfile->writeLog("tcp server : 处理fd= "+to_string(fd)+" 第"+ to_string(Tcpinf.FDStatus)+  "次失败。已经关闭连接。");
                                else
                                    stt::system::ServerSetting::logfile->writeLog("tcp server : handled fd= "+to_string(fd)+" fail.It's the "+to_string(Tcpinf.FDStatus)+"times and now has closed this connection.");
                            }
                            //Tcpinf.pendindQueue.pop();
                            return;
                        }
                    }
                    Tcpinf.pendindQueue.pop();
                }

                    if(stt::system::ServerSetting::logfile!=nullptr)
                    {
                        if(stt::system::ServerSetting::language=="Chinese")
                            stt::system::ServerSetting::logfile->writeLog("tcp server : 处理fd= "+to_string(fd)+" 完成");
                        else
                            stt::system::ServerSetting::logfile->writeLog("tcp server : handled fd= "+to_string(fd)+" sucessfully");
                    }
                
            }
            
        }
    }
    /*
    void stt::network::TcpServer::consumer(const int &threadID)
    {
        TcpFDHandler k;

        if(stt::system::ServerSetting::logfile!=nullptr)
        {
            if(stt::system::ServerSetting::language=="Chinese")
            stt::system::ServerSetting::logfile->writeLog("tcp server consumer "+to_string(threadID)+" 打开");
            else
                stt::system::ServerSetting::logfile->writeLog("tcp server consumer "+to_string(threadID)+" 打开");
        }
        while(flag1)
        {
            unique_lock<mutex> ul1(lq1[threadID]);
            while(fdQueue[threadID].empty()&&flag1)
            {
                cv[threadID].wait(ul1);
            }
            if(!flag1)
            {
                ul1.unlock();
                break;
            }
            QueueFD cclientfd=fdQueue[threadID].front();
            fdQueue[threadID].pop();
            
            ul1.unlock();

            
            if(cclientfd.close)
            {
                TcpServer::close(cclientfd.fd);
                continue;
            }
            
            //unique_lock<mutex> lock6(lc1);
            //auto jj=clientfd.find(cclientfd.fd);
            if(clientfd[cclientfd.fd].fd==-1)//can not find fd information,we need to writedown this error and close this fd
            {
                //lock6.unlock();
                continue;
            }
            else
            {
                if(security_open)
                {
                    if(!connectionLimiter.allowRequest(clientfd[cclientfd.fd].ip))
                    {
                        TcpServer::close(cclientfd.fd);
                        if(stt::system::ServerSetting::logfile!=nullptr)
                        {
                            if(stt::system::ServerSetting::language=="Chinese")
                                stt::system::ServerSetting::logfile->writeLog("tcp server consumer "+to_string(threadID)+" : fd= "+to_string(cclientfd.fd)+"请求太频繁，已经关闭连接");
                            else
                                stt::system::ServerSetting::logfile->writeLog("tcp server consumer "+to_string(threadID)+" : fd= "+to_string(cclientfd.fd)+"request are too frequent,now has closed this connection");
                        }
                        continue;
                    }
                }
                if(stt::system::ServerSetting::logfile!=nullptr)
                {
                    if(stt::system::ServerSetting::language=="Chinese")
                        stt::system::ServerSetting::logfile->writeLog("tcp server consumer "+to_string(threadID)+" : 正在处理fd= "+to_string(cclientfd.fd));
                    else
                        stt::system::ServerSetting::logfile->writeLog("tcp server consumer "+to_string(threadID)+" :now handleing fd= "+to_string(cclientfd.fd));
                }
                prepareHandler(k,cclientfd.fd);
                TcpFDInf &Tcpinf=clientfd[cclientfd.fd];
            //lock6.unlock();
            
            if(!fc(k,Tcpinf))
            {
                TcpServer::close(cclientfd.fd);
                if(stt::system::ServerSetting::logfile!=nullptr)
                {
                    if(stt::system::ServerSetting::language=="Chinese")
                        stt::system::ServerSetting::logfile->writeLog("tcp server consumer"+to_string(threadID)+" : 处理fd= "+to_string(cclientfd.fd)+" 失败，已经关闭连接");
                    else
                        stt::system::ServerSetting::logfile->writeLog("tcp server consumer "+to_string(threadID)+" : handled fd= "+to_string(cclientfd.fd)+" fail,now has closed this connection");
                }
            }
            else
            {
                if(stt::system::ServerSetting::logfile!=nullptr)
                {
                    if(stt::system::ServerSetting::language=="Chinese")
                        stt::system::ServerSetting::logfile->writeLog("tcp server consumer"+to_string(threadID)+" : 处理fd= "+to_string(cclientfd.fd)+" 完成");
                    else
                        stt::system::ServerSetting::logfile->writeLog("tcp server consumer "+to_string(threadID)+" : handled fd= "+to_string(cclientfd.fd)+" sucessfully");
                }
            }
            }
            
        }
        //跳出循环意味着结束线程
        unique_lock<mutex> lock3(lco1);
        consumerNum--;
        lock3.unlock();
        cout<<"consumer "<<consumerNum<<" quit"<<endl;
        if(stt::system::ServerSetting::logfile!=nullptr)
        {
            if(stt::system::ServerSetting::language=="Chinese")
                stt::system::ServerSetting::logfile->writeLog("tcp server consumer "+to_string(consumerNum)+"退出");
            else
                stt::system::ServerSetting::logfile->writeLog("tcp server consumer "+to_string(consumerNum)+"quit");
        }
    }
    */
    bool stt::network::HttpServerFDHandler::sendBack(const string &data,const string &header,const string &code,const string &header1)
    {
        string result;
        result.reserve(data.size()+header.size()+header1.size()+96);
        result="HTTP/1.1 "+code+"\r\nContent-Length: "+to_string(data.length())+"\r\n";
        if(!header1.empty())
        {
            result+=header1;
            if(header1.size()<2||header1.compare(header1.size()-2,2,"\r\n")!=0)
                result+="\r\n";
        }
        if(!header.empty())
        {
            result+=header;
            if(header.size()<2||header.compare(header.size()-2,2,"\r\n")!=0)
                result+="\r\n";
        }
        result+="\r\n";
        result+=data;
        
        if(result.size()>static_cast<size_t>(std::numeric_limits<int>::max()))
            return false;
        return sendData(result)==static_cast<int>(result.size());
    }

    bool stt::network::HttpServerFDHandler::sendText(const string &data,const string &code,
                                                     const string &contentType,const string &extraHeaders)
    {
        if(contentType.empty()||contentType.find_first_of("\r\n")!=string::npos)
            return false;
        string headers="Content-Type: "+contentType;
        if(!extraHeaders.empty())
        {
            headers+="\r\n";
            headers+=extraHeaders;
        }
        return sendBack(data,headers,code);
    }

    bool stt::network::HttpServerFDHandler::sendJson(const Json::Value &value,const string &code,
                                                     const string &extraHeaders)
    {
        return sendText(stt::data::JsonHelper::toString(value),code,
                        "application/json; charset=utf-8",extraHeaders);
    }

    bool stt::network::HttpServerFDHandler::redirect(const string &location,const string &code)
    {
        if(location.empty()||location.find_first_of("\r\n")!=string::npos)
            return false;
        return sendBack("","Location: "+location,code);
    }
    
    bool stt::network::HttpServerFDHandler::sendBack(const char *data,const size_t &length,const char *header,const char *code,const char *header1,const size_t &header_length)
    {
        if(data==nullptr||header==nullptr||code==nullptr||header1==nullptr)
            return false;
        if(length>static_cast<size_t>(std::numeric_limits<int>::max()))
            return false;
        string result;
        const size_t headerReserve=std::min(header_length,1024UL*1024UL);
        result.reserve(length+headerReserve+96);
        result="HTTP/1.1 ";
        result+=code;
        result+="\r\nContent-Length: ";
        result+=to_string(length);
        result+="\r\n";
        const auto appendHeader=[&result](const char *value) {
            if(value[0]=='\0') return;
            const size_t headerSize=strlen(value);
            result.append(value,headerSize);
            if(headerSize<2||memcmp(value+headerSize-2,"\r\n",2)!=0)
                result+="\r\n";
        };
        appendHeader(header1);
        appendHeader(header);
        result+="\r\n";
        result.append(data,length);
        if(result.size()>static_cast<size_t>(std::numeric_limits<int>::max()))
            return false;
        return sendData(result)==static_cast<int>(result.size());
    }

    std::string_view stt::network::HttpRequestInformation::headerValue(const std::string_view name) const noexcept
    {
        if(name.empty()||name.find(':')!=std::string_view::npos)
            return {};
        const auto equalsIgnoreCase=[](const std::string_view left,const std::string_view right) {
            if(left.size()!=right.size()) return false;
            for(size_t index=0;index<left.size();++index)
            {
                if(std::tolower(static_cast<unsigned char>(left[index]))!=
                   std::tolower(static_cast<unsigned char>(right[index])))
                    return false;
            }
            return true;
        };
        const std::string_view raw(header);
        size_t lineStart=raw.find("\r\n");
        if(lineStart==std::string_view::npos)
            return {};
        lineStart+=2;
        while(lineStart<raw.size())
        {
            size_t lineEnd=raw.find("\r\n",lineStart);
            if(lineEnd==std::string_view::npos)
                lineEnd=raw.size();
            const std::string_view line=raw.substr(lineStart,lineEnd-lineStart);
            const size_t colon=line.find(':');
            if(colon!=std::string_view::npos&&equalsIgnoreCase(line.substr(0,colon),name))
            {
                std::string_view value=line.substr(colon+1);
                while(!value.empty()&&(value.front()==' '||value.front()=='\t')) value.remove_prefix(1);
                while(!value.empty()&&(value.back()==' '||value.back()=='\t')) value.remove_suffix(1);
                return value;
            }
            if(lineEnd==raw.size())
                break;
            lineStart=lineEnd+2;
        }
        return {};
    }

    std::string_view stt::network::HttpRequestInformation::bodyView() const noexcept
    {
        return body_chunked.empty()?std::string_view(body):std::string_view(body_chunked);
    }

    static int parseHttpRequestBuffer(stt::network::TcpFDInf &tcp,
                                      stt::network::HttpRequestInformation &request,
                                      const size_t bufferLimit,
                                      const size_t configuredHeaderLimit)
    {
        if(tcp.p_buffer_now==0)
            return 0;
        if(tcp.buffer==nullptr||tcp.p_buffer_now>bufferLimit)
            return -1;
        const std::string_view input(tcp.buffer,tcp.p_buffer_now);
        const size_t headerLimit=std::min(bufferLimit,configuredHeaderLimit);
        const size_t headerEnd=input.find("\r\n\r\n");
        if(headerEnd==std::string_view::npos)
            return input.size()>=headerLimit?-1:0;
        const size_t headerBytes=headerEnd+4;
        if(headerBytes>headerLimit)
            return -1;

        const auto isToken=[](const char character) {
            const unsigned char value=static_cast<unsigned char>(character);
            if(std::isalnum(value)) return true;
            switch(character)
            {
                case '!': case '#': case '$': case '%': case '&': case '\'':
                case '*': case '+': case '-': case '.': case '^': case '_':
                case '`': case '|': case '~': return true;
                default: return false;
            }
        };
        const auto trim=[](std::string_view value) {
            while(!value.empty()&&(value.front()==' '||value.front()=='\t')) value.remove_prefix(1);
            while(!value.empty()&&(value.back()==' '||value.back()=='\t')) value.remove_suffix(1);
            return value;
        };
        const auto equalsIgnoreCase=[](const std::string_view left,const std::string_view right) {
            if(left.size()!=right.size()) return false;
            for(size_t index=0;index<left.size();++index)
            {
                if(std::tolower(static_cast<unsigned char>(left[index]))!=
                   std::tolower(static_cast<unsigned char>(right[index])))
                    return false;
            }
            return true;
        };
        const auto parseDecimal=[](std::string_view value,size_t &result) {
            if(value.empty()) return false;
            result=0;
            for(const char character:value)
            {
                if(character<'0'||character>'9') return false;
                const size_t digit=static_cast<size_t>(character-'0');
                if(result>(std::numeric_limits<size_t>::max()-digit)/10) return false;
                result=result*10+digit;
            }
            return true;
        };
        const auto parseHex=[](std::string_view value,size_t &result) {
            if(value.empty()) return false;
            result=0;
            for(const char character:value)
            {
                unsigned int digit=0;
                if(character>='0'&&character<='9') digit=static_cast<unsigned int>(character-'0');
                else if(character>='a'&&character<='f') digit=static_cast<unsigned int>(character-'a'+10);
                else if(character>='A'&&character<='F') digit=static_cast<unsigned int>(character-'A'+10);
                else return false;
                if(result>(std::numeric_limits<size_t>::max()-digit)/16) return false;
                result=result*16+digit;
            }
            return true;
        };
        const auto validValue=[](const std::string_view value) {
            for(const char character:value)
            {
                const unsigned char byte=static_cast<unsigned char>(character);
                if((byte<0x20&&character!='\t')||byte==0x7f) return false;
            }
            return true;
        };

        const size_t requestLineEnd=input.find("\r\n");
        if(requestLineEnd==std::string_view::npos||requestLineEnd==0||requestLineEnd>headerEnd)
            return -1;
        const std::string_view requestLine=input.substr(0,requestLineEnd);
        const size_t firstSpace=requestLine.find(' ');
        const size_t secondSpace=firstSpace==std::string_view::npos?std::string_view::npos:requestLine.find(' ',firstSpace+1);
        if(firstSpace==std::string_view::npos||secondSpace==std::string_view::npos||
           firstSpace==0||secondSpace==firstSpace+1||requestLine.find(' ',secondSpace+1)!=std::string_view::npos)
            return -1;
        const std::string_view method=requestLine.substr(0,firstSpace);
        const std::string_view target=requestLine.substr(firstSpace+1,secondSpace-firstSpace-1);
        const std::string_view version=requestLine.substr(secondSpace+1);
        if(!std::all_of(method.begin(),method.end(),isToken)||target.empty()||
           (version!="HTTP/1.1"&&version!="HTTP/1.0"))
            return -1;
        for(const char character:target)
            if(static_cast<unsigned char>(character)<=0x20||character==0x7f)
                return -1;

        bool hasHost=false;
        bool hasContentLength=false;
        size_t contentLength=0;
        bool hasTransferEncoding=false;
        size_t lineStart=requestLineEnd+2;
        while(lineStart<headerEnd)
        {
            const size_t lineEnd=input.find("\r\n",lineStart);
            if(lineEnd==std::string_view::npos||lineEnd>headerEnd||lineEnd==lineStart)
                return -1;
            const std::string_view line=input.substr(lineStart,lineEnd-lineStart);
            if(line.front()==' '||line.front()=='\t')
                return -1;
            const size_t colon=line.find(':');
            if(colon==std::string_view::npos||colon==0)
                return -1;
            const std::string_view name=line.substr(0,colon);
            const std::string_view value=trim(line.substr(colon+1));
            if(!std::all_of(name.begin(),name.end(),isToken)||!validValue(value))
                return -1;
            if(equalsIgnoreCase(name,"host"))
            {
                if(hasHost||value.empty()) return -1;
                hasHost=true;
            }
            else if(equalsIgnoreCase(name,"content-length"))
            {
                size_t parsedLength=0;
                if(hasContentLength||!parseDecimal(value,parsedLength)) return -1;
                hasContentLength=true;
                contentLength=parsedLength;
            }
            else if(equalsIgnoreCase(name,"transfer-encoding"))
            {
                if(hasTransferEncoding) return -1;
                hasTransferEncoding=true;
                if(value.find(',')!=std::string_view::npos||!equalsIgnoreCase(trim(value),"chunked"))
                    return -1;
            }
            lineStart=lineEnd+2;
        }
        if(version=="HTTP/1.1"&&!hasHost)
            return -1;
        if(hasContentLength&&hasTransferEncoding)
            return -1;

        size_t consumed=headerBytes;
        std::string decodedBody;
        if(hasTransferEncoding)
        {
            size_t cursor=headerBytes;
            while(true)
            {
                const size_t sizeLineEnd=input.find("\r\n",cursor);
                if(sizeLineEnd==std::string_view::npos)
                    return input.size()>=bufferLimit?-1:0;
                std::string_view sizeText=input.substr(cursor,sizeLineEnd-cursor);
                if(!validValue(sizeText)) return -1;
                const size_t extension=sizeText.find(';');
                if(extension!=std::string_view::npos) sizeText=sizeText.substr(0,extension);
                sizeText=trim(sizeText);
                size_t chunkSize=0;
                if(!parseHex(sizeText,chunkSize)||decodedBody.size()>bufferLimit||chunkSize>bufferLimit-decodedBody.size())
                    return -1;
                cursor=sizeLineEnd+2;
                if(chunkSize==0)
                {
                    while(true)
                    {
                        const size_t trailerEnd=input.find("\r\n",cursor);
                        if(trailerEnd==std::string_view::npos)
                            return input.size()>=bufferLimit?-1:0;
                        if(trailerEnd==cursor)
                        {
                            consumed=cursor+2;
                            break;
                        }
                        const std::string_view trailer=input.substr(cursor,trailerEnd-cursor);
                        const size_t colon=trailer.find(':');
                        if(colon==std::string_view::npos||colon==0||
                           !std::all_of(trailer.begin(),trailer.begin()+static_cast<std::ptrdiff_t>(colon),isToken)||
                           !validValue(trim(trailer.substr(colon+1))))
                            return -1;
                        cursor=trailerEnd+2;
                    }
                    break;
                }
                if(chunkSize>input.size()-cursor||input.size()-cursor-chunkSize<2)
                    return input.size()>=bufferLimit?-1:0;
                if(input.substr(cursor+chunkSize,2)!="\r\n")
                    return -1;
                decodedBody.append(input.data()+cursor,chunkSize);
                cursor+=chunkSize+2;
            }
        }
        else if(hasContentLength)
        {
            if(headerBytes>bufferLimit||contentLength>bufferLimit-headerBytes)
                return -1;
            consumed=headerBytes+contentLength;
            if(input.size()<consumed)
                return input.size()>=bufferLimit?-1:0;
        }

        request.type.assign(method);
        request.locPara.assign(target);
        HttpStringUtil::get_location_str(request.locPara,request.loc);
        HttpStringUtil::getPara(request.locPara,request.para);
        request.header.assign(input.data(),headerEnd);
        request.body.clear();
        request.body_chunked.clear();
        request.ctx.clear();
        if(hasTransferEncoding)
            request.body_chunked=std::move(decodedBody);
        else if(hasContentLength&&contentLength>0)
            request.body.assign(input.data()+headerBytes,contentLength);

        const size_t remaining=input.size()-consumed;
        if(remaining>0)
            memmove(tcp.buffer,tcp.buffer+consumed,remaining);
        tcp.p_buffer_now=remaining;
        tcp.data={};
        tcp.status=0;
        return 1;
    }

    int stt::network::HttpServerFDHandler::solveRequest(TcpFDInf &TcpInf,HttpRequestInformation &HttpInf,const unsigned long &buffer_size,const int &times,const unsigned long &max_header_size)
    {
        if(buffer_size==0||max_header_size==0)
            return -1;
        int receiveResult=-100;
        if(times==1)
        {
            if(!ensureReceiveBuffer(TcpInf,buffer_size,std::min(8192UL,buffer_size)))
                return -1;
            receiveResult=1;
            while(receiveResult>0)
            {
                if(TcpInf.p_buffer_now==TcpInf.buffer_capacity)
                {
                    if(TcpInf.buffer_capacity>=buffer_size||
                       !ensureReceiveBuffer(TcpInf,buffer_size,TcpInf.buffer_capacity+1))
                        break;
                }
                receiveResult=recvData(TcpInf.buffer+TcpInf.p_buffer_now,TcpInf.buffer_capacity-TcpInf.p_buffer_now);
                if(receiveResult>0)
                    TcpInf.p_buffer_now+=static_cast<unsigned long>(receiveResult);
            }
        }
        if(TcpInf.p_buffer_now==0&&receiveResult!=-100)
            return -1;
        return parseHttpRequestBuffer(TcpInf,HttpInf,buffer_size,max_header_size);


    }
    void stt::network::HttpServer::handler_netevent(const int &fd)
    {
        HttpServerFDHandler k;
        //HttpRequestInformation inf;
        if(clientfd[fd].fd!=-1)//can not find fd information,we need to writedown this error and close this fd
        {
            
                if(stt::system::ServerSetting::logfile!=nullptr)
                {
                    if(stt::system::ServerSetting::language=="Chinese")
                        stt::system::ServerSetting::logfile->writeLog("http server : 正在处理fd= "+to_string(fd));
                    else
                        stt::system::ServerSetting::logfile->writeLog("http server : now handleing fd= "+to_string(fd));
                }
            TcpFDInf &Tcpinf=clientfd[fd];
            prepareHandler(k,fd);
            
            int ret=1;
            httpinf[fd].fd=fd;
            httpinf[fd].connection_obj_fd=clientfd[fd].connection_obj_fd;
            
            ret=k.solveRequest(Tcpinf,httpinf[fd],buffer_size,1,maxHttpHeaderBytes);
            
            if(ret==-1)
            {
                    TcpServer::close(fd);
                    if(stt::system::ServerSetting::logfile!=nullptr)
                    {
                        if(stt::system::ServerSetting::language=="Chinese")
                            stt::system::ServerSetting::logfile->writeLog("http server : 读取数据fd= "+to_string(fd)+" 失败，已经关闭连接");
                        else
                            stt::system::ServerSetting::logfile->writeLog("http server : read data from fd= "+to_string(fd)+" fail,now has closed this connection");
                    }
                    return;
            }
            else if(ret==1)
            {
                metricParsedHttpRequests.fetch_add(1,std::memory_order_relaxed);
                
                if(stt::system::ServerSetting::logfile!=nullptr)
                {
                    if(stt::system::ServerSetting::language=="Chinese")
                    {
                        if(httpinf[fd].body.length()<=1024*10 && httpinf[fd].body_chunked.length()<=1024*10)
                            stt::system::ServerSetting::logfile->writeLog("http server consumer  : fd= "+to_string(fd)+" 读取数据完成。 \n*******请求信息：*********\nheader= "+string(httpinf[fd].header)+"\nbody="+string(httpinf[fd].body)+"\nbody_chunked="+string(httpinf[fd].body_chunked)+"\n*************************");
                        else
                            stt::system::ServerSetting::logfile->writeLog("http server consumer  : fd= "+to_string(fd)+" 读取数据完成。 \n*******请求信息：*********\nheader= "+string(httpinf[fd].header)+"\nbody,body_chunked= ... \n*************************");
                    }
                    else
                    {
                        if(httpinf[fd].body.length()<=1024*10 && httpinf[fd].body_chunked.length()<=1024*10)
                            stt::system::ServerSetting::logfile->writeLog("http server consumer  : fd= "+to_string(fd)+" now has solved request.\n*******request information：*********\nheader= "+string(httpinf[fd].header)+"\nbody="+string(httpinf[fd].body)+"\nbody_chunked="+string(httpinf[fd].body_chunked)+"\n*************************");
                        else
                            stt::system::ServerSetting::logfile->writeLog("http server consumer  : fd= "+to_string(fd)+" now has solved request.\n*******request information：*********\nheader= "+string(httpinf[fd].header)+"\nbody,body_chunked= ... \n*************************");
                    }

                }
                
            }
            else if(ret==0)
            {
                    if(stt::system::ServerSetting::logfile!=nullptr)
                    {
                        if(stt::system::ServerSetting::language=="Chinese")
                            stt::system::ServerSetting::logfile->writeLog("http server consumer  : 解析fd= "+to_string(fd)+"未完成 等待新的数据继续解析");
                        else
                            stt::system::ServerSetting::logfile->writeLog("http server consumer  : fd= "+to_string(fd)+"wait new data to continue solve this request");
                    }
                    return;
            }
        
            

            
            //lock6.unlock();
            //开始处理
                //入队
            Tcpinf.pendindQueue.push(std::move(httpinf[fd]));
            
            if(Tcpinf.pendindQueue.size()==1)//只有一个 说明没有任务没做完 直接执行
            {
                HttpRequestInformation &inff=std::any_cast<HttpRequestInformation&>(clientfd[fd].pendindQueue.front());
                Tcpinf.FDStatus=-1;
                int ret;
                //获取key,自动解析到ctx的key键
                ++Tcpinf.FDStatus;
                ret=parseKey(k,inff);
    
                if(ret==0)//慢处理
                {
                    return;
                }
                else if(ret<=-1)
                {
                    //清掉任务后返回
                    //Tcpinf.pendindQueue.pop();
                    //-2要关闭连接
                    if(ret==-2)
                    {
                        k.sendBack("","","404 NOT FOUND");
                        TcpServer::close(fd);
                        if(stt::system::ServerSetting::logfile!=nullptr)
                        {
                            if(stt::system::ServerSetting::language=="Chinese")
                                stt::system::ServerSetting::logfile->writeLog("http server : parsekey的时候失败 fd= "+to_string(fd)+" ，已经关闭连接");
                            else
                                stt::system::ServerSetting::logfile->writeLog("http server : parsekey fail fd= "+to_string(fd)+",now has closed this connection");
                        }
                    }
                    else
                    {
                        k.sendBack("","","404 NOT FOUND");
                        if(stt::system::ServerSetting::logfile!=nullptr)
                        {
                            if(stt::system::ServerSetting::language=="Chinese")
                                stt::system::ServerSetting::logfile->writeLog("http server : parsekey的时候失败 fd= "+to_string(fd)+" ，已扔掉本次任务并且发回错误信息");
                            else
                                    stt::system::ServerSetting::logfile->writeLog("http server : parsekey fail fd= "+to_string(fd)+",now has throwed this task and send back error message");
                        }
                        Tcpinf.pendindQueue.pop();
                        if(Tcpinf.p_buffer_now>0)
                            scheduleBufferedRead(fd,Tcpinf.connection_obj_fd);
                    }
                    return;
                }
                if(security_open)
                {
                    int ret=connectionLimiter.allowRequest(clientfd[fd].ip,fd,std::any_cast<const std::string&>(inff.ctx["key"]),requestTimes,requestSecs);
                    if(ret!=stt::security::ALLOW)
                    {
                        securitySendBackFun(k,inff);
                        if(ret==stt::security::CLOSE)
                        {
                            TcpServer::close(fd);
                            if(stt::system::ServerSetting::logfile!=nullptr)
                            {
                                if(stt::system::ServerSetting::language=="Chinese")
                                    stt::system::ServerSetting::logfile->writeLog("http server : fd="+to_string(fd)+"请求太频繁，已经关闭连接");
                                else
                                    stt::system::ServerSetting::logfile->writeLog("http server : fd="+to_string(fd)+"request are too frequent,now has closed this connection");
                            }
                        }
                        else
                        {
                            if(stt::system::ServerSetting::logfile!=nullptr)
                            {
                                if(stt::system::ServerSetting::language=="Chinese")
                                    stt::system::ServerSetting::logfile->writeLog("http server : fd="+to_string(fd)+"请求太频繁，已经忽略请求");
                                else
                                    stt::system::ServerSetting::logfile->writeLog("http server : fd="+to_string(fd)+"request are too frequent,now has ignored this request");
                            }
                            Tcpinf.pendindQueue.pop();
                            if(Tcpinf.p_buffer_now>0)
                                scheduleBufferedRead(fd,Tcpinf.connection_obj_fd);
                        }
                        return;
                    }
                }
                //遍历任务
                
                auto ii=solveFun.find(std::any_cast<const std::string&>(inff.ctx["key"]));
                
                if(ii==solveFun.end())//找不到
                {
                    if(globalSolveFun.size()==0)//连全局处理函数都没有 只能发404
                    {
                        if(stt::system::ServerSetting::logfile!=nullptr)
                        {
                            if(stt::system::ServerSetting::language=="Chinese")
                                stt::system::ServerSetting::logfile->writeLog("http server : 找不到处理函数也找不到全局处理函数 fd= "+to_string(fd)+"发送404 not found.");
                            else
                                stt::system::ServerSetting::logfile->writeLog("http server : can not find solve function and global function. fd= "+to_string(fd)+". has sent 404 not found.");
                        }
                        if(!k.sendBack("","","404 NOT FOUND"))
                        {
                            TcpServer::close(fd);
                            if(stt::system::ServerSetting::logfile!=nullptr)
                            {
                                if(stt::system::ServerSetting::language=="Chinese")
                                    stt::system::ServerSetting::logfile->writeLog("http server : 发送404 not found失败 fd= "+to_string(fd)+"已经关闭连接.");
                                else
                                    stt::system::ServerSetting::logfile->writeLog("http server : sending 404 not found fail. fd= "+to_string(fd)+". has closed this connection.");
                            }
                            return;
                        }
                    }
                    else
                    {
                    
                        if(stt::system::ServerSetting::logfile!=nullptr)
                        {
                            if(stt::system::ServerSetting::language=="Chinese")
                                stt::system::ServerSetting::logfile->writeLog("http server : 找不到处理函数 fd= "+to_string(fd)+"。调用全局备用处理函数");
                            else
                                stt::system::ServerSetting::logfile->writeLog("http server : can not find solve function fd= "+to_string(fd)+" . use global backup slove function.");
                        }
                        for(auto &f:globalSolveFun)
                        {
                            int rett=f(k,inff);
                            ++Tcpinf.FDStatus;
                            if(rett==1)
                            {
                                if(stt::system::ServerSetting::logfile!=nullptr)
                                {
                                    if(stt::system::ServerSetting::language=="Chinese")
                                        stt::system::ServerSetting::logfile->writeLog("http server : 处理fd= "+to_string(fd)+" 第"+ to_string(Tcpinf.FDStatus)+  "次完成");
                                    else
                                        stt::system::ServerSetting::logfile->writeLog("http server : handled fd= "+to_string(fd)+" sucessfully.It's the "+to_string(Tcpinf.FDStatus)+"times");
                                }
                            }
                            else if(rett==0)
                            {
                                if(stt::system::ServerSetting::logfile!=nullptr)
                                {
                                    if(stt::system::ServerSetting::language=="Chinese")
                                        stt::system::ServerSetting::logfile->writeLog("http server : 处理fd= "+to_string(fd)+" 第"+ to_string(Tcpinf.FDStatus)+  "次.等待任务完成.");
                                    else
                                        stt::system::ServerSetting::logfile->writeLog("http server : handled fd= "+to_string(fd)+" .It's the "+to_string(Tcpinf.FDStatus)+"times job. now is waitting it to be finish.");
                                }
                            
                                return;
                            }
                            else if(rett==-1)
                            {
                            
                                if(stt::system::ServerSetting::logfile!=nullptr)
                                {
                                    if(stt::system::ServerSetting::language=="Chinese")
                                        stt::system::ServerSetting::logfile->writeLog("http server : 处理fd= "+to_string(fd)+" 第"+ to_string(Tcpinf.FDStatus)+  "次失败。");
                                    else
                                        stt::system::ServerSetting::logfile->writeLog("http server : handled fd= "+to_string(fd)+" fail.It's the "+to_string(Tcpinf.FDStatus)+"times.");
                                }
                                Tcpinf.pendindQueue.pop();
                                if(Tcpinf.p_buffer_now>0)
                                    scheduleBufferedRead(fd,Tcpinf.connection_obj_fd);
                                return;
                            }
                            else
                            {
                                TcpServer::close(fd);
                                if(stt::system::ServerSetting::logfile!=nullptr)
                                {
                                    if(stt::system::ServerSetting::language=="Chinese")
                                        stt::system::ServerSetting::logfile->writeLog("http server : 处理fd= "+to_string(fd)+" 第"+ to_string(Tcpinf.FDStatus)+  "次失败。已经关闭连接。");
                                    else
                                        stt::system::ServerSetting::logfile->writeLog("http server : handled fd= "+to_string(fd)+" fail.It's the "+to_string(Tcpinf.FDStatus)+"times and now has closed this connection.");
                                }
                                //Tcpinf.pendindQueue.pop();
                                return;
                            }
                        }
                    }
                    clientfd[fd].pendindQueue.pop();
                }
                else//找得到处理函数
                {
                    for(auto &f:ii->second)
                    {
                        int rett=f(k,inff);
                        ++Tcpinf.FDStatus;
                        if(rett==1)
                        {
                            if(stt::system::ServerSetting::logfile!=nullptr)
                            {
                                if(stt::system::ServerSetting::language=="Chinese")
                                    stt::system::ServerSetting::logfile->writeLog("http server : 处理fd= "+to_string(fd)+" 第"+ to_string(Tcpinf.FDStatus)+  "次完成");
                                else
                                    stt::system::ServerSetting::logfile->writeLog("http server : handled fd= "+to_string(fd)+" sucessfully.It's the "+to_string(Tcpinf.FDStatus)+"times");
                            }
                        }
                        else if(rett==0)
                        {
                            if(stt::system::ServerSetting::logfile!=nullptr)
                            {
                                if(stt::system::ServerSetting::language=="Chinese")
                                    stt::system::ServerSetting::logfile->writeLog("http server : 处理fd= "+to_string(fd)+" 第"+ to_string(Tcpinf.FDStatus)+  "次.等待任务完成.");
                                else
                                    stt::system::ServerSetting::logfile->writeLog("http server : handled fd= "+to_string(fd)+" .It's the "+to_string(Tcpinf.FDStatus)+"times job. now is waitting it to be finish.");
                            }
                            
                            return;
                        }
                        else if(rett==-1)
                        {
                            
                            if(stt::system::ServerSetting::logfile!=nullptr)
                            {
                                if(stt::system::ServerSetting::language=="Chinese")
                                    stt::system::ServerSetting::logfile->writeLog("http server : 处理fd= "+to_string(fd)+" 第"+ to_string(Tcpinf.FDStatus)+  "次失败。");
                                else
                                    stt::system::ServerSetting::logfile->writeLog("http server : handled fd= "+to_string(fd)+" fail.It's the "+to_string(Tcpinf.FDStatus)+"times.");
                            }
                            Tcpinf.pendindQueue.pop();
                            if(Tcpinf.p_buffer_now>0)
                                scheduleBufferedRead(fd,Tcpinf.connection_obj_fd);
                            return;
                        }
                        else
                        {
                            TcpServer::close(fd);
                            if(stt::system::ServerSetting::logfile!=nullptr)
                            {
                                if(stt::system::ServerSetting::language=="Chinese")
                                    stt::system::ServerSetting::logfile->writeLog("http server : 处理fd= "+to_string(fd)+" 第"+ to_string(Tcpinf.FDStatus)+  "次失败。已经关闭连接。");
                                else
                                    stt::system::ServerSetting::logfile->writeLog("http server : handled fd= "+to_string(fd)+" fail.It's the "+to_string(Tcpinf.FDStatus)+"times and now has closed this connection.");
                            }
                            //Tcpinf.pendindQueue.pop();
                            return;
                        }
                    }
                    Tcpinf.pendindQueue.pop();
                }

                    if(stt::system::ServerSetting::logfile!=nullptr)
                    {
                        if(stt::system::ServerSetting::language=="Chinese")
                            stt::system::ServerSetting::logfile->writeLog("http server : 处理fd= "+to_string(fd)+" 完成");
                        else
                            stt::system::ServerSetting::logfile->writeLog("http server : handled fd= "+to_string(fd)+" sucessfully");
                    }
                
            }
            if(Tcpinf.fd==fd&&Tcpinf.pendindQueue.empty()&&Tcpinf.p_buffer_now>0)
                scheduleBufferedRead(fd,Tcpinf.connection_obj_fd);
        }
    }
    bool stt::network::HttpServer::close()
    {
        const bool result=TcpServer::close();
        httpinf.clear();
        return result;
    }

    bool stt::network::HttpServer::close(const int &fd)
    {
        httpinf.erase(fd);
        return TcpServer::close(fd);
    }

     void stt::network::HttpServer::handler_workerevent(WorkerMessage message)
    {
        const int fd=message.fd;
        const int ret=message.ret;
        auto connectionIt=clientfd.find(fd);
        if(connectionIt==clientfd.end()||connectionIt->second.fd!=fd||
           connectionIt->second.connection_obj_fd!=message.connection_obj_fd)
            return;
        if(connectionIt->second.active_workers>0)
            --connectionIt->second.active_workers;
        if(connectionIt->second.closing)
        {
            if(connectionIt->second.active_workers==0)
            {
                connectionIt->second.closing=false;
                TcpServer::close(fd);
            }
            return;
        }
        if(ret>=0&&message.request&&!connectionIt->second.pendindQueue.empty())
        {
            auto request=std::static_pointer_cast<HttpRequestInformation>(message.request);
            std::any_cast<HttpRequestInformation&>(connectionIt->second.pendindQueue.front())=std::move(*request);
        }
        if(ret==-2)
        {
            TcpServer::close(fd);
                    if(stt::system::ServerSetting::logfile!=nullptr)
                    {
                        if(stt::system::ServerSetting::language=="Chinese")
                            stt::system::ServerSetting::logfile->writeLog("http server : worker处理失败 fd= "+to_string(fd)+" ，已经关闭连接");
                        else
                            stt::system::ServerSetting::logfile->writeLog("http server : worker solve fail fd= "+to_string(fd)+" ,now has closed this connection");
                    }
            return;
        }
        HttpServerFDHandler k;
        prepareHandler(k,fd);
        
        if(ret==-1)
        {
            clientfd[fd].pendindQueue.pop();
            if(stt::system::ServerSetting::logfile!=nullptr)
                    {
                        if(stt::system::ServerSetting::language=="Chinese")
                            stt::system::ServerSetting::logfile->writeLog("http server : worker处理失败 fd= "+to_string(fd)+" ，跳过本次请求");
                        else
                            stt::system::ServerSetting::logfile->writeLog("http server : worker solve fail fd= "+to_string(fd)+" ,skip this request");
                    }
        }
        else
        {
            if(clientfd[fd].pendindQueue.empty())
            {
                if(stt::system::ServerSetting::logfile!=nullptr)
                {
                    if(stt::system::ServerSetting::language=="Chinese")
                        stt::system::ServerSetting::logfile->writeLog("http server : worker处理成功,但是上一个连接已经关闭，所以不予继续处理 fd= "+to_string(fd));
                    else
                        stt::system::ServerSetting::logfile->writeLog("http server : worker solve sucessfully.but the last connection has been closed so stop solving this request. fd= "+to_string(fd));
                }
                return;
            }
            HttpRequestInformation &inf=std::any_cast<HttpRequestInformation&>(clientfd[fd].pendindQueue.front());
            if(inf.connection_obj_fd!=clientfd[fd].connection_obj_fd)
            {
                if(stt::system::ServerSetting::logfile!=nullptr)
                {
                    if(stt::system::ServerSetting::language=="Chinese")
                        stt::system::ServerSetting::logfile->writeLog("http server : worker处理成功,但是上一个连接已经关闭，所以不予继续处理 fd= "+to_string(fd));
                    else
                        stt::system::ServerSetting::logfile->writeLog("http server : worker solve sucessfully.but the last connection has been closed so stop solving this request. fd= "+to_string(fd));
                }
                return;
            }
            
            auto ii=solveFun.find(std::any_cast<const std::string&>(inf.ctx["key"]));//对应的任务
            if(stt::system::ServerSetting::logfile!=nullptr)
            {
                if(stt::system::ServerSetting::language=="Chinese")
                    stt::system::ServerSetting::logfile->writeLog("http server : worker处理成功 fd= "+to_string(fd));
                else
                    stt::system::ServerSetting::logfile->writeLog("http server : worker solve sucessfully fd= "+to_string(fd));
            }
            if(ii==solveFun.end())//找不到
            {
                    
                    if(globalSolveFun.size()==0)//连全局处理函数都没有 只能发404
                    {
                        if(stt::system::ServerSetting::logfile!=nullptr)
                        {
                            if(stt::system::ServerSetting::language=="Chinese")
                                stt::system::ServerSetting::logfile->writeLog("http server : 找不到处理函数也找不到全局处理函数 fd= "+to_string(fd)+"发送404 not found.");
                            else
                                stt::system::ServerSetting::logfile->writeLog("http server : can not find solve function and global function. fd= "+to_string(fd)+". has sent 404 not found.");
                        }
                        if(!k.sendBack("","","404 NOT FOUND"))
                        {
                            TcpServer::close(fd);
                            if(stt::system::ServerSetting::logfile!=nullptr)
                            {
                                if(stt::system::ServerSetting::language=="Chinese")
                                    stt::system::ServerSetting::logfile->writeLog("http server : 发送404 not found失败 fd= "+to_string(fd)+"已经关闭连接.");
                                else
                                    stt::system::ServerSetting::logfile->writeLog("http server : sending 404 not found fail. fd= "+to_string(fd)+". has closed this connection.");
                            }
                            return;
                        }
                    }
                    else
                    {
                    
                        if(stt::system::ServerSetting::logfile!=nullptr)
                        {
                            if(stt::system::ServerSetting::language=="Chinese")
                                stt::system::ServerSetting::logfile->writeLog("http server : 找不到处理函数 fd= "+to_string(fd)+"。调用全局备用处理函数");
                            else
                                stt::system::ServerSetting::logfile->writeLog("http server : can not find solve function fd= "+to_string(fd)+" . use global backup slove function.");
                        }
                        for(;static_cast<size_t>(clientfd[fd].FDStatus)<globalSolveFun.size();)
                        {
                            //继续做
                            int rett=globalSolveFun[clientfd[fd].FDStatus](k,inf);
                            clientfd[fd].FDStatus++;
                            if(rett==1)
                            {
                                if(stt::system::ServerSetting::logfile!=nullptr)
                                {
                                    if(stt::system::ServerSetting::language=="Chinese")
                                        stt::system::ServerSetting::logfile->writeLog("http server : 处理fd= "+to_string(fd)+" 第"+ to_string(clientfd[fd].FDStatus)+  "次完成");
                                    else
                                        stt::system::ServerSetting::logfile->writeLog("http server : handled fd= "+to_string(fd)+" sucessfully.It's the "+to_string(clientfd[fd].FDStatus)+"times");
                                }
                            }
                            else if(rett==0)
                            {
                                if(stt::system::ServerSetting::logfile!=nullptr)
                                {
                                    if(stt::system::ServerSetting::language=="Chinese")
                                        stt::system::ServerSetting::logfile->writeLog("http server : 处理fd= "+to_string(fd)+" 第"+ to_string(clientfd[fd].FDStatus)+  "次.等待任务完成.");
                                    else
                                        stt::system::ServerSetting::logfile->writeLog("http server : handled fd= "+to_string(fd)+" .It's the "+to_string(clientfd[fd].FDStatus)+"times job. now is waitting it to be finish.");
                                }
                                return;
                            }
                            else
                            {
                                TcpServer::close(fd);
                                if(stt::system::ServerSetting::logfile!=nullptr)
                                {
                                    if(stt::system::ServerSetting::language=="Chinese")
                                        stt::system::ServerSetting::logfile->writeLog("http server : 处理fd= "+to_string(fd)+" 第"+ to_string(clientfd[fd].FDStatus)+  "次失败。已经关闭连接。");
                                    else
                                        stt::system::ServerSetting::logfile->writeLog("http server : handled fd= "+to_string(fd)+" fail.It's the "+to_string(clientfd[fd].FDStatus)+"times and now has closed this connection.");
                                }
                                //clientfd[fd].pendindQueue.pop();
                                return;
                            }
                        }
                    }
                    clientfd[fd].pendindQueue.pop();
            }
            else
            {
            //继续做
            
            for(;static_cast<size_t>(clientfd[fd].FDStatus)<ii->second.size();)
            {
                
                int rett=ii->second[clientfd[fd].FDStatus](k,inf);
                clientfd[fd].FDStatus++;
                        if(rett==1)
                        {
                            if(stt::system::ServerSetting::logfile!=nullptr)
                            {
                                if(stt::system::ServerSetting::language=="Chinese")
                                    stt::system::ServerSetting::logfile->writeLog("http server : 处理fd= "+to_string(fd)+" 第"+ to_string(clientfd[fd].FDStatus)+  "次完成");
                                else
                                    stt::system::ServerSetting::logfile->writeLog("http server : handled fd= "+to_string(fd)+" sucessfully.It's the "+to_string(clientfd[fd].FDStatus)+"times");
                            }
                        }
                        else if(rett==0)
                        {
                            if(stt::system::ServerSetting::logfile!=nullptr)
                            {
                                if(stt::system::ServerSetting::language=="Chinese")
                                    stt::system::ServerSetting::logfile->writeLog("http server : 处理fd= "+to_string(fd)+" 第"+ to_string(clientfd[fd].FDStatus)+  "次.等待任务完成.");
                                else
                                    stt::system::ServerSetting::logfile->writeLog("http server : handled fd= "+to_string(fd)+" .It's the "+to_string(clientfd[fd].FDStatus)+"times job. now is waitting it to be finish.");
                            }
                            return;
                        }
                        else
                        {
                            TcpServer::close(fd);
                            if(stt::system::ServerSetting::logfile!=nullptr)
                            {
                                if(stt::system::ServerSetting::language=="Chinese")
                                    stt::system::ServerSetting::logfile->writeLog("http server : 处理fd= "+to_string(fd)+" 第"+ to_string(clientfd[fd].FDStatus)+  "次失败。已经关闭连接。");
                                else
                                    stt::system::ServerSetting::logfile->writeLog("http server : handled fd= "+to_string(fd)+" fail.It's the "+to_string(clientfd[fd].FDStatus)+"times and now has closed this connection.");
                            }
                            //clientfd[fd].pendindQueue.pop();
                            return;
                        }
            }
            
            clientfd[fd].pendindQueue.pop();
            }
        }
     
        TcpFDInf &Tcpinf=clientfd[fd];
        //检查是否有下一轮
        if(Tcpinf.pendindQueue.size()>=1)//只有一个 说明没有任务没做完 直接执行
            {
                HttpRequestInformation &inff=std::any_cast<HttpRequestInformation&>(clientfd[fd].pendindQueue.front());
                Tcpinf.FDStatus=-1;
                int ret;
                //获取key,自动解析到ctx的key键
                ++Tcpinf.FDStatus;
                ret=parseKey(k,inff);

                if(ret==0)//慢处理
                    return;
                else if(ret<=-1)
                {
                    //清掉任务后返回
                    Tcpinf.pendindQueue.pop();
                    //-2要关闭连接
                    if(ret==-2)
                    {
                        k.sendBack("","","404 NOT FOUND");
                        TcpServer::close(fd);
                        if(stt::system::ServerSetting::logfile!=nullptr)
                        {
                            if(stt::system::ServerSetting::language=="Chinese")
                                stt::system::ServerSetting::logfile->writeLog("http server : parsekey的时候失败 fd= "+to_string(fd)+" ，已经关闭连接");
                            else
                                stt::system::ServerSetting::logfile->writeLog("http server : parsekey fail fd= "+to_string(fd)+",now has closed this connection");
                        }
                    }
                    else
                    {
                        k.sendBack("","","404 NOT FOUND");
                        if(stt::system::ServerSetting::logfile!=nullptr)
                        {
                            if(stt::system::ServerSetting::language=="Chinese")
                                stt::system::ServerSetting::logfile->writeLog("http server : parsekey的时候失败 fd= "+to_string(fd)+" ，已扔掉本次任务并且发回错误信息");
                            else
                                stt::system::ServerSetting::logfile->writeLog("http server : parsekey fail fd= "+to_string(fd)+",now has throwed this task and send back error message");
                        }
                    }
                    return;
                }
                
                //遍历任务
                auto ii=solveFun.find(std::any_cast<const std::string&>(inff.ctx["key"]));
                if(ii==solveFun.end())//找不到
                {
                    if(globalSolveFun.size()==0)//连全局处理函数都没有 只能发404
                    {
                        if(stt::system::ServerSetting::logfile!=nullptr)
                        {
                            if(stt::system::ServerSetting::language=="Chinese")
                                stt::system::ServerSetting::logfile->writeLog("http server : 找不到处理函数也找不到全局处理函数 fd= "+to_string(fd)+"发送404 not found.");
                            else
                                stt::system::ServerSetting::logfile->writeLog("http server : can not find solve function and global function. fd= "+to_string(fd)+". has sent 404 not found.");
                        }
                        if(!k.sendBack("","","404 NOT FOUND"))
                        {
                            TcpServer::close(fd);
                            if(stt::system::ServerSetting::logfile!=nullptr)
                            {
                                if(stt::system::ServerSetting::language=="Chinese")
                                    stt::system::ServerSetting::logfile->writeLog("http server : 发送404 not found失败 fd= "+to_string(fd)+"已经关闭连接.");
                                else
                                    stt::system::ServerSetting::logfile->writeLog("http server : sending 404 not found fail. fd= "+to_string(fd)+". has closed this connection.");
                            }
                            return;
                        }
                    }
                    else
                    {
                    
                        if(stt::system::ServerSetting::logfile!=nullptr)
                        {
                            if(stt::system::ServerSetting::language=="Chinese")
                                stt::system::ServerSetting::logfile->writeLog("http server : 找不到处理函数 fd= "+to_string(fd)+"。调用全局备用处理函数");
                            else
                                stt::system::ServerSetting::logfile->writeLog("http server : can not find solve function fd= "+to_string(fd)+" . use global backup slove function.");
                        }
                        for(auto &f:globalSolveFun)
                        {
                            int rett=f(k,inff);
                            ++Tcpinf.FDStatus;
                            if(rett==1)
                            {
                                if(stt::system::ServerSetting::logfile!=nullptr)
                                {
                                    if(stt::system::ServerSetting::language=="Chinese")
                                        stt::system::ServerSetting::logfile->writeLog("http server : 处理fd= "+to_string(fd)+" 第"+ to_string(Tcpinf.FDStatus)+  "次完成");
                                    else
                                        stt::system::ServerSetting::logfile->writeLog("http server : handled fd= "+to_string(fd)+" sucessfully.It's the "+to_string(Tcpinf.FDStatus)+"times");
                                }
                            }
                            else if(rett==0)
                            {
                                if(stt::system::ServerSetting::logfile!=nullptr)
                                {
                                    if(stt::system::ServerSetting::language=="Chinese")
                                        stt::system::ServerSetting::logfile->writeLog("http server : 处理fd= "+to_string(fd)+" 第"+ to_string(Tcpinf.FDStatus)+  "次.等待任务完成.");
                                    else
                                        stt::system::ServerSetting::logfile->writeLog("http server : handled fd= "+to_string(fd)+" .It's the "+to_string(Tcpinf.FDStatus)+"times job. now is waitting it to be finish.");
                                }
                            
                                return;
                            }
                            else if(rett==-1)
                            {
                            
                                if(stt::system::ServerSetting::logfile!=nullptr)
                                {
                                    if(stt::system::ServerSetting::language=="Chinese")
                                        stt::system::ServerSetting::logfile->writeLog("http server : 处理fd= "+to_string(fd)+" 第"+ to_string(Tcpinf.FDStatus)+  "次失败。");
                                    else
                                        stt::system::ServerSetting::logfile->writeLog("http server : handled fd= "+to_string(fd)+" fail.It's the "+to_string(Tcpinf.FDStatus)+"times.");
                                }
                                //Tcpinf.pendindQueue.pop();
                                return;
                            }
                            else
                            {
                                TcpServer::close(fd);
                                if(stt::system::ServerSetting::logfile!=nullptr)
                                {
                                    if(stt::system::ServerSetting::language=="Chinese")
                                        stt::system::ServerSetting::logfile->writeLog("http server : 处理fd= "+to_string(fd)+" 第"+ to_string(Tcpinf.FDStatus)+  "次失败。已经关闭连接。");
                                    else
                                        stt::system::ServerSetting::logfile->writeLog("http server : handled fd= "+to_string(fd)+" fail.It's the "+to_string(Tcpinf.FDStatus)+"times and now has closed this connection.");
                                }
                                //Tcpinf.pendindQueue.pop();
                                return;
                            }
                        }
                    }
                    clientfd[fd].pendindQueue.pop();
                }
                else//找得到处理函数
                {
                    for(auto &f:ii->second)
                    {
                        int rett=f(k,inff);
                        ++Tcpinf.FDStatus;
                        if(rett==1)
                        {
                            if(stt::system::ServerSetting::logfile!=nullptr)
                            {
                                if(stt::system::ServerSetting::language=="Chinese")
                                    stt::system::ServerSetting::logfile->writeLog("http server : 处理fd= "+to_string(fd)+" 第"+ to_string(Tcpinf.FDStatus)+  "次完成");
                                else
                                    stt::system::ServerSetting::logfile->writeLog("http server : handled fd= "+to_string(fd)+" sucessfully.It's the "+to_string(Tcpinf.FDStatus)+"times");
                            }
                        }
                        else if(rett==0)
                        {
                            if(stt::system::ServerSetting::logfile!=nullptr)
                            {
                                if(stt::system::ServerSetting::language=="Chinese")
                                    stt::system::ServerSetting::logfile->writeLog("http server : 处理fd= "+to_string(fd)+" 第"+ to_string(Tcpinf.FDStatus)+  "次.等待任务完成.");
                                else
                                    stt::system::ServerSetting::logfile->writeLog("http server : handled fd= "+to_string(fd)+" .It's the "+to_string(Tcpinf.FDStatus)+"times job. now is waitting it to be finish.");
                            }
                            return;
                        }
                        else if(rett==-1)
                        {
                            if(stt::system::ServerSetting::logfile!=nullptr)
                            {
                                if(stt::system::ServerSetting::language=="Chinese")
                                    stt::system::ServerSetting::logfile->writeLog("http server : 处理fd= "+to_string(fd)+" 第"+ to_string(Tcpinf.FDStatus)+  "次失败。");
                                else
                                    stt::system::ServerSetting::logfile->writeLog("http server : handled fd= "+to_string(fd)+" fail.It's the "+to_string(Tcpinf.FDStatus)+"times.");
                            }
                            //Tcpinf.pendindQueue.pop();
                            return;
                        }
                        else
                        {
                            TcpServer::close(fd);
                            if(stt::system::ServerSetting::logfile!=nullptr)
                            {
                                if(stt::system::ServerSetting::language=="Chinese")
                                    stt::system::ServerSetting::logfile->writeLog("http server : 处理fd= "+to_string(fd)+" 第"+ to_string(Tcpinf.FDStatus)+  "次失败。已经关闭连接。");
                                else
                                    stt::system::ServerSetting::logfile->writeLog("http server : handled fd= "+to_string(fd)+" fail.It's the "+to_string(Tcpinf.FDStatus)+"times and now has closed this connection.");
                            }
                            //Tcpinf.pendindQueue.pop();
                            return;
                        }
                    }
                    
                }
                Tcpinf.pendindQueue.pop();

                    if(stt::system::ServerSetting::logfile!=nullptr)
                    {
                        if(stt::system::ServerSetting::language=="Chinese")
                            stt::system::ServerSetting::logfile->writeLog("http server : 处理fd= "+to_string(fd)+" 完成");
                        else
                            stt::system::ServerSetting::logfile->writeLog("http server : handled fd= "+to_string(fd)+" sucessfully");
                    }
                
            }
        const auto bufferedConnection=clientfd.find(fd);
        if(bufferedConnection!=clientfd.end()&&bufferedConnection->second.fd==fd&&
           bufferedConnection->second.pendindQueue.empty()&&bufferedConnection->second.p_buffer_now>0)
            scheduleBufferedRead(fd,bufferedConnection->second.connection_obj_fd);
    }

    static bool validateWebSocketUpgrade(const stt::network::HttpRequestInformation &request,std::string &key)
    {
        if(request.type!="GET")
            return false;
        const auto trim=[](std::string_view value) {
            while(!value.empty()&&(value.front()==' '||value.front()=='\t')) value.remove_prefix(1);
            while(!value.empty()&&(value.back()==' '||value.back()=='\t')) value.remove_suffix(1);
            return value;
        };
        const auto lower=[](std::string_view value) {
            std::string result(value);
            std::transform(result.begin(),result.end(),result.begin(),[](const unsigned char character) {
                return static_cast<char>(std::tolower(character));
            });
            return result;
        };
        std::unordered_map<std::string,std::string> headers;
        size_t cursor=request.header.find("\r\n");
        if(cursor==std::string::npos)
            return false;
        cursor+=2;
        while(cursor<request.header.size())
        {
            const size_t end=request.header.find("\r\n",cursor);
            const size_t lineEnd=end==std::string::npos?request.header.size():end;
            const std::string_view line(request.header.data()+cursor,lineEnd-cursor);
            const size_t colon=line.find(':');
            if(colon==std::string_view::npos||colon==0)
                return false;
            const std::string name=lower(line.substr(0,colon));
            if(headers.find(name)!=headers.end())
                return false;
            headers.emplace(name,std::string(trim(line.substr(colon+1))));
            if(end==std::string::npos) break;
            cursor=end+2;
        }
        const auto upgrade=headers.find("upgrade");
        const auto connection=headers.find("connection");
        const auto version=headers.find("sec-websocket-version");
        const auto requestKey=headers.find("sec-websocket-key");
        if(upgrade==headers.end()||connection==headers.end()||version==headers.end()||requestKey==headers.end()||
           lower(trim(upgrade->second))!="websocket"||trim(version->second)!="13")
            return false;
        bool hasUpgradeToken=false;
        std::string_view connectionValue(connection->second);
        size_t tokenStart=0;
        while(tokenStart<=connectionValue.size())
        {
            const size_t comma=connectionValue.find(',',tokenStart);
            const std::string_view token=trim(connectionValue.substr(tokenStart,comma==std::string_view::npos?connectionValue.size()-tokenStart:comma-tokenStart));
            if(lower(token)=="upgrade") hasUpgradeToken=true;
            if(comma==std::string_view::npos) break;
            tokenStart=comma+1;
        }
        if(!hasUpgradeToken)
            return false;
        key=std::string(trim(requestKey->second));
        if(key.size()!=24)
            return false;
        unsigned char decoded[32]{};
        const int decodedLength=EVP_DecodeBlock(decoded,
                                                reinterpret_cast<const unsigned char*>(key.data()),
                                                static_cast<int>(key.size()));
        if(decodedLength<0)
            return false;
        size_t padding=0;
        if(!key.empty()&&key.back()=='=') ++padding;
        if(key.size()>1&&key[key.size()-2]=='=') ++padding;
        return static_cast<size_t>(decodedLength)>=padding&&static_cast<size_t>(decodedLength)-padding==16;
    }

    void stt::network::WebSocketServer::handler_netevent(const int &fd)
    {
        
        
        if(clientfd[fd].fd!=-1)//can not find fd information,we need to writedown this error and close this fd
        {
            
                
                if(stt::system::ServerSetting::logfile!=nullptr)
                {
                    if(stt::system::ServerSetting::language=="Chinese")
                        stt::system::ServerSetting::logfile->writeLog("websocket server : 正在处理fd= "+to_string(fd));
                    else
                        stt::system::ServerSetting::logfile->writeLog("websocket server : now handleing fd= "+to_string(fd));
                }
            TcpFDInf &Tcpinf=clientfd[fd];

            //unique_lock<mutex> lock(lwb);
            auto jj=wbclientfd.find(fd);
            if(jj==wbclientfd.end())//没有进行wb握手
            {
                
                HttpServerFDHandler k;
                prepareHandler(k,fd);
                //k1.setFD(cclientfd.fd,clientfd[cclientfd.fd].ssl,unblock);
                //unique_lock<mutex> lock(lwb);
                if(stt::system::ServerSetting::logfile!=nullptr)
                {
                    if(stt::system::ServerSetting::language=="Chinese")
                        stt::system::ServerSetting::logfile->writeLog("websocket server consumer  : fd= "+to_string(fd)+" 正在进行websocket握手");
                    else
                        stt::system::ServerSetting::logfile->writeLog("websocket server consumer  : fd= "+to_string(fd)+" handshaking websocket...");
                }
                WebSocketFDInformation winf;
                winf.fd=fd;
                winf.closeflag=false;
                
                //HttpRequestInformation HttpInf;
                int ret=k.solveRequest(Tcpinf,winf.httpinf,buffer_size,1,maxHttpHeaderBytes);
                if(ret==-1)
                {
                    //k.close();

                        TcpServer::close(fd);
                        
                        if(stt::system::ServerSetting::logfile!=nullptr)
                        {
                            if(stt::system::ServerSetting::language=="Chinese")
                                stt::system::ServerSetting::logfile->writeLog("websocket server consumer  : fd= "+to_string(fd)+" 无法解析http请求或者对端关闭连接 wb握手失败 已经关闭这个连接");
                            else
                                stt::system::ServerSetting::logfile->writeLog("websocket server consumer  : fd= "+to_string(fd)+" couldn't solve http request or host had closed this connection.fail to handshake websocket.have closed this connection");
                        }
                    //continue;
                    return;
                    //wb握手失败
                }
                else if(ret==0)
                {
                    if(stt::system::ServerSetting::logfile!=nullptr)
                    {
                        if(stt::system::ServerSetting::language=="Chinese")
                            stt::system::ServerSetting::logfile->writeLog("websocket server consumer  : 解析fd= "+to_string(fd)+"未完成 等待新的数据继续解析");
                        else
                            stt::system::ServerSetting::logfile->writeLog("websocket server consumer  : fd= "+to_string(fd)+"wait new data to continue solve this request");
                    }
                    //continue;
                    return;
                }
                else if(ret==1)
                {
                    metricParsedHttpRequests.fetch_add(1,std::memory_order_relaxed);
                    winf.locPara=winf.httpinf.locPara;
                    winf.header=winf.httpinf.header;
                    string keyy;
                    if(!validateWebSocketUpgrade(winf.httpinf,keyy))
                    {
                        (void)k.sendBack("","Connection: close","400 Bad Request");
                        requestCloseAfterFlush(fd);
                        return;
                    }
                    //cout<<winf.header<<endl;
                    if(security_open)
                    {
                        int ret=connectionLimiter.allowRequest(clientfd[fd].ip,fd,winf.httpinf.loc,requestTimes,requestSecs);
                        if(ret!=stt::security::ALLOW)
                        {
                            //securitySendBackFun(k,inff);
                            if(ret==stt::security::CLOSE)
                            {
                                TcpServer::close(fd);
                                if(stt::system::ServerSetting::logfile!=nullptr)
                                {
                                    if(stt::system::ServerSetting::language=="Chinese")
                                        stt::system::ServerSetting::logfile->writeLog("websocket server : fd="+to_string(fd)+"请求太频繁，已经关闭连接");
                                    else
                                        stt::system::ServerSetting::logfile->writeLog("websocket server : fd="+to_string(fd)+"request are too frequent,now has closed this connection");
                                }
                            }
                            else
                            {
                                if(stt::system::ServerSetting::logfile!=nullptr)
                                {
                                    if(stt::system::ServerSetting::language=="Chinese")
                                        stt::system::ServerSetting::logfile->writeLog("websocket server : fd="+to_string(fd)+"请求太频繁，已经忽略请求");
                                    else
                                        stt::system::ServerSetting::logfile->writeLog("websocket server : fd="+to_string(fd)+"request are too frequent,now has ignored this request");
                                }
                            }
                            return;
                        }
                    }
                    if(!fcc(winf))//条件不满足
                    {
                        //k.close();
                        TcpServer::close(fd);
                        
                        if(stt::system::ServerSetting::logfile!=nullptr)
                        {
                            if(stt::system::ServerSetting::language=="Chinese")
                                stt::system::ServerSetting::logfile->writeLog("websocket server consumer  : fd= "+to_string(fd)+" 连接限制条件不满足 websocket握手失败 服务器已经关闭这个连接");
                            else
                                stt::system::ServerSetting::logfile->writeLog("websocket server consumer  : fd= "+to_string(fd)+" The connection constraints are not met.websocket handshake fail.server has closed this connection.");
                        }
                        //continue;
                        return;
                    }
                    keyy+="258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
                    string result="";
                    CryptoUtil::sha1(string(keyy),result);
                    keyy=EncodingUtil::base64_encode(result);
                    result=HttpStringUtil::createHeader("Upgrade","websocket","Connection","Upgrade","Sec-WebSocket-Accept",keyy);
                    if(!k.sendBack("",result,"101 Switching Protocols"))
                    {
                        //k.close();
                        TcpServer::close(fd);
                    
                        if(stt::system::ServerSetting::logfile!=nullptr)
                        {
                            if(stt::system::ServerSetting::language=="Chinese")
                                stt::system::ServerSetting::logfile->writeLog("websocket server consumer  : fd= "+to_string(fd)+" 握手响应无法发送 websocket握手失败 服务器已经关闭这个连接");
                            else
                                stt::system::ServerSetting::logfile->writeLog("websocket server consumer  : fd= "+to_string(fd)+" couldn't send handshake response .websocket handshake fail. server has closed this connection");
                        }
                        //continue;
                        return;
                        //握手失败
                    }
                    winf.response=::time(0);
                    winf.HBTime=0;
                    wbclientfd.emplace(fd,winf);
                    {
                        std::lock_guard<std::mutex> lock(websocketRegistryMutex);
                        websocketConnections[fd]=Tcpinf.connection_obj_fd;
                        websocketClosing.erase(fd);
                    }
                    if(stt::system::ServerSetting::logfile!=nullptr)
                    {
                            if(stt::system::ServerSetting::language=="Chinese")
                                stt::system::ServerSetting::logfile->writeLog("websocket server consumer  : fd= "+to_string(fd)+" websocket握手成功");
                            else
                                stt::system::ServerSetting::logfile->writeLog("websocket server consumer  : fd= "+to_string(fd)+" websocket has handshaked sucessfully");
                    }
                    //thread(fccc,winf,ref(*this)).detach();
                    WebSocketServerFDHandler kk;
                    prepareHandler(kk,fd);
                    if(!fccc(kk,winf))
                    {
                        closeWithoutLock(fd);
                        if(stt::system::ServerSetting::logfile!=nullptr)
                        {
                            if(stt::system::ServerSetting::language=="Chinese")
                                stt::system::ServerSetting::logfile->writeLog("websocket server : fd= "+to_string(fd)+" 调用连接后的初始函数失败，已经关闭连接");
                            else
                                stt::system::ServerSetting::logfile->writeLog("websocket server : fd= "+to_string(fd)+" fail to use start function,now has closed this connection");
                        }
                        return;
                    }
                    if(Tcpinf.p_buffer_now>0)
                        scheduleBufferedRead(fd,Tcpinf.connection_obj_fd);
                    
                }
                //sleep(10);
                //k.solveRequest(Tcpinf,HttpInf,buffer_size,1);
                //sleep(5);
                //lock.unlock();
            }
            else
            {
                
                //lock.unlock();
                WebSocketServerFDHandler k;
                
            prepareHandler(k,fd);
            
            int ret=1;
            jj->second.fd=fd;
            jj->second.connection_obj_fd=clientfd[fd].connection_obj_fd;
            
            ret=k.getMessage(Tcpinf,jj->second,buffer_size,1);
            
            if(ret==-1)
            {
                    closeWithoutLock(fd);
                    if(stt::system::ServerSetting::logfile!=nullptr)
                    {
                        if(stt::system::ServerSetting::language=="Chinese")
                            stt::system::ServerSetting::logfile->writeLog("websocket server : 读取数据fd= "+to_string(fd)+" 失败，已经关闭连接");
                        else
                            stt::system::ServerSetting::logfile->writeLog("websocket server : read data from fd= "+to_string(fd)+" fail,now has closed this connection");
                    }
                    return;
            }
            else if(ret==1)
            {
                    if(jj->second.closeflag==true)//收到关闭确认
                    {
                        if(stt::system::ServerSetting::logfile!=nullptr)
                        {
                        if(stt::system::ServerSetting::language=="Chinese")
                            stt::system::ServerSetting::logfile->writeLog("websocket server consumer  : fd= "+to_string(fd)+" 收到关闭确认帧: "+ jj->second.message);
                        else 
                            stt::system::ServerSetting::logfile->writeLog("websocket server consumer  : fd= "+to_string(fd)+" has received closed confirm fin:"+ jj->second.message);
                        }
                        TcpServer::close(fd);
                        return;
                    }
                    else//收到关闭
                    {
                        if(stt::system::ServerSetting::logfile!=nullptr)
                        {
                        if(stt::system::ServerSetting::language=="Chinese")
                            stt::system::ServerSetting::logfile->writeLog("websocket server consumer  : fd= "+to_string(fd)+" 收到关闭帧: "+ jj->second.message);
                        else 
                            stt::system::ServerSetting::logfile->writeLog("websocket server consumer  : fd= "+to_string(fd)+" has received closed fin:"+ jj->second.message);
                        }
                        
                        closeAck(fd,jj->second.message);
                        wbclientfd.erase(jj);
                        return;
                    }
            }
            else if(ret==2)
            {
                    if(stt::system::ServerSetting::logfile!=nullptr)
                    {
                        if(stt::system::ServerSetting::language=="Chinese")
                            stt::system::ServerSetting::logfile->writeLog("websocket server consumer  : fd= "+to_string(fd)+" 收到心跳确认: "+ jj->second.message);
                        else 
                            stt::system::ServerSetting::logfile->writeLog("websocket server consumer  : fd= "+to_string(fd)+" has received heartbeat confirm:"+ jj->second.message);
                    }
                    jj->second.response=::time(0);
                    jj->second.HBTime=0;

                    //记录心跳确认

                    jj->second.message="";
                    if(Tcpinf.p_buffer_now>0)
                        scheduleBufferedRead(fd,Tcpinf.connection_obj_fd);
                    return;
            }
            else if(ret==3)
            {
                    if(stt::system::ServerSetting::logfile!=nullptr)
                    {
                        if(stt::system::ServerSetting::language=="Chinese")
                            stt::system::ServerSetting::logfile->writeLog("websocket server consumer  : fd= "+to_string(fd)+" 收到心跳: "+ jj->second.message);
                        else 
                            stt::system::ServerSetting::logfile->writeLog("websocket server consumer  : fd= "+to_string(fd)+" has received heartbeat:"+ jj->second.message);
                    }
                    jj->second.response=::time(0);
                    const string pingPayload=jj->second.message;
                    if(!sendMessage(jj->first,pingPayload,"1010"))//Pong 必须回显 Ping payload
                    {
                        const int failedFD=jj->first;
                        wbclientfd.erase(jj);
                        TcpServer::close(failedFD);
                        return;
                    }
         
                    //心跳
                    else
                        jj->second.message="";
                    if(Tcpinf.p_buffer_now>0)
                        scheduleBufferedRead(fd,Tcpinf.connection_obj_fd);
                    return;
            }
            else if(ret==4)
            {
                    if(stt::system::ServerSetting::logfile!=nullptr)
                    {
                        if(stt::system::ServerSetting::language=="Chinese")
                            stt::system::ServerSetting::logfile->writeLog("websocket server consumer  : 解析fd= "+to_string(fd)+"未完成 等待新的数据继续解析");
                        else
                            stt::system::ServerSetting::logfile->writeLog("websocket server consumer  : fd= "+to_string(fd)+"wait new data to continue solve this request");
                    }
                    return;
            }
            else if(ret==0)
            {
                    if(stt::system::ServerSetting::logfile!=nullptr)
                    {
                        if(stt::system::ServerSetting::language=="Chinese")
                            stt::system::ServerSetting::logfile->writeLog("websocket server consumer  : fd= "+to_string(fd)+" 收到常规信息: "+ jj->second.message);
                        else 
                            stt::system::ServerSetting::logfile->writeLog("websocket server consumer  : fd= "+to_string(fd)+" has received normal message:"+ jj->second.message);
                    }
                    jj->second.response=::time(0);
                    //if(!fc(jj->second.message,*this,jj->second))//回调函数失败
                    //{
                    //    close(fd);
                    //    return;
                    //}
                    //jj->second.message="";
                    //return;
            }
        
            //lock.unlock();
            
            
            //lock6.unlock();
            //开始处理
                //入队
            Tcpinf.pendindQueue.push(std::move(jj->second));
            
            if(Tcpinf.pendindQueue.size()==1)//只有一个 说明没有任务没做完 直接执行
            {
               
                WebSocketFDInformation &inff=std::any_cast<WebSocketFDInformation&>(clientfd[fd].pendindQueue.front());
                Tcpinf.FDStatus=-1;
                int ret;
                //获取key,自动解析到ctx的key键
                ++Tcpinf.FDStatus;
                ret=parseKey(k,inff);
                
                if(ret==0)//慢处理
                {
                    return;
                }
                else if(ret<=-1)
                {
                    //清掉任务后返回
                    //Tcpinf.pendindQueue.pop();
                    //-2要关闭连接
                    if(ret==-2)
                    {
                        closeFD(fd);
                        //closeWithoutLock(fd);
                        if(stt::system::ServerSetting::logfile!=nullptr)
                        {
                            if(stt::system::ServerSetting::language=="Chinese")
                                stt::system::ServerSetting::logfile->writeLog("websocket server : parsekey的时候失败 fd= "+to_string(fd)+" ，已经关闭连接");
                            else
                                stt::system::ServerSetting::logfile->writeLog("websocket server : parsekey fail fd= "+to_string(fd)+",now has closed this connection");
                        }
                    }
                    else
                    {
                        //k.sendBack("","","404 NOT FOUND");
                        if(stt::system::ServerSetting::logfile!=nullptr)
                        {
                            if(stt::system::ServerSetting::language=="Chinese")
                                stt::system::ServerSetting::logfile->writeLog("websocket server : parsekey的时候失败 fd= "+to_string(fd)+" ，已扔掉本次任务并且发回错误信息");
                            else
                                stt::system::ServerSetting::logfile->writeLog("websocket server : parsekey fail fd= "+to_string(fd)+",now has throwed this task and send back error message");
                        }
                    }
                    return;
                }
                if(security_open)
                {
                    int ret=connectionLimiter.allowRequest(clientfd[fd].ip,fd,std::any_cast<const std::string&>(inff.ctx["key"]),requestTimes,requestSecs);
                    if(ret!=stt::security::ALLOW)
                    {
                        securitySendBackFun(k,inff);
                        if(ret==stt::security::CLOSE)
                        {
                            TcpServer::close(fd);
                            if(stt::system::ServerSetting::logfile!=nullptr)
                            {
                                if(stt::system::ServerSetting::language=="Chinese")
                                    stt::system::ServerSetting::logfile->writeLog("websocket server : fd="+to_string(fd)+"请求太频繁，已经关闭连接");
                                else
                                    stt::system::ServerSetting::logfile->writeLog("websocket server : fd="+to_string(fd)+"request are too frequent,now has closed this connection");
                            }
                        }
                        else
                        {
                            if(stt::system::ServerSetting::logfile!=nullptr)
                            {
                                if(stt::system::ServerSetting::language=="Chinese")
                                    stt::system::ServerSetting::logfile->writeLog("websocket server : fd="+to_string(fd)+"请求太频繁，已经忽略请求");
                                else
                                    stt::system::ServerSetting::logfile->writeLog("websocket server : fd="+to_string(fd)+"request are too frequent,now has ignored this request");
                            }
                        }
                        return;
                    }
                }
                //遍历任务
                
                auto ii=solveFun.find(std::any_cast<const std::string&>(inff.ctx["key"]));
                
                if(ii==solveFun.end())//找不到
                {
                    
                    //k.sendBack("","","404 NOT FOUND");
                    //close(fd);
                    if(stt::system::ServerSetting::logfile!=nullptr)
                    {
                        if(stt::system::ServerSetting::language=="Chinese")
                            stt::system::ServerSetting::logfile->writeLog("websocket server : 找不到处理函数 fd= "+to_string(fd)+"。调用全局备用处理函数");
                        else
                            stt::system::ServerSetting::logfile->writeLog("websocket server : can not find solve function fd= "+to_string(fd)+" . use global backup slove function.");
                    }
                    if(!globalSolveFun(k,inff))
                    {
                        closeFD(fd);
                        if(stt::system::ServerSetting::logfile!=nullptr)
                        {
                            if(stt::system::ServerSetting::language=="Chinese")
                                stt::system::ServerSetting::logfile->writeLog("websocket server : 调用全局备用函数失败 fd= "+to_string(fd)+"已经关闭连接.");
                            else
                                stt::system::ServerSetting::logfile->writeLog("websocket server : use global backup slove function fail. fd= "+to_string(fd)+". has closed this connection.");
                        }
                    }
                    Tcpinf.pendindQueue.pop();
                }
                else//找得到处理函数
                {
                    
                    for(auto &f:ii->second)
                    {
                        int rett=f(k,inff);
                        ++Tcpinf.FDStatus;
                        if(rett==1)
                        {
                            if(stt::system::ServerSetting::logfile!=nullptr)
                            {
                                if(stt::system::ServerSetting::language=="Chinese")
                                    stt::system::ServerSetting::logfile->writeLog("websocket server : 处理fd= "+to_string(fd)+" 第"+ to_string(Tcpinf.FDStatus)+  "次完成");
                                else
                                    stt::system::ServerSetting::logfile->writeLog("websocket server : handled fd= "+to_string(fd)+" sucessfully.It's the "+to_string(Tcpinf.FDStatus)+"times");
                            }
                        }
                        else if(rett==0)
                        {
                            if(stt::system::ServerSetting::logfile!=nullptr)
                            {
                                if(stt::system::ServerSetting::language=="Chinese")
                                    stt::system::ServerSetting::logfile->writeLog("websocket server : 处理fd= "+to_string(fd)+" 第"+ to_string(Tcpinf.FDStatus)+  "次.等待任务完成.");
                                else
                                    stt::system::ServerSetting::logfile->writeLog("websocket server : handled fd= "+to_string(fd)+" .It's the "+to_string(Tcpinf.FDStatus)+"times job. now is waitting it to be finish.");
                            }
                            
                            return;
                        }
                        else if(rett==-1)
                        {
                            if(stt::system::ServerSetting::logfile!=nullptr)
                            {
                                if(stt::system::ServerSetting::language=="Chinese")
                                    stt::system::ServerSetting::logfile->writeLog("websocket server : 处理fd= "+to_string(fd)+" 第"+ to_string(Tcpinf.FDStatus)+  "次失败.");
                                else
                                    stt::system::ServerSetting::logfile->writeLog("websocket server : handled fd= "+to_string(fd)+" fail.It's the "+to_string(Tcpinf.FDStatus)+"times.");
                            }
                            //Tcpinf.pendindQueue.pop();
                            return;
                        }
                        else
                        {
                            closeFD(fd);
                            if(stt::system::ServerSetting::logfile!=nullptr)
                            {
                                if(stt::system::ServerSetting::language=="Chinese")
                                    stt::system::ServerSetting::logfile->writeLog("websocket server : 处理fd= "+to_string(fd)+" 第"+ to_string(Tcpinf.FDStatus)+  "次失败。已经关闭连接。");
                                else
                                    stt::system::ServerSetting::logfile->writeLog("websocket server : handled fd= "+to_string(fd)+" fail.It's the "+to_string(Tcpinf.FDStatus)+"times and now has closed this connection.");
                            }
                            //Tcpinf.pendindQueue.pop();
                            return;
                        }
                    }
                    
                    Tcpinf.pendindQueue.pop();
                }

                    if(stt::system::ServerSetting::logfile!=nullptr)
                    {
                        if(stt::system::ServerSetting::language=="Chinese")
                            stt::system::ServerSetting::logfile->writeLog("websocket server : 处理fd= "+to_string(fd)+" 完成");
                        else
                            stt::system::ServerSetting::logfile->writeLog("websocket server : handled fd= "+to_string(fd)+" sucessfully");
                    }
                
            }
            if(Tcpinf.fd==fd&&Tcpinf.pendindQueue.empty()&&Tcpinf.p_buffer_now>0)
                scheduleBufferedRead(fd,Tcpinf.connection_obj_fd);
            }
        }
    }
    void stt::network::WebSocketServer::handler_workerevent(WorkerMessage message)
    {
        const int fd=message.fd;
        const int ret=message.ret;
        auto connectionIt=clientfd.find(fd);
        if(connectionIt==clientfd.end()||connectionIt->second.fd!=fd||
           connectionIt->second.connection_obj_fd!=message.connection_obj_fd)
            return;
        if(connectionIt->second.active_workers>0)
            --connectionIt->second.active_workers;
        if(connectionIt->second.closing)
        {
            if(connectionIt->second.active_workers==0)
            {
                connectionIt->second.closing=false;
                TcpServer::close(fd);
            }
            return;
        }
        if(ret>=0&&message.request&&!connectionIt->second.pendindQueue.empty())
        {
            auto request=std::static_pointer_cast<WebSocketFDInformation>(message.request);
            std::any_cast<WebSocketFDInformation&>(connectionIt->second.pendindQueue.front())=std::move(*request);
        }
        if(ret==-2)
        {
            closeFD(fd);
                    if(stt::system::ServerSetting::logfile!=nullptr)
                    {
                        if(stt::system::ServerSetting::language=="Chinese")
                            stt::system::ServerSetting::logfile->writeLog("websocket server : worker处理失败 fd= "+to_string(fd)+" ，已经关闭连接");
                        else
                            stt::system::ServerSetting::logfile->writeLog("websocket server : worker solve fail fd= "+to_string(fd)+" ,now has closed this connection");
                    }
            return;
        }
        WebSocketServerFDHandler k;
        prepareHandler(k,fd);
        
        if(ret==-1)
        {
            clientfd[fd].pendindQueue.pop();
            if(stt::system::ServerSetting::logfile!=nullptr)
                    {
                        if(stt::system::ServerSetting::language=="Chinese")
                            stt::system::ServerSetting::logfile->writeLog("websocket server : worker处理失败 fd= "+to_string(fd)+" ，跳过本次请求");
                        else
                            stt::system::ServerSetting::logfile->writeLog("websocket server : worker solve fail fd= "+to_string(fd)+" ,skip this request");
                    }
        }
        else
        {
            if(clientfd[fd].pendindQueue.empty())
            {
                if(stt::system::ServerSetting::logfile!=nullptr)
                {
                    if(stt::system::ServerSetting::language=="Chinese")
                        stt::system::ServerSetting::logfile->writeLog("websocket server : worker处理成功,但是上一个连接已经关闭，所以不予继续处理 fd= "+to_string(fd));
                    else
                        stt::system::ServerSetting::logfile->writeLog("websocket server : worker solve sucessfully.but the last connection has been closed so stop solving this request. fd= "+to_string(fd));
                }
                return;
            }
            WebSocketFDInformation &inf=std::any_cast<WebSocketFDInformation&>(clientfd[fd].pendindQueue.front());
            if(inf.connection_obj_fd!=clientfd[fd].connection_obj_fd)
            {
                if(stt::system::ServerSetting::logfile!=nullptr)
                {
                    if(stt::system::ServerSetting::language=="Chinese")
                        stt::system::ServerSetting::logfile->writeLog("websocket server : worker处理成功,但是上一个连接已经关闭，所以不予继续处理 fd= "+to_string(fd));
                    else
                        stt::system::ServerSetting::logfile->writeLog("websocket server : worker solve sucessfully.but the last connection has been closed so stop solving this request. fd= "+to_string(fd));
                }
                return;
            }
            
            auto ii=solveFun.find(std::any_cast<const std::string&>(inf.ctx["key"]));//对应的任务
            if(stt::system::ServerSetting::logfile!=nullptr)
            {
                if(stt::system::ServerSetting::language=="Chinese")
                    stt::system::ServerSetting::logfile->writeLog("websocket server : worker处理成功 fd= "+to_string(fd));
                else
                    stt::system::ServerSetting::logfile->writeLog("websocket server : worker solve sucessfully fd= "+to_string(fd));
            }
            if(ii==solveFun.end())//找不到
                {
                    
                    //k.sendBack("","","404 NOT FOUND");
                    //close(fd);
                    if(stt::system::ServerSetting::logfile!=nullptr)
                    {
                        if(stt::system::ServerSetting::language=="Chinese")
                            stt::system::ServerSetting::logfile->writeLog("websocket server : 找不到处理函数 fd= "+to_string(fd)+"。调用全局备用处理函数");
                        else
                            stt::system::ServerSetting::logfile->writeLog("websocket server : can not find solve function fd= "+to_string(fd)+" . use global backup slove function.");
                    }
                    if(!globalSolveFun(k,inf))
                    {
                        closeFD(fd);
                        if(stt::system::ServerSetting::logfile!=nullptr)
                        {
                            if(stt::system::ServerSetting::language=="Chinese")
                                stt::system::ServerSetting::logfile->writeLog("websocket server : 调用全局备用函数失败 fd= "+to_string(fd)+"已经关闭连接.");
                            else
                                stt::system::ServerSetting::logfile->writeLog("websocket server : use global backup slove function fail. fd= "+to_string(fd)+". has closed this connection.");
                        }
                    }
                    //clientfd[fd].pendindQueue.pop();
                    clientfd[fd].pendindQueue.pop();
                }
            else
            {
            //继续做
            
            for(;static_cast<size_t>(clientfd[fd].FDStatus)<ii->second.size();)
            {
                
                int rett=ii->second[clientfd[fd].FDStatus](k,inf);
                clientfd[fd].FDStatus++;
                        if(rett==1)
                        {
                            if(stt::system::ServerSetting::logfile!=nullptr)
                            {
                                if(stt::system::ServerSetting::language=="Chinese")
                                    stt::system::ServerSetting::logfile->writeLog("websocket server : 处理fd= "+to_string(fd)+" 第"+ to_string(clientfd[fd].FDStatus)+  "次完成");
                                else
                                    stt::system::ServerSetting::logfile->writeLog("websocket server : handled fd= "+to_string(fd)+" sucessfully.It's the "+to_string(clientfd[fd].FDStatus)+"times");
                            }
                        }
                        else if(rett==0)
                        {
                            if(stt::system::ServerSetting::logfile!=nullptr)
                            {
                                if(stt::system::ServerSetting::language=="Chinese")
                                    stt::system::ServerSetting::logfile->writeLog("websocket server : 处理fd= "+to_string(fd)+" 第"+ to_string(clientfd[fd].FDStatus)+  "次.等待任务完成.");
                                else
                                    stt::system::ServerSetting::logfile->writeLog("websocket server : handled fd= "+to_string(fd)+" .It's the "+to_string(clientfd[fd].FDStatus)+"times job. now is waitting it to be finish.");
                            }
                            return;
                        }
                        else
                        {
                            closeFD(fd);
                            if(stt::system::ServerSetting::logfile!=nullptr)
                            {
                                if(stt::system::ServerSetting::language=="Chinese")
                                    stt::system::ServerSetting::logfile->writeLog("websocket server : 处理fd= "+to_string(fd)+" 第"+ to_string(clientfd[fd].FDStatus)+  "次失败。已经关闭连接。");
                                else
                                    stt::system::ServerSetting::logfile->writeLog("websocket server : handled fd= "+to_string(fd)+" fail.It's the "+to_string(clientfd[fd].FDStatus)+"times and now has closed this connection.");
                            }
                            //clientfd[fd].pendindQueue.pop();
                            return;
                        }
            }
            
            clientfd[fd].pendindQueue.pop();
            }
        }
     
        TcpFDInf &Tcpinf=clientfd[fd];
        //检查是否有下一轮
        if(Tcpinf.pendindQueue.size()>=1)//只有一个 说明没有任务没做完 直接执行
            {
                WebSocketFDInformation &inff=std::any_cast< WebSocketFDInformation&>(clientfd[fd].pendindQueue.front());
                Tcpinf.FDStatus=-1;
                int ret;
                //获取key,自动解析到ctx的key键
                ++Tcpinf.FDStatus;
                ret=parseKey(k,inff);

                if(ret==0)//慢处理
                    return;
                else if(ret<=-1)
                {
                    //清掉任务后返回
                    Tcpinf.pendindQueue.pop();
                    //-2要关闭连接
                    if(ret==-2)
                    {
                        //k.sendBack("","","404 NOT FOUND");
                        closeFD(fd);
                        if(stt::system::ServerSetting::logfile!=nullptr)
                        {
                            if(stt::system::ServerSetting::language=="Chinese")
                                stt::system::ServerSetting::logfile->writeLog("websocket server : parsekey的时候失败 fd= "+to_string(fd)+" ，已经关闭连接");
                            else
                                stt::system::ServerSetting::logfile->writeLog("websocket server : parsekey fail fd= "+to_string(fd)+",now has closed this connection");
                        }
                    }
                    else
                    {
                        //k.sendBack("","","404 NOT FOUND");
                        if(stt::system::ServerSetting::logfile!=nullptr)
                        {
                            if(stt::system::ServerSetting::language=="Chinese")
                                stt::system::ServerSetting::logfile->writeLog("websocket server : parsekey的时候失败 fd= "+to_string(fd)+" ，已扔掉本次任务并且发回错误信息");
                            else
                                stt::system::ServerSetting::logfile->writeLog("websocketserver : parsekey fail fd= "+to_string(fd)+",now has throwed this task and send back error message");
                        }
                    }
                    return;
                }
                
                //遍历任务
                auto ii=solveFun.find(std::any_cast<const std::string&>(inff.ctx["key"]));
                if(ii==solveFun.end())//找不到
                {
                    //k.sendBack("","","404 NOT FOUND");
                    //close(fd);
                    if(stt::system::ServerSetting::logfile!=nullptr)
                    {
                        if(stt::system::ServerSetting::language=="Chinese")
                            stt::system::ServerSetting::logfile->writeLog("websocket server : 找不到处理函数 fd= "+to_string(fd)+"。调用全局备用处理函数");
                        else
                            stt::system::ServerSetting::logfile->writeLog("websocket server : can not find solve function fd= "+to_string(fd)+" . use global backup slove function.");
                    }
                    if(!globalSolveFun(k,inff))
                    {
                        closeFD(fd);
                        if(stt::system::ServerSetting::logfile!=nullptr)
                        {
                            if(stt::system::ServerSetting::language=="Chinese")
                                stt::system::ServerSetting::logfile->writeLog("websocket server : 调用全局备用函数失败 fd= "+to_string(fd)+"已经关闭连接.");
                            else
                                stt::system::ServerSetting::logfile->writeLog("websocket server : use global backup slove function fail. fd= "+to_string(fd)+". has closed this connection.");
                        }
                    }
                    Tcpinf.pendindQueue.pop();
                }
                else//找得到处理函数
                {
                    for(auto &f:ii->second)
                    {
                        int rett=f(k,inff);
                        ++Tcpinf.FDStatus;
                        if(rett==1)
                        {
                            if(stt::system::ServerSetting::logfile!=nullptr)
                            {
                                if(stt::system::ServerSetting::language=="Chinese")
                                    stt::system::ServerSetting::logfile->writeLog("websocket server : 处理fd= "+to_string(fd)+" 第"+ to_string(Tcpinf.FDStatus)+  "次完成");
                                else
                                    stt::system::ServerSetting::logfile->writeLog("websocket server : handled fd= "+to_string(fd)+" sucessfully.It's the "+to_string(Tcpinf.FDStatus)+"times");
                            }
                        }
                        else if(rett==0)
                        {
                            if(stt::system::ServerSetting::logfile!=nullptr)
                            {
                                if(stt::system::ServerSetting::language=="Chinese")
                                    stt::system::ServerSetting::logfile->writeLog("websocket server : 处理fd= "+to_string(fd)+" 第"+ to_string(Tcpinf.FDStatus)+  "次.等待任务完成.");
                                else
                                    stt::system::ServerSetting::logfile->writeLog("websocket server : handled fd= "+to_string(fd)+" .It's the "+to_string(Tcpinf.FDStatus)+"times job. now is waitting it to be finish.");
                            }
                            return;
                        }
                        else if(rett==-1)
                        {

                            if(stt::system::ServerSetting::logfile!=nullptr)
                            {
                                if(stt::system::ServerSetting::language=="Chinese")
                                    stt::system::ServerSetting::logfile->writeLog("websocket server : 处理fd= "+to_string(fd)+" 第"+ to_string(Tcpinf.FDStatus)+  "次失败。");
                                else
                                    stt::system::ServerSetting::logfile->writeLog("websocket server : handled fd= "+to_string(fd)+" fail.It's the "+to_string(Tcpinf.FDStatus)+"times.");
                            }
                            //Tcpinf.pendindQueue.pop();
                            return;
                        }
                        else
                        {
                            closeFD(fd);
                            if(stt::system::ServerSetting::logfile!=nullptr)
                            {
                                if(stt::system::ServerSetting::language=="Chinese")
                                    stt::system::ServerSetting::logfile->writeLog("websocket server : 处理fd= "+to_string(fd)+" 第"+ to_string(Tcpinf.FDStatus)+  "次失败。已经关闭连接。");
                                else
                                    stt::system::ServerSetting::logfile->writeLog("websocket server : handled fd= "+to_string(fd)+" fail.It's the "+to_string(Tcpinf.FDStatus)+"times and now has closed this connection.");
                            }
                            //Tcpinf.pendindQueue.pop();
                            return;
                        }
                    }
                    
                }
                Tcpinf.pendindQueue.pop();

                    if(stt::system::ServerSetting::logfile!=nullptr)
                    {
                        if(stt::system::ServerSetting::language=="Chinese")
                            stt::system::ServerSetting::logfile->writeLog("websocket server : 处理fd= "+to_string(fd)+" 完成");
                        else
                            stt::system::ServerSetting::logfile->writeLog("websocket server : handled fd= "+to_string(fd)+" sucessfully");
                    }
                
            }
        const auto bufferedConnection=clientfd.find(fd);
        if(bufferedConnection!=clientfd.end()&&bufferedConnection->second.fd==fd&&
           bufferedConnection->second.pendindQueue.empty()&&bufferedConnection->second.p_buffer_now>0)
            scheduleBufferedRead(fd,bufferedConnection->second.connection_obj_fd);
    }
    /*
    void stt::network::HttpServer::consumer(const int &threadID)
    {
        HttpServerFDHandler k;
        //TcpFDInf &Tcpinf;
        

        if(stt::system::ServerSetting::logfile!=nullptr)
        {
            if(stt::system::ServerSetting::language=="Chinese")
                stt::system::ServerSetting::logfile->writeLog("http server consumer "+to_string(threadID)+" 打开");
            else
                stt::system::ServerSetting::logfile->writeLog("http server consumer "+to_string(threadID)+" has opened");
        }

        while(flag1)
        {
            unique_lock<mutex> ul1(lq1[threadID]);
            while(fdQueue[threadID].empty()&&flag1)
            {
                cv[threadID].wait(ul1);
            }
            if(!flag1)
            {
                ul1.unlock();
                break;
            }
            int cclientfd=fdQueue[threadID].front().fd;
            bool cclose=fdQueue[threadID].front().close;
            fdQueue[threadID].pop();
            
            ul1.unlock();

            if(cclose)
            {
                TcpServer::close(cclientfd);
                continue;
            }
           // endd=chrono::high_resolution_clock::now();
            //            duration=chrono::duration_cast<chrono::microseconds>(endd-start);
           //             cout<<"队列中拿出用时"<<duration.count()<<endl;
            //unique_lock<mutex> lock6(lc1);
            //auto jj=clientfd.find(cclientfd.fd);
            if(clientfd[cclientfd].fd==-1)//can not find fd information,we need to writedown this error and close this fd
            {
                continue;
            }
            else
            {
                
                if(security_open)
                {
                    if(!connectionLimiter.allowRequest(clientfd[cclientfd].ip,HttpInf[cclientfd].loc))
                    {
                        TcpServer::close(cclientfd);
                        if(stt::system::ServerSetting::logfile!=nullptr)
                        {
                            if(stt::system::ServerSetting::language=="Chinese")
                                stt::system::ServerSetting::logfile->writeLog("tcp server consumer "+to_string(threadID)+" : fd= "+to_string(cclientfd)+"请求太频繁，已经关闭连接");
                            else
                                stt::system::ServerSetting::logfile->writeLog("tcp server consumer "+to_string(threadID)+" : fd= "+to_string(cclientfd)+"request are too frequent,now has closed this connection");
                        }
                        continue;
                    }
                }
                //k.setFD(cclientfd.fd,clientfd[cclientfd.fd].ssl,unblock);
                
                //TcpFDInf &Tcpinf=clientfd[cclientfd.fd];
                
            //lock6.unlock();

             if(stt::system::ServerSetting::logfile!=nullptr)
             {
                if(stt::system::ServerSetting::language=="Chinese")
                    stt::system::ServerSetting::logfile->writeLog("http server consumer "+to_string(threadID)+" : 开始处理fd= "+to_string(cclientfd));
                else
                    stt::system::ServerSetting::logfile->writeLog("http server consumer "+to_string(threadID)+" : now start solveing fd= "+to_string(cclientfd));
             }
            
            
            //endd=chrono::high_resolution_clock::now();
            //            duration=chrono::duration_cast<chrono::microseconds>(endd-start);
            //            cout<<"开始处理用时"<<duration.count()<<endl;
             int ret=1;
             int ii=1;
             prepareHandler(k,cclientfd);
            //cout<<"fd="<<cclientfd<<endl;
            while(ret==1)
            {
                if(stt::system::ServerSetting::logfile!=nullptr)
                {
                    if(stt::system::ServerSetting::language=="Chinese")
                        stt::system::ServerSetting::logfile->writeLog("http server consumer "+to_string(threadID)+" : fd= "+to_string(cclientfd)+" 正在解析请求... 第"+to_string(ii)+"次处理");
                    else
                        stt::system::ServerSetting::logfile->writeLog("http server consumer "+to_string(threadID)+" : fd= "+to_string(cclientfd)+" now solveing request... it is the "+to_string(ii)+" times");
                }
            ret=k.solveRequest(clientfd[cclientfd],HttpInf[cclientfd],buffer_size,ii);
            if(ret==-1)
            {
                    TcpServer::close(cclientfd);
                    if(stt::system::ServerSetting::logfile!=nullptr)
                    {
                        if(stt::system::ServerSetting::language=="Chinese")
                            stt::system::ServerSetting::logfile->writeLog("http server consumer "+to_string(threadID)+" : fd= "+to_string(cclientfd)+" 解析请求失败或者是对方已经关闭连接，服务器已经关闭这个连接");
                        else
                            stt::system::ServerSetting::logfile->writeLog("http server consumer "+to_string(threadID)+" : fd= "+to_string(cclientfd)+" solved request fail or host had closed connection.now server has closed this connection");
                    }
            }

            else if(ret==1)
            {
                
                if(stt::system::ServerSetting::logfile!=nullptr)
                {
                    if(stt::system::ServerSetting::language=="Chinese")
                    {
                        stt::system::ServerSetting::logfile->writeLog("http server consumer "+to_string(threadID)+" : fd= "+to_string(cclientfd)+" 正在处理请求... \n*******请求信息：*********\nheader= "+string(HttpInf[cclientfd].header)+"\nbody="+string(HttpInf[cclientfd].body)+"\nbody_chunked="+string(HttpInf[cclientfd].body_chunked)+"\n*************************");
                    }
                    else
                    {
                        stt::system::ServerSetting::logfile->writeLog("http server consumer "+to_string(threadID)+" : fd= "+to_string(cclientfd)+" now handleing request...\n*******request information：*********\nheader= "+string(HttpInf[cclientfd].header)+"\nbody="+string(HttpInf[cclientfd].body)+"\nbody_chunked="+string(HttpInf[cclientfd].body_chunked)+"\n*************************");
                    }

                }
                //endd=chrono::high_resolution_clock::now();
                //        duration=chrono::duration_cast<chrono::microseconds>(endd-start);
                //        cout<<"开始调用fc用时"<<duration.count()<<endl;
                if(!fc(HttpInf[cclientfd],k))
                {
                
                    TcpServer::close(cclientfd);
                    if(stt::system::ServerSetting::logfile!=nullptr)
                    {
                        if(stt::system::ServerSetting::language=="Chinese")
                            stt::system::ServerSetting::logfile->writeLog("http server consumer "+to_string(threadID)+" : 处理fd= "+to_string(cclientfd)+"失败或者是对方已经关闭连接，已经关闭连接");
                        else
                           stt::system::ServerSetting::logfile->writeLog("http server consumer "+to_string(threadID)+" : handle fd= "+to_string(cclientfd)+"fail or host had closed connection.now server has closed this connection");
                    }
                    break;
                }
                else
                {
                    //endd=chrono::high_resolution_clock::now();
                    //    duration=chrono::duration_cast<chrono::microseconds>(endd-start);
                    //    cout<<"fc完成用时"<<duration.count()<<endl;
                    if(stt::system::ServerSetting::logfile!=nullptr)
                    {
                        if(stt::system::ServerSetting::language=="Chinese")
                            stt::system::ServerSetting::logfile->writeLog("http server consumer "+to_string(threadID)+" : 处理fd= "+to_string(cclientfd)+"完成");
                        else
                            stt::system::ServerSetting::logfile->writeLog("http server consumer "+to_string(threadID)+" : has solved fd= "+to_string(cclientfd)+"sucessfully");
                    }
                    break;
                }
            }
            else if(ret==0)
            {
                    if(stt::system::ServerSetting::logfile!=nullptr)
                    {
                        if(stt::system::ServerSetting::language=="Chinese")
                            stt::system::ServerSetting::logfile->writeLog("http server consumer "+to_string(threadID)+" : 解析fd= "+to_string(cclientfd)+"未完成 等待新的数据继续解析");
                        else
                            stt::system::ServerSetting::logfile->writeLog("http server consumer "+to_string(threadID)+" : fd= "+to_string(cclientfd)+"wait new data to continue solve this request");
                    }
            }
            ++ii;
            //k.solveRequest(clientfd[cclientfd],HttpInf[cclientfd],buffer_size,ii);
            }
            //endd=chrono::high_resolution_clock::now();
            //            duration=chrono::duration_cast<chrono::microseconds>(endd-start);
            //            cout<<"全部处理完用时"<<duration.count()<<endl;
            //            op+=duration.count();
             //           times++;
            //            cout<<"times="<<times<<"op="<<op<<endl;

            }
        }
        //跳出循环意味着结束线程
        unique_lock<mutex> lock3(lco1);
        consumerNum--;
        cout<<"consumer "<<consumerNum<<" quit"<<endl;
        if(stt::system::ServerSetting::logfile!=nullptr)
        {
            if(stt::system::ServerSetting::language=="Chinese")
                stt::system::ServerSetting::logfile->writeLog("http server consumer "+to_string(consumerNum)+"退出");
            else
                stt::system::ServerSetting::logfile->writeLog("http server consumer "+to_string(consumerNum)+"quit");
        }
    }
    */
    
    bool stt::network::EpollSingle::endListen()
    {
        endListenWithSignal();
        if(listenerThread.joinable()&&listenerThread.get_id()!=std::this_thread::get_id())
            listenerThread.join();
        if(controlFD>=0&&!listenerThread.joinable())
        {
            ::close(controlFD);
            controlFD=-1;
        }
        return true;
    }

    void stt::network::EpollSingle::endListenWithSignal()
    {
        flag1.store(false,std::memory_order_release);
        const uint64_t one=1;
        if(controlFD>=0)
            (void)::write(controlFD,&one,sizeof(one));
    }

    void stt::network::EpollSingle::epolll()
    {
        const int epollFD=epoll_create1(EPOLL_CLOEXEC);
        if(epollFD<0)
        {
            flag2.store(false,std::memory_order_release);
            return;
        }
        epoll_event ev{};
        ev.data.fd=fd;
        if(flag==false)
            ev.events=EPOLLIN|EPOLLET|EPOLLERR|EPOLLHUP;
        else
            ev.events=EPOLLIN|EPOLLERR|EPOLLHUP;
        if(epoll_ctl(epollFD,EPOLL_CTL_ADD,fd,&ev)<0)
        {
            ::close(epollFD);
            flag2.store(false,std::memory_order_release);
            return;
        }
        epoll_event controlEvent{};
        controlEvent.data.fd=controlFD;
        controlEvent.events=EPOLLIN;
        if(controlFD<0||epoll_ctl(epollFD,EPOLL_CTL_ADD,controlFD,&controlEvent)<0)
        {
            ::close(epollFD);
            flag2.store(false,std::memory_order_release);
            return;
        }
        epoll_event events[2]{};

        DateTime timer1;
        Duration dt;
        DateTime timer2;
        Duration t;
        while(flag1.load(std::memory_order_acquire))
        {
            if(!timer1.isStart())
                timer1.startTiming();
            const int infds=epoll_wait(epollFD,events,2,1000);
            if(infds<0)
            {
                if(errno==EINTR) continue;
                break;
            }
            bool stopEvent=false;
            bool dataEvent=false;
            for(int index=0;index<infds;++index)
            {
                if(events[index].data.fd==controlFD)
                {
                    uint64_t value=0;
                    (void)::read(controlFD,&value,sizeof(value));
                    stopEvent=true;
                }
                else if(events[index].data.fd==fd)
                    dataEvent=true;
            }
            if(stopEvent||!flag1.load(std::memory_order_acquire))
                break;
            if(infds==0)
            {
                if(!flag3.load(std::memory_order_acquire))
                {
                    dt=timer1.checkTime();
                    if(dt>=this->dt)
                    {
                        std::function<bool(const int&)> timeoutFunction;
                        {
                            std::lock_guard<std::mutex> lock(callbackMutex);
                            timeoutFunction=fcTimeOut;
                        }
                        if(!timeoutFunction(fd)) break;
                    }
                }
                else
                {
                    if(!timer2.startTiming())
                    {
                        {
                            std::lock_guard<std::mutex> lock(countdownMutex);
                            t=this->t;
                        }
                        if(timer2.checkTime()>=t) break;
                    }
                }
                continue;
            }
            if(dataEvent)
            {
                if(flag3.exchange(false,std::memory_order_acq_rel))
                {
                    timer2.endTiming();
                }
                timer1.endTiming();
                std::function<bool(const int&)> dataFunction;
                {
                    std::lock_guard<std::mutex> lock(callbackMutex);
                    dataFunction=fc;
                }
                if(!dataFunction(fd)) break;
            }
        }
        std::function<void(const int&)> endFunction;
        {
            std::lock_guard<std::mutex> lock(callbackMutex);
            endFunction=fcEnd;
        }
        endFunction(fd);
        ::close(epollFD);
        flag3.store(false,std::memory_order_release);
        flag2.store(false,std::memory_order_release);
    }
    void stt::network::EpollSingle::startListen(const int &fd,const bool &flag,const Duration &dt)
    {
        endListen();
        // A callback executes on listenerThread. It may request shutdown, but it
        // must not overwrite the still-joinable std::thread with a restart.
        if(listenerThread.joinable())
            return;
        this->fd=fd;
        this->flag=flag;
        this->dt=dt;
        controlFD=eventfd(0,EFD_NONBLOCK|EFD_CLOEXEC);
        if(fd<0||controlFD<0)
            return;
        flag1.store(true,std::memory_order_release);
        flag2.store(true,std::memory_order_release);
        listenerThread=thread(&EpollSingle::epolll,this);
    }
    
    /*
    void Epoll::startListen(const int &fd,const condition_variable &cv1,const bool &flag,const int &evsNum)
    {
        if(isListen())
        {
            endListen();
        }
        this->fd=fd;
        this->flag=flag;
        this->cv1=cv1;
        this->evsNum=evsNum;
        thread(&EpollSingle::epolll,this).detach();
    }
    void Epoll::epolll()
    {
        int epollFD=epoll_create(1);
        epoll_event ev;
        ev.data.fd=fd;
        if(flag==false)
            ev.events=EPOLLIN|EPOLLET;
        else
            ev.events=EPOLLIN;
        epoll_ctl(epollFD,EPOLL_CTL_ADD,fd,&ev);
        epoll_event evs[evsNum];
    }
    */
    
    void stt::network::WebSocketClient::close(const short &code,const string &message,const bool &wait)
    {
        if(!isConnect())//没有连接 何来关闭
            return;
        const uint16_t networkCode=htons(static_cast<uint16_t>(code));
        char ccode[2];
        memcpy(ccode,&networkCode,2);
        string codee(ccode,2);
        codee+=message;
        if(!sendMessage(codee,"1000"))//如果发送失败 说明连接可能断了或者有其他错误 自行终止本方连接就ok了,否则等待epoll那边收到关闭帧再关闭
        {
            if(wait)
                k.endListenWithSignal();//发送停止信号
        }
        if(wait)
        {
            k.waitAndQuit();
            while(k.isListen());//wb的标志位会比epoll的标志位先确定，epoll是最后才改变的标志位，所以监听最后的epoll标志位更安全
        }
        flag5=true;//为了省事，一概设置flag5为true，如果是主动的那就设置了，后面能用；被动的后面也不需要在检查这个了。
    }
    void stt::network::WebSocketClient::close(const string &closeCodeAndMessage,const bool &wait)
    {
        if(!isConnect())//没有连接 何来关闭
            return;
        if(!sendMessage(closeCodeAndMessage,"1000"))//如果发送失败 说明连接可能断了或者有其他错误 自行终止本方连接就ok了,否则等待epoll那边收到关闭帧再关闭
        {
            if(wait)
                k.endListenWithSignal();//发送停止信号
        }
        if(wait)
        {
            k.waitAndQuit();
            while(k.isListen());
        }
        flag5=true;//为了省事，一概设置flag5为true，如果是主动的那就设置了，后面能用；被动的后面也不需要在检查这个了。
    }
    bool stt::network::WebSocketClient::close1()
    {
        flag4=false;
        flag5=false;
        if(!TcpClient::close())
        {
            cerr<<"wb无法关闭前一个连接(tcp连接已经关闭但是没办法创建新的套接字，这个对象需要弃用)"<<endl;
            return false;
        }
        return true;
    }
    stt::network::WebSocketClient::~WebSocketClient()
    {
        close(1000,"bye");
    }
    bool stt::network::WebSocketClient::connect(const string &url,const int &min)
    {
        //解析url
        //建立tcp连接
        string ip;
        int port;
        string locPara;
        HttpStringUtil::getIP(url,ip);
        HttpStringUtil::getPort(url,port);
        HttpStringUtil::getLocPara(url,locPara);
        if(!TcpClient::isConnect()||TcpClient::getServerIP()!=ip||TcpClient::getServerPort()!=port)//没有连接或者服务器变更需要重新连接
        {
            if(TcpClient::isConnect())//如果是变更服务器 需要先关闭原有的连接
            {
                if(!TcpClient::close())
                {
                    cerr<<"wb无法关闭前一个连接"<<endl;
                    return false;
                }
            }
            if(!TcpClient::connect(ip,port))//重新连接或者第一次连接
            {
                cerr<<"wb无法连接到服务器"<<endl;
                return false;
            }
        }
        //进行websocket握手
        const auto handshakeFailed=[this]() {
            flag4=false;
            flag5=false;
            (void)TcpClient::close();
            return false;
        };

        string httpURL=url;
        if(httpURL.rfind("ws://",0)==0||httpURL.rfind("wss://",0)==0)
            httpURL.replace(0,2,"http");
        else
            return handshakeFailed();

        // RFC 6455: Sec-WebSocket-Key 必须是 16 字节随机值的 Base64 编码。
        unsigned char nonce[16]{};
        if(RAND_bytes(nonce,static_cast<int>(sizeof(nonce)))!=1)
            return handshakeFailed();
        string wbKey=EncodingUtil::base64_encode(
            string(reinterpret_cast<const char*>(nonce),sizeof(nonce)));

        HttpClient k;
        if(!k.getRequestFromFD(TcpFDHandler::getFD(),TcpFDHandler::ssl,httpURL,HttpStringUtil::createHeader("Upgrade","websocket","Connection","Upgrade","Sec-WebSocket-Key",wbKey),"Sec-WebSocket-Version: 13"))
            return handshakeFailed();
        const size_t statusLineEnd=k.header.find("\r\n");
        const string_view statusLine(k.header.data(),
            statusLineEnd==string::npos?k.header.size():statusLineEnd);
        if(!k.isReturn()||!isSwitchingProtocolsStatus(statusLine))
            return handshakeFailed();

        // HTTP 字段名和 Upgrade/Connection token 均不区分 ASCII 大小写。
        string upgradeValue;
        string connectionValue;
        string acceptValue;
        if(!getHttpHeaderValueCaseInsensitive(k.header,"Upgrade",upgradeValue)||
           !httpHeaderContainsToken(upgradeValue,"websocket")||
           !getHttpHeaderValueCaseInsensitive(k.header,"Connection",connectionValue)||
           !httpHeaderContainsToken(connectionValue,"Upgrade")||
           !getHttpHeaderValueCaseInsensitive(k.header,"Sec-WebSocket-Accept",acceptValue))
            return handshakeFailed();

        string expectedAccept=wbKey+"258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
        string digest;
        CryptoUtil::sha1(expectedAccept,digest);
        expectedAccept=EncodingUtil::base64_encode(digest);
        if(acceptValue!=expectedAccept)
            return handshakeFailed();
        //连接完毕
        this->url=url;
        flag4=true;
        flag5=false;
        if(this->k.isListen())
            this->k.endListen();
        //监听,用水平触发的逻辑
        auto ffc=[this](const int &fd)->bool
        {
            TcpFDHandler k;
            k.setFD(fd,this->ssl);
            string totalResult="";
            char b1;
            char b2;
            string code;
            bool isRec;
            do
            {
                isRec=true;
                if(k.recvDataByLength(&b1,1)<=0)
                {   
                    //if(!this->close(1002,"",false))
                    //{
                    //    cout<<"false"<<endl;
                        return false;
                    //}
                }
                if(k.recvDataByLength(&b2,1)<=0)
                {   
                    //if(!this->close(1002,"",false))
                    //{
                        return false;
                    //}
                }
                
                
                unsigned long sizee;//payload len
                string ssize;
                BitUtil::bitOutput(b2,ssize);
                ssize=ssize.substr(1);
                BitUtil::bitStrToNumber(ssize,sizee);//单字节里的是大端序 
                if(sizee==126)
                {
                    char s[2];
                    if(k.recvDataByLength(s,2)<=0)
                    {   
                        //if(!this->close(1002,"",false))
                        //{
                            return false;
                        //}
                    }
                    sizee=BitUtil::bitToNumber(string(s,2),sizee);
                }
                else if(sizee==127)
                {
                    char s[8];
                    if(k.recvDataByLength(s,8)<=0)
                    {   
                        //if(!this->close(1002,"",false))
                        //{
                            return false;
                        //}
                    }
                    sizee=BitUtil::bitToNumber(string(s,8),sizee);
                }
                if(sizee==0)//长度为0只可能是控制帧，但控制帧最大长度125，不允许分片
                    isRec=false;
                //接收正文
                string result;
                if(isRec)
                {
                    if(k.recvDataByLength(result,sizee)<=0)
                    {
                        //if(!this->close(1002,"",false))
                        //{
                            return false;
                        //}
                    }
                    totalResult+=result;
                }
                //检查收到的讯息
                
                BitUtil::bitOutput(b1,code);
                code=code.substr(4);
                //cout<<"finish+"<<code<<endl;
                if(code=="1000")
                {
                    //cout<<"收到对端关闭帧";
                    if(this->flag5)//说明是关闭确认帧
                    {
                        return false;
                    }
                    else//需要发送关闭确认帧
                    {
                        //if(!this->close(totalResult,false))
                        //{
                        //    return false;
                        //}
                        //isRec=false;
                        this->close(totalResult,false);
                        return false;
                    }
                }
                else if(code=="1001")
                {
                    //cout<<"收到对端心跳:"<<totalResult<<endl;
                    this->sendMessage(totalResult,"1010");
                    isRec=false;
                }
                else if(code=="1010")
                {
                    //cout<<"收到对端心跳响应"<<endl;
                    isRec=false;
                }
                BitUtil::bitOutput_bit(b1,1,b1);
            }while(BitUtil::bitOutput(b1,code).substr(0,1)=="0"&&BitUtil::bitOutput(b1,code).substr(4)=="0000"&&isRec);  

            //用回调函数处理
            if(isRec)
                return this->fc(totalResult,*this);//处理失败epoll会退出
            return true;
        };
        auto endfc=[this](const int &)->void
        {
            this->close1();
        };
        auto timeoutfc=[this](const int &)->bool
        {
            if(!this->sendMessage("心跳","1001"))
                return false;
            this->k.waitAndQuit();
            return true;
        };
        this->k.setFunction(ffc);
        this->k.setEndFunction(endfc);
        this->k.setTimeOutFunction(timeoutfc);
        this->k.startListen(TcpFDHandler::getFD(),true,Duration{0,0,min,0,0});
        return this->k.isListen();
    }
    static bool isValidWebSocketUtf8(std::string_view value);

    static bool buildWebSocketFrame(const std::string &message,const std::string &type,const bool masked,std::string &frame)
    {
        uint8_t opcode=0;
        if(type=="0001") opcode=0x1;
        else if(type=="0010") opcode=0x2;
        else if(type=="1000") opcode=0x8;
        else if(type=="1001") opcode=0x9;
        else if(type=="1010") opcode=0xA;
        else return false;
        if(opcode>=0x8&&message.size()>125)
            return false;
        if(opcode==0x1&&!isValidWebSocketUtf8(message))
            return false;
        if(opcode==0x8)
        {
            if(message.size()==1)
                return false;
            if(message.size()>=2)
            {
                const uint16_t closeCode=(static_cast<uint16_t>(static_cast<uint8_t>(message[0]))<<8)|
                                         static_cast<uint8_t>(message[1]);
                if(closeCode<1000||closeCode>=5000||closeCode==1004||closeCode==1005||
                   closeCode==1006||closeCode==1015||(closeCode>=1016&&closeCode<=2999)||
                   (message.size()>2&&!isValidWebSocketUtf8(std::string_view(message).substr(2))))
                    return false;
            }
        }

        const uint64_t length=message.size();
        frame.clear();
        frame.reserve(message.size()+14);
        frame.push_back(static_cast<char>(0x80U|opcode));
        const uint8_t maskBit=masked?0x80U:0U;
        if(length<=125)
        {
            frame.push_back(static_cast<char>(maskBit|static_cast<uint8_t>(length)));
        }
        else if(length<=65535)
        {
            frame.push_back(static_cast<char>(maskBit|126U));
            frame.push_back(static_cast<char>((length>>8)&0xffU));
            frame.push_back(static_cast<char>(length&0xffU));
        }
        else
        {
            frame.push_back(static_cast<char>(maskBit|127U));
            for(int shift=56;shift>=0;shift-=8)
                frame.push_back(static_cast<char>((length>>shift)&0xffU));
        }
        if(!masked)
        {
            frame.append(message);
            return true;
        }
        std::string mask;
        EncodingUtil::generateMask_4(mask);
        if(mask.size()!=4)
            return false;
        frame.append(mask);
        const size_t payloadStart=frame.size();
        frame.resize(payloadStart+message.size());
        for(size_t index=0;index<message.size();++index)
            frame[payloadStart+index]=static_cast<char>(static_cast<unsigned char>(message[index])^
                                                        static_cast<unsigned char>(mask[index%4]));
        return true;
    }

    static bool isValidWebSocketUtf8(const std::string_view value)
    {
        size_t index=0;
        const auto continuation=[&value](const size_t position) {
            return position<value.size()&&
                   (static_cast<uint8_t>(value[position])&0xC0U)==0x80U;
        };
        while(index<value.size())
        {
            const uint8_t first=static_cast<uint8_t>(value[index]);
            if(first<=0x7fU)
            {
                ++index;
                continue;
            }
            if(first>=0xC2U&&first<=0xDFU)
            {
                if(!continuation(index+1)) return false;
                index+=2;
                continue;
            }
            if(first>=0xE0U&&first<=0xEFU)
            {
                if(index+2>=value.size()||!continuation(index+2)) return false;
                const uint8_t second=static_cast<uint8_t>(value[index+1]);
                if((first==0xE0U&&(second<0xA0U||second>0xBFU))||
                   (first==0xEDU&&(second<0x80U||second>0x9FU))||
                   (first!=0xE0U&&first!=0xEDU&&!continuation(index+1)))
                    return false;
                index+=3;
                continue;
            }
            if(first>=0xF0U&&first<=0xF4U)
            {
                if(index+3>=value.size()||!continuation(index+2)||!continuation(index+3))
                    return false;
                const uint8_t second=static_cast<uint8_t>(value[index+1]);
                if((first==0xF0U&&(second<0x90U||second>0xBFU))||
                   (first==0xF4U&&(second<0x80U||second>0x8FU))||
                   (first!=0xF0U&&first!=0xF4U&&!continuation(index+1)))
                    return false;
                index+=4;
                continue;
            }
            return false;
        }
        return true;
    }

    bool stt::network::WebSocketClient::sendMessage(const string &message,const string &type)
    {
        if(!isConnect())
        {
            cerr<<"websocket对象没有连接,发送失败"<<endl;
            return false;
        }
        string frame;
        if(!buildWebSocketFrame(message,type,true,frame))
            return false;
        return sendData(frame)==static_cast<int>(frame.size());

    }
    
    int stt::network::WebSocketServerFDHandler::getMessage(TcpFDInf &Tcpinf,WebSocketFDInformation &Websocketinf,const unsigned long &buffer_size,const int &ii)
    {
        if(buffer_size<6)
            return -1;
        int receiveResult=-100;
        if(ii==1)
        {
            if(!ensureReceiveBuffer(Tcpinf,buffer_size,std::min(8192UL,buffer_size)))
                return -1;
            receiveResult=1;
            while(receiveResult>0)
            {
                if(Tcpinf.p_buffer_now==Tcpinf.buffer_capacity)
                {
                    if(Tcpinf.buffer_capacity>=buffer_size||
                       !ensureReceiveBuffer(Tcpinf,buffer_size,Tcpinf.buffer_capacity+1))
                        break;
                }
                receiveResult=recvData(Tcpinf.buffer+Tcpinf.p_buffer_now,Tcpinf.buffer_capacity-Tcpinf.p_buffer_now);
                if(receiveResult>0)
                    Tcpinf.p_buffer_now+=static_cast<unsigned long>(receiveResult);
            }
        }
        if(Tcpinf.p_buffer_now==0)
            return receiveResult==-100?4:-1;

        while(true)
        {
            const std::string_view input(Tcpinf.buffer,Tcpinf.p_buffer_now);
            if(input.size()<2)
                return input.size()>=buffer_size?-1:4;
            const uint8_t first=static_cast<uint8_t>(input[0]);
            const uint8_t second=static_cast<uint8_t>(input[1]);
            const bool fin=(first&0x80U)!=0;
            const uint8_t opcode=first&0x0fU;
            if((first&0x70U)!=0||(second&0x80U)==0)
                return -1;
            if(opcode!=0x0&&opcode!=0x1&&opcode!=0x2&&opcode!=0x8&&opcode!=0x9&&opcode!=0xA)
                return -1;

            uint64_t payloadLength=second&0x7fU;
            size_t cursor=2;
            if(payloadLength==126)
            {
                if(input.size()<4) return input.size()>=buffer_size?-1:4;
                payloadLength=(static_cast<uint64_t>(static_cast<uint8_t>(input[2]))<<8)|
                              static_cast<uint8_t>(input[3]);
                if(payloadLength<126) return -1;
                cursor=4;
            }
            else if(payloadLength==127)
            {
                if(input.size()<10) return input.size()>=buffer_size?-1:4;
                if((static_cast<uint8_t>(input[2])&0x80U)!=0) return -1;
                payloadLength=0;
                for(size_t index=2;index<10;++index)
                    payloadLength=(payloadLength<<8)|static_cast<uint8_t>(input[index]);
                if(payloadLength<=65535) return -1;
                cursor=10;
            }
            const bool controlFrame=opcode>=0x8;
            if(controlFrame&&(!fin||payloadLength>125))
                return -1;
            if(payloadLength>buffer_size||cursor>buffer_size-4)
                return -1;
            const size_t payloadSize=static_cast<size_t>(payloadLength);
            const size_t frameSize=cursor+4+payloadSize;
            if(frameSize>buffer_size)
                return -1;
            if(input.size()<frameSize)
                return input.size()>=buffer_size?-1:4;

            Websocketinf.mask.assign(input.data()+cursor,4);
            cursor+=4;
            std::string payload(input.data()+cursor,payloadSize);
            for(size_t index=0;index<payload.size();++index)
                payload[index]=static_cast<char>(static_cast<unsigned char>(payload[index])^
                                                 static_cast<unsigned char>(Websocketinf.mask[index%4]));
            const size_t remaining=input.size()-frameSize;
            if(remaining>0)
                memmove(Tcpinf.buffer,Tcpinf.buffer+frameSize,remaining);
            Tcpinf.p_buffer_now=remaining;
            Tcpinf.data={};
            Tcpinf.status=0;
            Websocketinf.fin=fin;

            if(opcode==0x8)
            {
                if(payload.size()==1) return -1;
                if(payload.size()>=2)
                {
                    const uint16_t closeCode=(static_cast<uint16_t>(static_cast<uint8_t>(payload[0]))<<8)|
                                             static_cast<uint8_t>(payload[1]);
                    if(closeCode<1000||closeCode>=5000||closeCode==1004||closeCode==1005||
                       closeCode==1006||closeCode==1015||(closeCode>=1016&&closeCode<=2999))
                        return -1;
                    if(payload.size()>2&&!isValidWebSocketUtf8(std::string_view(payload).substr(2)))
                        return -1;
                }
                Websocketinf.message=std::move(payload);
                Websocketinf.message_type=1;
                return 1;
            }
            if(opcode==0x9)
            {
                Websocketinf.message=std::move(payload);
                Websocketinf.message_type=3;
                return 3;
            }
            if(opcode==0xA)
            {
                Websocketinf.message=std::move(payload);
                Websocketinf.message_type=2;
                return 2;
            }
            if(opcode==0x0)
            {
                if(Websocketinf.fragmented_opcode==0||Websocketinf.fragmented_message.size()>buffer_size||
                   payload.size()>buffer_size-Websocketinf.fragmented_message.size())
                    return -1;
                Websocketinf.fragmented_message+=payload;
                if(fin)
                {
                    const uint8_t messageOpcode=Websocketinf.fragmented_opcode;
                    Websocketinf.message=std::move(Websocketinf.fragmented_message);
                    Websocketinf.fragmented_message.clear();
                    Websocketinf.fragmented_opcode=0;
                    if(messageOpcode==0x1&&!isValidWebSocketUtf8(Websocketinf.message))
                        return -1;
                    Websocketinf.message_type=0;
                    return 0;
                }
            }
            else
            {
                if(Websocketinf.fragmented_opcode!=0)
                    return -1;
                if(fin)
                {
                    Websocketinf.message=std::move(payload);
                    if(opcode==0x1&&!isValidWebSocketUtf8(Websocketinf.message))
                        return -1;
                    Websocketinf.message_type=0;
                    return 0;
                }
                Websocketinf.fragmented_opcode=opcode;
                Websocketinf.fragmented_message=std::move(payload);
            }
            if(Tcpinf.p_buffer_now==0)
                return 4;
        }

    }
    bool stt::network::WebSocketServerFDHandler::sendMessage(const string &msg,const string &type)
    {
        string frame;
        if(!buildWebSocketFrame(msg,type,false,frame))
            return false;
        return sendData(frame,false)==static_cast<int>(frame.size());

    }
    /*
    void WebSocketServerFDHandler::closeAck(const string &closeCodeAndMessage)
    {
        sendMessage(closeCodeAndMessage,"1000");
        TcpFDHandler::close();
    }
    void WebSocketServerFDHandler::closeAck(const short &code,const string &message)
    {
        char ccode[2];
        memcpy(ccode,&code,2);
        string codee(ccode,2);
        codee+=message;
        sendMessage(codee,"1000");
        TcpFDHandler::close();
    }
    */
    void stt::network::WebSocketServer::closeAck(const int &fd,const string &closeCodeAndMessage)
    {
        uint64_t connection=0;
        {
            std::lock_guard<std::mutex> lock(websocketRegistryMutex);
            const auto connectionIt=websocketConnections.find(fd);
            if(connectionIt==websocketConnections.end())
                return;
            connection=connectionIt->second;
            websocketClosing.insert(fd);
        }
        sendMessageForConnection(fd,connection,closeCodeAndMessage,"1000");
        requestCloseAfterFlush(fd,connection);
    }
    void stt::network::WebSocketServer::closeAck(const int &fd,const short &code,const string &message)
    {
        uint64_t connection=0;
        {
            std::lock_guard<std::mutex> lock(websocketRegistryMutex);
            const auto connectionIt=websocketConnections.find(fd);
            if(connectionIt==websocketConnections.end())
                return;
            connection=connectionIt->second;
            websocketClosing.insert(fd);
        }
        const uint16_t networkCode=htons(static_cast<uint16_t>(code));
        char ccode[2];
        memcpy(ccode,&networkCode,2);
        string codee(ccode,2);
        codee+=message;
        sendMessageForConnection(fd,connection,codee,"1000");
        requestCloseAfterFlush(fd,connection);
    }
    bool stt::network::WebSocketServer::closeFD(const int &fd,const string &closeCodeAndMessage)
    {
        uint64_t connection=0;
        {
            std::lock_guard<std::mutex> lock(websocketRegistryMutex);
            const auto connectionIt=websocketConnections.find(fd);
            if(connectionIt==websocketConnections.end()||
               !websocketClosing.insert(fd).second)
                return false;
            connection=connectionIt->second;
        }
        if(!sendMessageForConnection(fd,connection,closeCodeAndMessage,"1000"))
        {
            std::lock_guard<std::mutex> lock(websocketRegistryMutex);
            websocketClosing.erase(fd);
            return false;
        }
        requestCloseAfterFlush(fd,connection);
        return true;
    }
    
    bool stt::network::WebSocketServer::closeFD(const int &fd,const short &code,const string &message)
    {
        uint64_t connection=0;
        {
            std::lock_guard<std::mutex> lock(websocketRegistryMutex);
            const auto connectionIt=websocketConnections.find(fd);
            if(connectionIt==websocketConnections.end()||
               !websocketClosing.insert(fd).second)
                return false;
            connection=connectionIt->second;
        }
        const uint16_t networkCode=htons(static_cast<uint16_t>(code));
        char ccode[2];
        memcpy(ccode,&networkCode,2);
        string codee(ccode,2);
        codee+=message;
        if(!sendMessageForConnection(fd,connection,codee,"1000"))
        {
            std::lock_guard<std::mutex> lock(websocketRegistryMutex);
            websocketClosing.erase(fd);
            return false;
        }
        requestCloseAfterFlush(fd,connection);
        return true;
    }
    
    bool stt::network::WebSocketServer::close(const int &fd)
    {
        WebSocketServerFDHandler handler;
        prepareQueuedHandler(handler,fd);
        if(handler.getFD()<0)
            return false;
        handler.close();
        return true;
    }
    bool stt::network::WebSocketServer::closeWithoutLock(const int &fd,const string &closeCodeAndMessage)
    {
        auto ii=wbclientfd.find(fd);
        if(ii==wbclientfd.end()||ii->second.closeflag==true)
        {
            return false;
        }
        else
        {
            WebSocketServerFDHandler k;
            prepareHandler(k,fd);
            if(!k.sendMessage(closeCodeAndMessage,"1000"))
            {
                TcpServer::close(fd);
                return false;
            }
            ii->second.closeflag=true;
            {
                std::lock_guard<std::mutex> lock(websocketRegistryMutex);
                websocketClosing.insert(fd);
            }
            return true;
        }
    }
    bool stt::network::WebSocketServer::closeWithoutLock(const int &fd,const short &code,const string &message)
    {
        auto ii=wbclientfd.find(fd);
        if(ii==wbclientfd.end()||ii->second.closeflag==true)//找不到或者已经发送过了
        {
            return false;
        }
        else
        {
            const uint16_t networkCode=htons(static_cast<uint16_t>(code));
            char ccode[2];
            memcpy(ccode,&networkCode,2);
            string codee(ccode,2);
            codee+=message;
            WebSocketServerFDHandler k;
            prepareHandler(k,fd);
            if(!k.sendMessage(codee,"1000"))//发送失败会自动删除在记录表里的
            {
                TcpServer::close(fd);
                return false;
            }
            ii->second.closeflag=true;
            {
                std::lock_guard<std::mutex> lock(websocketRegistryMutex);
                websocketClosing.insert(fd);
            }
            return true;
        }
    }
    /*
    void stt::network::WebSocketServer::handler(const int &fd)
    {
        HttpServerFDHandler k;
        WebSocketServerFDHandler k1;
            if(clientfd[fd].fd!=-1)
            {
                if(security_open)
                {
                    if(!connectionLimiter.allowRequest(clientfd[fd].ip))
                    {
                        TcpServer::close(fd);
                        if(stt::system::ServerSetting::logfile!=nullptr)
                        {
                            if(stt::system::ServerSetting::language=="Chinese")
                                stt::system::ServerSetting::logfile->writeLog("tcp server consumer : fd= "+to_string(fd)+"请求太频繁，已经关闭连接");
                            else
                                stt::system::ServerSetting::logfile->writeLog("tcp server consumer : fd= "+to_string(fd)+"request are too frequent,now has closed this connection");
                        }
                      
                    }
                }
                
                TcpFDInf &Tcpinf=clientfd[fd];

            cout<<"websocket fd="<<fd<<endl;
            if(stt::system::ServerSetting::logfile!=nullptr)
             {
                if(stt::system::ServerSetting::language=="Chinese")
                    stt::system::ServerSetting::logfile->writeLog("websocket server consumer : 正在处理fd= "+to_string(fd));
                else
                    stt::system::ServerSetting::logfile->writeLog("websocket server consumer : now solveing fd= "+to_string(fd));
             }
            //拿到fd之后开始操作
            unique_lock<mutex> lock(lwb);
            auto jj=wbclientfd.find(fd);
            if(jj==wbclientfd.end())//没有进行wb握手
            {
                prepareHandler(k,fd);

                if(stt::system::ServerSetting::logfile!=nullptr)
                {
                    if(stt::system::ServerSetting::language=="Chinese")
                        stt::system::ServerSetting::logfile->writeLog("websocket server consumer : fd= "+to_string(fd)+" 正在进行websocket握手");
                    else
                        stt::system::ServerSetting::logfile->writeLog("websocket server consumer : fd= "+to_string(fd)+" handshaking websocket...");
                }
                WebSocketFDInformation winf;
                winf.fd=fd;
                winf.closeflag=false;
                
                HttpRequestInformation HttpInf;
                int ret=k.solveRequest(Tcpinf,HttpInf,buffer_size,1);
                if(ret==-1)
                {
                    //k.close();

                        TcpServer::close(fd);
                        cout<<"无法解析http请求 wb握手失败"<<endl;
                        if(stt::system::ServerSetting::logfile!=nullptr)
                        {
                            if(stt::system::ServerSetting::language=="Chinese")
                                stt::system::ServerSetting::logfile->writeLog("websocket server consumer : fd= "+to_string(fd)+" 无法解析http请求或者对端关闭连接 wb握手失败 已经关闭这个连接");
                            else
                                stt::system::ServerSetting::logfile->writeLog("websocket server consumer : fd= "+to_string(fd)+" couldn't solve http request or host had closed this connection.fail to handshake websocket.have closed this connection");
                        }
                   
                    //wb握手失败
                }
                else if(ret==0)
                {
                    if(stt::system::ServerSetting::logfile!=nullptr)
                    {
                        if(stt::system::ServerSetting::language=="Chinese")
                            stt::system::ServerSetting::logfile->writeLog("websocket server consumer : 解析fd= "+to_string(fd)+"未完成 等待新的数据继续解析");
                        else
                            stt::system::ServerSetting::logfile->writeLog("websocket server consumer : fd= "+to_string(fd)+"wait new data to continue solve this request");
                    }
                 
                }
                else if(ret==1)
                {
                    winf.locPara=HttpInf.locPara;
                    winf.header=HttpInf.header;
                    //cout<<winf.header<<endl;
                    if(!fcc(winf))//条件不满足
                    {
                        //k.close();
                        TcpServer::close(fd);
                        cout<<"连接限制条件不满足 wb握手失败"<<endl;
                        if(stt::system::ServerSetting::logfile!=nullptr)
                        {
                            if(stt::system::ServerSetting::language=="Chinese")
                                stt::system::ServerSetting::logfile->writeLog("websocket server consumer : fd= "+to_string(fd)+" 连接限制条件不满足 websocket握手失败 服务器已经关闭这个连接");
                            else
                                stt::system::ServerSetting::logfile->writeLog("websocket server consumer : fd= "+to_string(fd)+" The connection constraints are not met.websocket handshake fail.server has closed this connection.");
                        }
                    
                    }
                    string_view key;
                    string keyy;
                    HttpStringUtil::get_value_header(HttpInf.header,key,"Sec-WebSocket-Key");
                    keyy.assign(key);
                    keyy+="258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
                    string result="";
                    CryptoUtil::sha1(string(keyy),result);
                    keyy=EncodingUtil::base64_encode(result);
                    result=HttpStringUtil::createHeader("Upgrade","websocket","Connection","Upgrade","Sec-WebSocket-Accept",keyy);
                    //清理接收缓冲区可能的数据遗漏
                    //Tcpinf.data={};
                    
                    if(!k.sendBack("",result,"101 Switching Protocols"))
                    {
                        //k.close();
                        TcpServer::close(fd);
                        cout<<"握手响应无法发送 wb握手失败"<<endl;
                        if(stt::system::ServerSetting::logfile!=nullptr)
                        {
                            if(stt::system::ServerSetting::language=="Chinese")
                                stt::system::ServerSetting::logfile->writeLog("websocket server consumer : fd= "+to_string(fd)+" 握手响应无法发送 websocket握手失败 服务器已经关闭这个连接");
                            else
                                stt::system::ServerSetting::logfile->writeLog("websocket server consumer : fd= "+to_string(fd)+" couldn't send handshake response .websocket handshake fail. server has closed this connection");
                        }
                   
                        //握手失败
                    }
                    winf.response=::time(0);
                    winf.HBTime=0;
                    wbclientfd.emplace(fd,winf);
                    if(stt::system::ServerSetting::logfile!=nullptr)
                    {
                            if(stt::system::ServerSetting::language=="Chinese")
                                stt::system::ServerSetting::logfile->writeLog("websocket server consumer : fd= "+to_string(fd)+" websocket握手成功");
                            else
                                stt::system::ServerSetting::logfile->writeLog("websocket server consumer : fd= "+to_string(fd)+" websocket has handshaked sucessfully");
                    }
                    thread(fccc,winf,ref(*this)).detach();
                }
                //sleep(10);
                //k.solveRequest(Tcpinf,HttpInf,buffer_size,1);
                //sleep(5);

            }
            else//已经进行了握手操作
            {
                //k.setFD(cclientfd.fd,clientfd[cclientfd.fd].ssl,unblock);
                prepareHandler(k1,fd);
            int r=1;
            
            
                if(stt::system::ServerSetting::logfile!=nullptr)
                {
                    if(stt::system::ServerSetting::language=="Chinese")
                        stt::system::ServerSetting::logfile->writeLog("websocket server consumer : fd= "+to_string(fd)+" 正在解析请求... ");
                    else
                        stt::system::ServerSetting::logfile->writeLog("websocket server consumer : fd= "+to_string(fd)+" now solveing request... ");
                }
                r=k1.getMessage(Tcpinf,jj->second,buffer_size,1);

                if(r==-1)
                {
                    if(stt::system::ServerSetting::logfile!=nullptr)
                    {
                        if(stt::system::ServerSetting::language=="Chinese")
                            stt::system::ServerSetting::logfile->writeLog("websocket server consumer : fd= "+to_string(fd)+" 接收错误，关闭连接中 ");
                        else 
                            stt::system::ServerSetting::logfile->writeLog("websocket server consumer : fd= "+to_string(fd)+"error of receiving, and now closing this connection ");
                    }
                    closeWithoutLock(fd);
                }
                else if(r==1)
                {
                    
                    
                   
                    if(jj->second.closeflag==true)//收到关闭确认
                    {
                        if(stt::system::ServerSetting::logfile!=nullptr)
                        {
                        if(stt::system::ServerSetting::language=="Chinese")
                            stt::system::ServerSetting::logfile->writeLog("websocket server consumer : fd= "+to_string(fd)+" 收到关闭确认帧: "+ jj->second.message);
                        else 
                            stt::system::ServerSetting::logfile->writeLog("websocket server consumer : fd= "+to_string(fd)+" has received closed confirm fin:"+ jj->second.message);
                        }
                        TcpServer::close(fd);
                    }
                    else//收到关闭
                    {
                        if(stt::system::ServerSetting::logfile!=nullptr)
                        {
                        if(stt::system::ServerSetting::language=="Chinese")
                            stt::system::ServerSetting::logfile->writeLog("websocket server consumer : fd= "+to_string(fd)+" 收到关闭帧: "+ jj->second.message);
                        else 
                            stt::system::ServerSetting::logfile->writeLog("websocket server consumer : fd= "+to_string(fd)+" has received closed fin:"+ jj->second.message);
                        }
                        cout<<"yes"<<endl;
                        closeAck(fd,jj->second.message);
                    }
                    wbclientfd.erase(jj);
                   
                }
                else if(r==2)
                {
                    if(stt::system::ServerSetting::logfile!=nullptr)
                    {
                        if(stt::system::ServerSetting::language=="Chinese")
                            stt::system::ServerSetting::logfile->writeLog("websocket server consumer : fd= "+to_string(fd)+" 收到心跳确认: "+ jj->second.message);
                        else 
                            stt::system::ServerSetting::logfile->writeLog("websocket server consumer : fd= "+to_string(fd)+" has received heartbeat confirm:"+ jj->second.message);
                    }
                    jj->second.response=::time(0);
                    jj->second.HBTime=0;

                    //记录心跳确认

                    jj->second.message="";
                   
                }
                else if(r==3)
                {
                    if(stt::system::ServerSetting::logfile!=nullptr)
                    {
                        if(stt::system::ServerSetting::language=="Chinese")
                            stt::system::ServerSetting::logfile->writeLog("websocket server consumer : fd= "+to_string(fd)+" 收到心跳: "+ jj->second.message);
                        else 
                            stt::system::ServerSetting::logfile->writeLog("websocket server consumer : fd= "+to_string(fd)+" has received heartbeat:"+ jj->second.message);
                    }
                    jj->second.response=::time(0);
                    if(!sendMessage(jj->first,"心跳","1010"))//发送心跳失败直接关闭
                    {
                        wbclientfd.erase(jj);
                        TcpServer::close(jj->first);
                        
                    }
         
                    //心跳
                    else
                        jj->second.message="";
                    
                }
                else if(r==0)//正常报文
                {
                    if(stt::system::ServerSetting::logfile!=nullptr)
                    {
                        if(stt::system::ServerSetting::language=="Chinese")
                            stt::system::ServerSetting::logfile->writeLog("websocket server consumer : fd= "+to_string(fd)+" 收到常规信息: "+ jj->second.message);
                        else 
                            stt::system::ServerSetting::logfile->writeLog("websocket server consumer : fd= "+to_string(fd)+" has received normal message:"+ jj->second.message);
                    }
                    jj->second.response=::time(0);
                    if(!fc(jj->second.message,*this,jj->second))//回调函数失败
                    {
                        close(fd);
                        
                    }
                    jj->second.message="";
                    
                }
                //else if(r==4)
                //{
                //        solvingFD_lock.lock();
                //        solvingFD[cclientfd]=false;
                //        solvingFD_lock.unlock();
                //        continue;
                //}

                if(r!=4)
                {

                    if(stt::system::ServerSetting::logfile!=nullptr)
                    {
                        if(stt::system::ServerSetting::language=="Chinese")
                            stt::system::ServerSetting::logfile->writeLog("websocket server consumer : fd= "+to_string(fd)+" 处理完成");
                        else 
                            stt::system::ServerSetting::logfile->writeLog("websocket server consumer : fd= "+to_string(fd)+" has solved sucessfully");
                    }
                }
                else
                {
                    if(stt::system::ServerSetting::logfile!=nullptr)
                    {
                        if(stt::system::ServerSetting::language=="Chinese")
                            stt::system::ServerSetting::logfile->writeLog("websocket server consumer : fd= "+to_string(fd)+" 继续等待数据中");
                        else 
                            stt::system::ServerSetting::logfile->writeLog("websocket server consumer : fd= "+to_string(fd)+" waiting data...");
                    }
                }
            
            }
            }
    }
    */
    /*
    void stt::network::WebSocketServer::consumer(const int &threadID)
    {
        HttpServerFDHandler k;
        //TcpFDInf &Tcpinf;
        WebSocketServerFDHandler k1;
        
        if(stt::system::ServerSetting::logfile!=nullptr)
        {
            if(stt::system::ServerSetting::language=="Chinese")
                stt::system::ServerSetting::logfile->writeLog("websocket server consumer "+to_string(threadID)+" 打开");
            else
                stt::system::ServerSetting::logfile->writeLog("websocket server consumer "+to_string(threadID)+" has opened");
        }
        while(flag1)
        {
            unique_lock<mutex> ul1(lq1[threadID]);
            while(fdQueue[threadID].empty()&&flag1)
            {
                cv[threadID].wait(ul1);
            }
            if(!flag1)
            {
                ul1.unlock();
                break;
            }
            QueueFD cclientfd=fdQueue[threadID].front();
            fdQueue[threadID].pop();
            
            ul1.unlock();

            if(cclientfd.close)
            {
                    auto ii=wbclientfd.find(cclientfd.fd);
                    if(ii!=wbclientfd.end())
                    {
                        TcpServer::close(cclientfd.fd);
                        wbclientfd.erase(ii);
                    }
                continue;
            }


            //unique_lock<mutex> lock6(lc1);
            //auto ii=clientfd.find(cclientfd.fd);
            if(clientfd[cclientfd.fd].fd==-1)//can not find fd information,we need to writedown this error and close this fd
            {
                //lock6.unlock();
                continue;
            }
            else
            {
                if(security_open)
                {
                    if(!connectionLimiter.allowRequest(clientfd[cclientfd.fd].ip))
                    {
                        TcpServer::close(cclientfd.fd);
                        if(stt::system::ServerSetting::logfile!=nullptr)
                        {
                            if(stt::system::ServerSetting::language=="Chinese")
                                stt::system::ServerSetting::logfile->writeLog("tcp server consumer "+to_string(threadID)+" : fd= "+to_string(cclientfd.fd)+"请求太频繁，已经关闭连接");
                            else
                                stt::system::ServerSetting::logfile->writeLog("tcp server consumer "+to_string(threadID)+" : fd= "+to_string(cclientfd.fd)+"request are too frequent,now has closed this connection");
                        }
                        continue;
                    }
                }
                //k.setFD(cclientfd.fd,clientfd[cclientfd.fd].ssl,unblock);
                //k1.setFD(cclientfd.fd,clientfd[cclientfd.fd].ssl,unblock);
                TcpFDInf &Tcpinf=clientfd[cclientfd.fd];
            //lock6.unlock();
            cout<<"websocket fd="<<cclientfd.fd<<endl;
            if(stt::system::ServerSetting::logfile!=nullptr)
             {
                if(stt::system::ServerSetting::language=="Chinese")
                    stt::system::ServerSetting::logfile->writeLog("websocket server consumer "+to_string(threadID)+" : 正在处理fd= "+to_string(cclientfd.fd));
                else
                    stt::system::ServerSetting::logfile->writeLog("websocket server consumer "+to_string(threadID)+" : now solveing fd= "+to_string(cclientfd.fd));
             }
            //拿到fd之后开始操作
            unique_lock<mutex> lock(lwb);
            auto jj=wbclientfd.find(cclientfd.fd);
            if(jj==wbclientfd.end())//没有进行wb握手
            {
                prepareHandler(k,cclientfd.fd);
                //k1.setFD(cclientfd.fd,clientfd[cclientfd.fd].ssl,unblock);
                //unique_lock<mutex> lock(lwb);
                if(stt::system::ServerSetting::logfile!=nullptr)
                {
                    if(stt::system::ServerSetting::language=="Chinese")
                        stt::system::ServerSetting::logfile->writeLog("websocket server consumer "+to_string(threadID)+" : fd= "+to_string(cclientfd.fd)+" 正在进行websocket握手");
                    else
                        stt::system::ServerSetting::logfile->writeLog("websocket server consumer "+to_string(threadID)+" : fd= "+to_string(cclientfd.fd)+" handshaking websocket...");
                }
                WebSocketFDInformation winf;
                winf.fd=cclientfd.fd;
                winf.closeflag=false;
                
                HttpRequestInformation HttpInf;
                int ret=k.solveRequest(Tcpinf,HttpInf,buffer_size,1);
                if(ret==-1)
                {
                    //k.close();

                        TcpServer::close(cclientfd.fd);
                        cout<<"无法解析http请求 wb握手失败"<<endl;
                        if(stt::system::ServerSetting::logfile!=nullptr)
                        {
                            if(stt::system::ServerSetting::language=="Chinese")
                                stt::system::ServerSetting::logfile->writeLog("websocket server consumer "+to_string(threadID)+" : fd= "+to_string(cclientfd.fd)+" 无法解析http请求或者对端关闭连接 wb握手失败 已经关闭这个连接");
                            else
                                stt::system::ServerSetting::logfile->writeLog("websocket server consumer "+to_string(threadID)+" : fd= "+to_string(cclientfd.fd)+" couldn't solve http request or host had closed this connection.fail to handshake websocket.have closed this connection");
                        }
                    continue;
                    //wb握手失败
                }
                else if(ret==0)
                {
                    if(stt::system::ServerSetting::logfile!=nullptr)
                    {
                        if(stt::system::ServerSetting::language=="Chinese")
                            stt::system::ServerSetting::logfile->writeLog("websocket server consumer "+to_string(threadID)+" : 解析fd= "+to_string(cclientfd.fd)+"未完成 等待新的数据继续解析");
                        else
                            stt::system::ServerSetting::logfile->writeLog("websocket server consumer "+to_string(threadID)+" : fd= "+to_string(cclientfd.fd)+"wait new data to continue solve this request");
                    }
                    continue;
                }
                else if(ret==1)
                {
                    winf.locPara=HttpInf.locPara;
                    winf.header=HttpInf.header;
                    //cout<<winf.header<<endl;
                    if(!fcc(winf))//条件不满足
                    {
                        //k.close();
                        TcpServer::close(cclientfd.fd);
                        cout<<"连接限制条件不满足 wb握手失败"<<endl;
                        if(stt::system::ServerSetting::logfile!=nullptr)
                        {
                            if(stt::system::ServerSetting::language=="Chinese")
                                stt::system::ServerSetting::logfile->writeLog("websocket server consumer "+to_string(threadID)+" : fd= "+to_string(cclientfd.fd)+" 连接限制条件不满足 websocket握手失败 服务器已经关闭这个连接");
                            else
                                stt::system::ServerSetting::logfile->writeLog("websocket server consumer "+to_string(threadID)+" : fd= "+to_string(cclientfd.fd)+" The connection constraints are not met.websocket handshake fail.server has closed this connection.");
                        }
                        continue;
                    }
                    string_view key;
                    string keyy;
                    HttpStringUtil::get_value_header(HttpInf.header,key,"Sec-WebSocket-Key");
                    keyy.assign(key);
                    keyy+="258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
                    string result="";
                    CryptoUtil::sha1(string(keyy),result);
                    keyy=EncodingUtil::base64_encode(result);
                    result=HttpStringUtil::createHeader("Upgrade","websocket","Connection","Upgrade","Sec-WebSocket-Accept",keyy);
                    //清理接收缓冲区可能的数据遗漏
                    //Tcpinf.data={};
                    
                    if(!k.sendBack("",result,"101 Switching Protocols"))
                    {
                        //k.close();
                        TcpServer::close(cclientfd.fd);
                        cout<<"握手响应无法发送 wb握手失败"<<endl;
                        if(stt::system::ServerSetting::logfile!=nullptr)
                        {
                            if(stt::system::ServerSetting::language=="Chinese")
                                stt::system::ServerSetting::logfile->writeLog("websocket server consumer "+to_string(threadID)+" : fd= "+to_string(cclientfd.fd)+" 握手响应无法发送 websocket握手失败 服务器已经关闭这个连接");
                            else
                                stt::system::ServerSetting::logfile->writeLog("websocket server consumer "+to_string(threadID)+" : fd= "+to_string(cclientfd.fd)+" couldn't send handshake response .websocket handshake fail. server has closed this connection");
                        }
                        continue;
                        //握手失败
                    }
                    winf.response=::time(0);
                    winf.HBTime=0;
                    wbclientfd.emplace(cclientfd.fd,winf);
                    if(stt::system::ServerSetting::logfile!=nullptr)
                    {
                            if(stt::system::ServerSetting::language=="Chinese")
                                stt::system::ServerSetting::logfile->writeLog("websocket server consumer "+to_string(threadID)+" : fd= "+to_string(cclientfd.fd)+" websocket握手成功");
                            else
                                stt::system::ServerSetting::logfile->writeLog("websocket server consumer "+to_string(threadID)+" : fd= "+to_string(cclientfd.fd)+" websocket has handshaked sucessfully");
                    }
                    thread(fccc,winf,ref(*this)).detach();
                }
                //sleep(10);
                //k.solveRequest(Tcpinf,HttpInf,buffer_size,1);
                //sleep(5);

            }
            else//已经进行了握手操作
            {
                //k.setFD(cclientfd.fd,clientfd[cclientfd.fd].ssl,unblock);
                prepareHandler(k1,cclientfd.fd);
            int r=1;
             int ii=1;
            while(r!=-1&&r!=4)
            {
                if(stt::system::ServerSetting::logfile!=nullptr)
                {
                    if(stt::system::ServerSetting::language=="Chinese")
                        stt::system::ServerSetting::logfile->writeLog("websocket server consumer "+to_string(threadID)+" : fd= "+to_string(cclientfd.fd)+" 正在解析请求... 第"+to_string(ii)+"次处理");
                    else
                        stt::system::ServerSetting::logfile->writeLog("websocket server consumer "+to_string(threadID)+" : fd= "+to_string(cclientfd.fd)+" now solveing request... it is the "+to_string(ii)+" times");
                }
                r=k1.getMessage(Tcpinf,jj->second,buffer_size,ii);

                if(r==-1)
                {
                    if(stt::system::ServerSetting::logfile!=nullptr)
                    {
                        if(stt::system::ServerSetting::language=="Chinese")
                            stt::system::ServerSetting::logfile->writeLog("websocket server consumer "+to_string(threadID)+" : fd= "+to_string(cclientfd.fd)+" 接收错误，关闭连接中 ");
                        else 
                            stt::system::ServerSetting::logfile->writeLog("websocket server consumer "+to_string(threadID)+" : fd= "+to_string(cclientfd.fd)+"error of receiving, and now closing this connection ");
                    }
                    closeWithoutLock(cclientfd.fd);
                }
                else if(r==1)
                {
                    
                    
                   
                    if(jj->second.closeflag==true)//收到关闭确认
                    {
                        if(stt::system::ServerSetting::logfile!=nullptr)
                        {
                        if(stt::system::ServerSetting::language=="Chinese")
                            stt::system::ServerSetting::logfile->writeLog("websocket server consumer "+to_string(threadID)+" : fd= "+to_string(cclientfd.fd)+" 收到关闭确认帧: "+ jj->second.message);
                        else 
                            stt::system::ServerSetting::logfile->writeLog("websocket server consumer "+to_string(threadID)+" : fd= "+to_string(cclientfd.fd)+" has received closed confirm fin:"+ jj->second.message);
                        }
                        TcpServer::close(cclientfd.fd);
                    }
                    else//收到关闭
                    {
                        if(stt::system::ServerSetting::logfile!=nullptr)
                        {
                        if(stt::system::ServerSetting::language=="Chinese")
                            stt::system::ServerSetting::logfile->writeLog("websocket server consumer "+to_string(threadID)+" : fd= "+to_string(cclientfd.fd)+" 收到关闭帧: "+ jj->second.message);
                        else 
                            stt::system::ServerSetting::logfile->writeLog("websocket server consumer "+to_string(threadID)+" : fd= "+to_string(cclientfd.fd)+" has received closed fin:"+ jj->second.message);
                        }
                        cout<<"yes"<<endl;
                        closeAck(cclientfd.fd,jj->second.message);
                    }
                    wbclientfd.erase(jj);
                    break;
                }
                else if(r==2)
                {
                    if(stt::system::ServerSetting::logfile!=nullptr)
                    {
                        if(stt::system::ServerSetting::language=="Chinese")
                            stt::system::ServerSetting::logfile->writeLog("websocket server consumer "+to_string(threadID)+" : fd= "+to_string(cclientfd.fd)+" 收到心跳确认: "+ jj->second.message);
                        else 
                            stt::system::ServerSetting::logfile->writeLog("websocket server consumer "+to_string(threadID)+" : fd= "+to_string(cclientfd.fd)+" has received heartbeat confirm:"+ jj->second.message);
                    }
                    jj->second.response=::time(0);
                    jj->second.HBTime=0;

                    //记录心跳确认

                    jj->second.message="";
                    break;
                }
                else if(r==3)
                {
                    if(stt::system::ServerSetting::logfile!=nullptr)
                    {
                        if(stt::system::ServerSetting::language=="Chinese")
                            stt::system::ServerSetting::logfile->writeLog("websocket server consumer "+to_string(threadID)+" : fd= "+to_string(cclientfd.fd)+" 收到心跳: "+ jj->second.message);
                        else 
                            stt::system::ServerSetting::logfile->writeLog("websocket server consumer "+to_string(threadID)+" : fd= "+to_string(cclientfd.fd)+" has received heartbeat:"+ jj->second.message);
                    }
                    jj->second.response=::time(0);
                    if(!sendMessage(jj->first,"心跳","1010"))//发送心跳失败直接关闭
                    {
                        wbclientfd.erase(jj);
                        TcpServer::close(jj->first);
                        break;
                    }
         
                    //心跳
                    else
                        jj->second.message="";
                    break;
                }
                else if(r==0)//正常报文
                {
                    if(stt::system::ServerSetting::logfile!=nullptr)
                    {
                        if(stt::system::ServerSetting::language=="Chinese")
                            stt::system::ServerSetting::logfile->writeLog("websocket server consumer "+to_string(threadID)+" : fd= "+to_string(cclientfd.fd)+" 收到常规信息: "+ jj->second.message);
                        else 
                            stt::system::ServerSetting::logfile->writeLog("websocket server consumer "+to_string(threadID)+" : fd= "+to_string(cclientfd.fd)+" has received normal message:"+ jj->second.message);
                    }
                    jj->second.response=::time(0);
                    if(!fc(jj->second.message,*this,jj->second))//回调函数失败
                    {
                        close(cclientfd.fd);
                        break;
                    }
                    jj->second.message="";
                    break;
                }
                //else if(r==4)
                //{
                //        solvingFD_lock.lock();
                //        solvingFD[cclientfd]=false;
                //        solvingFD_lock.unlock();
                //        continue;
                //}

                if(r!=4)
                {

                    if(stt::system::ServerSetting::logfile!=nullptr)
                    {
                        if(stt::system::ServerSetting::language=="Chinese")
                            stt::system::ServerSetting::logfile->writeLog("websocket server consumer "+to_string(threadID)+" : fd= "+to_string(cclientfd.fd)+" 处理完成");
                        else 
                            stt::system::ServerSetting::logfile->writeLog("websocket server consumer "+to_string(threadID)+" : fd= "+to_string(cclientfd.fd)+" has solved sucessfully");
                    }
                }
                else
                {
                    if(stt::system::ServerSetting::logfile!=nullptr)
                    {
                        if(stt::system::ServerSetting::language=="Chinese")
                            stt::system::ServerSetting::logfile->writeLog("websocket server consumer "+to_string(threadID)+" : fd= "+to_string(cclientfd.fd)+" 继续等待数据中");
                        else 
                            stt::system::ServerSetting::logfile->writeLog("websocket server consumer "+to_string(threadID)+" : fd= "+to_string(cclientfd.fd)+" waiting data...");
                    }
                }
                ++ii;
            } 
            }
            }
        }
        //跳出循环意味着结束线程
        unique_lock<mutex> lock3(lco1);
        consumerNum--;
        cout<<"consumer "<<consumerNum<<" quit"<<endl;
        lock3.unlock();
        if(stt::system::ServerSetting::logfile!=nullptr)
        {
            if(stt::system::ServerSetting::language=="Chinese")
                stt::system::ServerSetting::logfile->writeLog("websocket server consumer "+to_string(consumerNum)+"退出");
            else
                stt::system::ServerSetting::logfile->writeLog("websocket server consumer "+to_string(consumerNum)+"quit");
        }
    }
    */
    SSL* stt::network::TcpServer::getSSL(const int &fd)
    {
        //SSL *ssl;
        //unique_lock<mutex> lock1(ltl1);
        //auto ii=tlsfd.find(fd);
        //if(ii==tlsfd.end())
        //    ssl=nullptr;
        //else
        //    ssl=ii->second;
        //unique_lock<mutex> lock1(lc1);
        //auto ii=clientfd.find(fd);
        auto connectionIt=clientfd.find(fd);
        if(fd<0||static_cast<unsigned long long>(fd)>=maxFD||connectionIt==clientfd.end()||connectionIt->second.fd==-1)
            return nullptr;
        return connectionIt->second.ssl;
    }
    void stt::network::WebSocketServer::handleHeartbeat()
    {
        
        time_t now;
        
            now=::time(0);
            //unique_lock<mutex> lock(lwb);
            
            for(auto ii=wbclientfd.begin();ii!=wbclientfd.end();)
            {
                if(ii->second.HBTime!=0)//已经发送心跳
                {
                    if(now-ii->second.response>secb)//超时
                    {
                        if(ii->second.closeflag!=true)
                        {
                            const uint16_t code=htons(1000);
                            char ccode[2];
                            memcpy(ccode,&code,2);
                            string codee(ccode,2);
                            codee+="bye";
                            WebSocketServerFDHandler k;
                            prepareHandler(k,ii->first);
                            if(!k.sendMessage(codee,"1000"))//发送失败会自动删除在记录表里的
                            {
                                const int failedFD=ii->first;
                                ++ii;
                                TcpServer::close(failedFD);
                                continue;
                            }
                            ii->second.closeflag=true;
                            {
                                std::lock_guard<std::mutex> lock(websocketRegistryMutex);
                                websocketClosing.insert(ii->first);
                            }
                        }
                    }
                }
                else//检查是否需要发送心跳
                {
                    if(now-ii->second.response>seca)
                    {
                        //cout<<"send"<<endl;
                        if(!sendMessage(ii->first,"心跳","1001"))//发送心跳失败直接关闭
                        {
                            const int failedFD=ii->first;
                            ii=wbclientfd.erase(ii);
                            TcpServer::close(failedFD);
                            continue;
                        }
                        ii->second.HBTime=now;
                    }
                }
                ++ii;
            }
            
            
    }
    bool stt::network::WebSocketServer::close()
    {
        const bool result=TcpServer::close();
        wbclientfd.clear();
        {
            std::lock_guard<std::mutex> lock(websocketRegistryMutex);
            websocketConnections.clear();
            websocketClosing.clear();
        }
        //HBflag1=false;
        //while(HBflag);
        return result;
    }

    void stt::network::WebSocketServer::onConnectionClosed(const int &fd)
    {
        {
            std::lock_guard<std::mutex> lock(websocketRegistryMutex);
            websocketConnections.erase(fd);
            websocketClosing.erase(fd);
        }
        // Physical close always runs on the reactor (or after it has stopped), so
        // the protocol state map remains reactor-owned and needs no cross-thread lock.
        wbclientfd.erase(fd);
    }

    bool stt::network::WebSocketServer::sendMessageForConnection(const int &fd,const uint64_t connection,const string &msg,const string &type)
    {
        WebSocketServerFDHandler handler;
        prepareQueuedHandler(handler,fd,connection);
        return handler.sendMessage(msg,type);
    }

    bool stt::network::WebSocketServer::sendMessage(const int &fd,const string &msg,const string &type)
    {
        uint64_t connection=0;
        {
            std::lock_guard<std::mutex> lock(websocketRegistryMutex);
            const auto connectionIt=websocketConnections.find(fd);
            if(connectionIt==websocketConnections.end()||websocketClosing.find(fd)!=websocketClosing.end())
                return false;
            connection=connectionIt->second;
        }
        return sendMessageForConnection(fd,connection,msg,type);
    }

    void stt::network::WebSocketServer::sendMessage(const string &msg,const string &type)
    {
        std::vector<std::pair<int,uint64_t>> recipients;
        {
            std::lock_guard<std::mutex> lock(websocketRegistryMutex);
            recipients.reserve(websocketConnections.size());
            for(const auto &connection:websocketConnections)
                if(websocketClosing.find(connection.first)==websocketClosing.end())
                    recipients.push_back(connection);
        }
        for(const auto &connection:recipients)
            (void)sendMessageForConnection(connection.first,connection.second,msg,type);
    }
    
bool stt::system::csemp::init(key_t key,unsigned short value,short sem_flg)
{
    if (m_semid!=-1) return false; // 如果已经初始化了，不必再次初始化。

    m_sem_flg=sem_flg;

    // 信号量的初始化不能直接用semget(key,1,0666|IPC_CREAT)
    // 因为信号量创建后，初始值是0，如果用于互斥锁，需要把它的初始值设置为1，
    // 而获取信号量则不需要设置初始值，所以，创建信号量和获取信号量的流程不同。

    // 信号量的初始化分三个步骤：
    // 1）获取信号量，如果成功，函数返回。
    // 2）如果失败，则创建信号量。
    // 3) 设置信号量的初始值。

    // 获取信号量。
    if ( (m_semid=semget(key,1,0666)) == -1)
    {
        // 如果信号量不存在，创建它。
        if (errno==ENOENT)
        {
            // 用IPC_EXCL标志确保只有一个进程创建并初始化信号量，其它进程只能获取。
            if ( (m_semid=semget(key,1,0666|IPC_CREAT|IPC_EXCL)) == -1)
            {
                if (errno==EEXIST) // 如果错误代码是信号量已存在，则再次获取信号量。
                {
                    if ( (m_semid=semget(key,1,0666)) == -1)
                    { 
                        perror("init 1 semget()"); return false; 
                    }
                    return true;
                }
                else  // 如果是其它错误，返回失败。
                {
                    perror("init 2 semget()"); return false;
                }
            }

            // 信号量创建成功后，还需要把它初始化成value。
            union semun sem_union;
            sem_union.val = value;   // 设置信号量的初始值。
            if (semctl(m_semid,0,SETVAL,sem_union) <  0) 
            { 
                perror("init semctl()"); return false; 
            }
        }
        else
        { perror("init 3 semget()"); return false; }
    }

    return true;
}

// 信号量的P操作（把信号量的值减value），如果信号量的值是0，将阻塞等待，直到信号量的值大于0。
bool stt::system::csemp::wait(short value)
{
    if (m_semid==-1) return false;

    struct sembuf sem_b;
    sem_b.sem_num = 0;      // 信号量编号，0代表第一个信号量。
    sem_b.sem_op = value;   // P操作的value必须小于0。
    sem_b.sem_flg = m_sem_flg;
    if (semop(m_semid,&sem_b,1) == -1) { perror("p semop()"); return false; }

    return true;
}

// 信号量的V操作（把信号量的值减value）。
bool stt::system::csemp::post(short value)
{
    if (m_semid==-1) return false;

    struct sembuf sem_b;
    sem_b.sem_num = 0;     // 信号量编号，0代表第一个信号量。
    sem_b.sem_op = value;  // V操作的value必须大于0。
    sem_b.sem_flg = m_sem_flg;
    if (semop(m_semid,&sem_b,1) == -1) { perror("V semop()"); return false; }

    return true;
}

// 获取信号量的值，成功返回信号量的值，失败返回-1。
int stt::system::csemp::getvalue()
{
    return semctl(m_semid,0,GETVAL);
}

// 销毁信号量。
bool stt::system::csemp::destroy()
{
    if (m_semid==-1) return false;

    if (semctl(m_semid,0,IPC_RMID) == -1) { perror("destroy semctl()"); return false; }

    return true;
}

stt::system::csemp::~csemp()
{
}
//string stt::system::ServerSetting::logName;
stt::file::LogFile* stt::system::ServerSetting::logfile=nullptr;
string stt::system::ServerSetting::language="English";
void stt::system::ServerSetting::signalterminated() noexcept
{
    static constexpr char message[]="STTNet: terminate called after an uncaught exception\n";
    (void)::write(STDERR_FILENO,message,sizeof(message)-1);
    std::abort();
}
void stt::system::ServerSetting::setExceptionHandling()
{
    struct sigaction ignoreAction{};
    sigemptyset(&ignoreAction.sa_mask);
    ignoreAction.sa_handler=SIG_IGN;
    sigaction(SIGPIPE,&ignoreAction,nullptr);

    struct sigaction defaultAction{};
    sigemptyset(&defaultAction.sa_mask);
    defaultAction.sa_handler=SIG_DFL;
    for(const int fatalSignal:{SIGSEGV,SIGABRT,SIGBUS,SIGILL,SIGFPE})
        sigaction(fatalSignal,&defaultAction,nullptr);

    std::set_terminate(signalterminated);
}

bool stt::system::ServerSetting::blockTerminationSignals()
{
    sigset_t waitSet;
    sigemptyset(&waitSet);
    sigaddset(&waitSet,SIGTERM);
    sigaddset(&waitSet,SIGINT);
    return pthread_sigmask(SIG_BLOCK,&waitSet,nullptr)==0;
}

int stt::system::ServerSetting::waitForTerminationSignal()
{
    sigset_t waitSet;
    sigemptyset(&waitSet);
    sigaddset(&waitSet,SIGTERM);
    sigaddset(&waitSet,SIGINT);
    int receivedSignal=0;
    return sigwait(&waitSet,&receivedSignal)==0?receivedSignal:-1;
}

void stt::system::ServerSetting::setLogFile(LogFile *logfile,const string &language)
{
    //日志设置
    if(logfile==nullptr)
        return;
    if(language!="")
        stt::system::ServerSetting::language=language;
    stt::system::ServerSetting::logfile=logfile;
     //设置为本对象的logfile
    if(!logfile->isOpen())
    {
        string logName;
        DateTime::getTime(logName);
        logName="./server_log/server_log_"+logName;
        stt::system::ServerSetting::logfile->openFile(logName);
    }
}

void stt::system::ServerSetting::init(LogFile *logfile,const string &language)
{
    setExceptionHandling();//信号设置
    //日志设置
   setLogFile(logfile,language);
    //写日志通知打开完成了
    if(stt::system::ServerSetting::logfile!=nullptr)
    {
        if(stt::system::ServerSetting::language=="Chinese")
            stt::system::ServerSetting::logfile->writeLog("服务器信号，日志等设置完成");
        else
            stt::system::ServerSetting::logfile->writeLog("set server signals and logfile sucessfully");
    }
}
ProcessInf* stt::system::HBSystem::p=nullptr;
stt::system::csemp stt::system::HBSystem::plock;
bool stt::system::HBSystem::isJoin=false;
bool stt::system::HBSystem::join(const char *name,const char *argv0,const char *argv1,const char *argv2)
{
    if(p!=nullptr)
        return false;
    //获取/创建共享内存
    bool first=false;
    int shmid=shmget(0x5095,sizeof(struct ProcessInf)*MAX_PROCESS_INF,0640);
    if(shmid==-1)
    {
        shmid=shmget(0x5095,sizeof(struct ProcessInf)*MAX_PROCESS_INF,0640|IPC_CREAT);
        if(shmid==-1)
            return false;
        first=true;
    }
    p=static_cast<ProcessInf *>(shmat(shmid,0,0));
    if(p==(void*)-1)
        return false;
    //获取锁
    plock.init(0x5095);
    //是否第一次是否需要初始化
    if(first)
    {
        memset(p,-1,sizeof(struct ProcessInf)*MAX_PROCESS_INF);
    }
    //写入信息
    time_t now=::time(nullptr);
    pid_t pid=getpid();
        //遍历找到位置
    first=false;
    //cout<<"join id= "<<pid<<endl;
    if(stt::system::ServerSetting::logfile!=nullptr)
    {
        if(stt::system::ServerSetting::language=="Chinese")
            stt::system::ServerSetting::logfile->writeLog("本进程正在加入心跳系统,id= "+to_string(pid));
        else
            stt::system::ServerSetting::logfile->writeLog("This peocess is joining HBSystem,id= "+to_string(pid));
    }
    plock.wait();
    for(int ii=0;ii<MAX_PROCESS_INF;ii++)
    {
        if(p[ii].pid==pid||p[ii].pid==0||p[ii].pid==-1)
        {
            p[ii].pid=pid;
            p[ii].lastTime=now;
            std::snprintf(p[ii].name,sizeof(p[ii].name),"%s",name==nullptr?"":name);
            std::snprintf(p[ii].argv0,sizeof(p[ii].argv0),"%s",argv0==nullptr?"":argv0);
            std::snprintf(p[ii].argv1,sizeof(p[ii].argv1),"%s",argv1==nullptr?"":argv1);
            std::snprintf(p[ii].argv2,sizeof(p[ii].argv2),"%s",argv2==nullptr?"":argv2);
            first=true;
            break;
        }
    }
    plock.post();
    if(first)
    {
        isJoin=true;
         if(stt::system::ServerSetting::logfile!=nullptr)
         {
            if(stt::system::ServerSetting::language=="Chinese")
                stt::system::ServerSetting::logfile->writeLog("本进程加入心跳系统成功");
            else
                stt::system::ServerSetting::logfile->writeLog("this process has join HBSystem sucessfully");
         }
        return true;
    }
    else
        return false;
}
bool stt::system::HBSystem::renew()
{
    if(p==nullptr)
        return false;
    time_t now=::time(nullptr);
    pid_t pid=getpid();
    //cout<<"renew= "<<pid<<endl;
    plock.wait();
    for(int ii=0;ii<MAX_PROCESS_INF;ii++)
    {
        if(p[ii].pid==pid)
        {
            p[ii].pid=pid;
            p[ii].lastTime=now;
            //cout<<"renwe*** ";
            list();
            plock.post();
            return true;
        }
    }
    plock.post();
    return false;
}
void stt::system::HBSystem::list()
{
    if(p!=nullptr)
    {
        for(int ii=0;ii<MAX_PROCESS_INF;ii++)
        {
            if(p[ii].pid>0)
                cout<<"pid= "<<p[ii].pid<<"name= "<<p[ii].name<<" last time= "<<p[ii].lastTime<<"para="<<p[ii].argv0<<" "<<p[ii].argv1<<" "<<p[ii].argv2<<endl;
        }
    }
}
bool stt::system::HBSystem::HBCheck(const int &sec)
{
    //拿到共享内存
    if(p==nullptr)
    {
        //获取/创建共享内存
        bool first=false;
        int shmid=shmget(0x5095,sizeof(struct ProcessInf)*MAX_PROCESS_INF,0640);
        if(shmid==-1)
        {
            shmid=shmget(0x5095,sizeof(struct ProcessInf)*MAX_PROCESS_INF,0640|IPC_CREAT);
            if(shmid==-1)
                return false;
            first=true;
        }
        p=static_cast<ProcessInf *>(shmat(shmid,0,0));
        if(p==(void*)-1)
            return false;
        //获取锁
        plock.init(0x5095);
        //是否第一次是否需要初始化
        if(first)
        {
            memset(p,-1,sizeof(struct ProcessInf)*MAX_PROCESS_INF);
        }
    }
    list();
    //遍历检查
    for(int ii=0;ii<MAX_PROCESS_INF;ii++)
    {
        time_t now=::time(nullptr);
        if(p[ii].pid==0)
            continue;
        else if(p[ii].pid==-1)
            break;
        //超时
        if((now-p[ii].lastTime)>=sec)
        {
            const pid_t targetPid=p[ii].pid;
            const string processName(p[ii].name,strnlen(p[ii].name,sizeof(p[ii].name)));
            const string argv0(p[ii].argv0,strnlen(p[ii].argv0,sizeof(p[ii].argv0)));
            const string argv1(p[ii].argv1,strnlen(p[ii].argv1,sizeof(p[ii].argv1)));
            const string argv2(p[ii].argv2,strnlen(p[ii].argv2,sizeof(p[ii].argv2)));
            int processFD=-1;
#if defined(__linux__) && defined(SYS_pidfd_open)
            processFD=static_cast<int>(syscall(SYS_pidfd_open,targetPid,0));
#endif
            auto processExists=[targetPid,processFD]() {
                if(processFD>=0)
                {
                    pollfd watchedProcess{processFD,POLLIN,0};
                    return ::poll(&watchedProcess,1,0)==0;
                }
                if(kill(targetPid,0)==0)
                    return true;
                return errno==EPERM;
            };
            auto sendSignal=[targetPid,processFD](const int signalNumber) {
                (void)processFD;
#if defined(__linux__) && defined(SYS_pidfd_send_signal)
                if(processFD>=0)
                    return static_cast<int>(syscall(SYS_pidfd_send_signal,processFD,signalNumber,nullptr,0));
#endif
                return kill(targetPid,signalNumber);
            };

            if(processExists())
                sendSignal(SIGTERM);
            for(int waited=0;waited<8&&processExists();++waited)
                sleep(1);
            if(processExists())
            {
                sendSignal(SIGKILL);
                for(int waited=0;waited<2&&processExists();++waited)
                {
                    int status=0;
                    if(waitpid(targetPid,&status,WNOHANG)==targetPid)
                        break;
                    sleep(1);
                }
            }
            if(processExists())
            {
                if(processFD>=0)
                    ::close(processFD);
                return false;
            }
            if(processFD>=0)
                ::close(processFD);

            //清理信息
            plock.wait();
            if(p[ii].pid==targetPid)
            {
                p[ii].pid=0;
                p[ii].lastTime=0;
            }
            plock.post();
            //重新启动
            Process::startProcess(processName,-1,argv0.c_str(),argv1.c_str(),argv2.c_str());
        }
    }
    return true;
}
bool stt::system::HBSystem::deleteFromHBS()
{
    if(isJoin&&(p!=nullptr))
    {
        //cout<<"清理信息"<<endl;
        pid_t pid=getpid();
        plock.wait();
        for(int ii=0;ii<MAX_PROCESS_INF;ii++)
        {
            if(p[ii].pid==pid)
            {
                p[ii].pid=0;
                p[ii].lastTime=0;
                plock.post();
                list();
                return true;
            }
        }
        plock.post();
        cerr<<"can't find process in hbs"<<endl;
        return false;
    }
    return false;
}
stt::system::HBSystem::~HBSystem()
{
    //cout<<"析构函数运行  ~HBS"<<endl;
    if(p!=nullptr)
    {
        if(!deleteFromHBS()||shmdt(p)==-1)
            cerr<<"close shared memory failed"<<endl;
    }
}
bool stt::security::ConnectionLimiter::allow(RateState &st,const RateLimitType &type,const int &times,const int &secs,const std::chrono::steady_clock::time_point &now)
{
    using namespace std::chrono;

    if (times <= 0 || secs <= 0)
        return true;

    auto window = seconds(secs);

    switch (type)
    {
    case RateLimitType::Cooldown:
        if (st.lastTime.time_since_epoch().count() == 0)
            st.lastTime = now;

        if (now - st.lastTime >= window)
        {
            st.counter = 0;
            st.lastTime = now;
        }

        if (st.counter >= times)
        {
            st.violations++;
            return false;
        }

        st.counter++;
        st.lastTime = now;
        return true;

    case RateLimitType::FixedWindow:
        if (st.lastTime.time_since_epoch().count() == 0)
            st.lastTime = now;

        if (now - st.lastTime >= window)
        {
            st.counter = 0;
            st.lastTime = now;
        }

        if (st.counter >= times)
        {
            st.violations++;
            return false;
        }

        st.counter++;
        return true;

    case RateLimitType::SlidingWindow:
        while (!st.history.empty() &&
               now - st.history.front() >= window)
            st.history.pop_front();

        if ((int)st.history.size() >= times)
        {
            st.violations++;
            return false;
        }

        st.history.push_back(now);
        return true;

    case RateLimitType::TokenBucket:
        if (st.lastRefill.time_since_epoch().count() == 0)
        {
            st.tokens = times;
            st.lastRefill = now;
        }

        double dt =
            std::chrono::duration_cast<std::chrono::duration<double>>(
                now - st.lastRefill).count();

        st.tokens = std::min<double>(
            times,
            st.tokens + dt * (double(times) / secs));

        st.lastRefill = now;

        if (st.tokens < 1.0)
        {
            st.violations++;
            return false;
        }

        st.tokens -= 1.0;
        return true;
    }

    return true;
}
stt::security::DefenseDecision stt::security::ConnectionLimiter::allowConnect(const std::string &ip, const int &fd,const int &times, const int &secs)
{
    auto now = std::chrono::steady_clock::now();

    // ===== 黑名单检查 =====
    auto bit = blacklist.find(ip);
    if (bit != blacklist.end())
    {
        if (now < bit->second)
        {
            logSecurity(
                "【封禁】IP " + ip + " 尝试连接，被拒绝（封禁中）",
                "[BAN] IP " + ip + " connection rejected (banned)"
            );
            return CLOSE;
        }
        blacklist.erase(bit); // 解封
    }

    auto &info = table[ip];

    // 并发连接数
    if (info.activeConnections >= maxConnections)
    {
        info.badScore++;

        logSecurity(
            "【安全】IP " + ip + " 并发连接数超限，已断开",
            "[SECURITY] IP " + ip + " exceeded max connections"
        );

        return CLOSE;
    }

    // 建连速率
    if (!allow(info.connectRate, connectStrategy, times, secs, now))
    {
        info.badScore++;

        logSecurity(
            "【安全】IP " + ip + " 建连过快，已断开",
            "[SECURITY] IP " + ip + " connection rate limited"
        );

        // 升级为黑名单
        if (info.badScore >= 10)
        {
            blacklist[ip] = now + std::chrono::minutes(10);

            logSecurity(
                "【封禁】IP " + ip + " 多次恶意建连，封禁 10 分钟",
                "[BAN] IP " + ip + " banned for 10 minutes"
            );
        }

        return CLOSE;
    }

    // ===== 正式登记 fd =====
    info.activeConnections++;
    info.conns.emplace(fd, ConnectionState{
        fd, RateState{}, {}, now
    });

    return ALLOW;
}


stt::security::DefenseDecision stt::security::ConnectionLimiter::allowRequest(const std::string &ip,const int &fd,const std::string_view &path,const int &times,const int &secs)
{
    auto now = std::chrono::steady_clock::now();

    auto it = table.find(ip);
    if (it == table.end())
        return CLOSE;

    auto &info = it->second;

    // 黑名单检查
    auto bit = blacklist.find(ip);
    if (bit != blacklist.end() && now < bit->second)
        return CLOSE;

    auto itc = info.conns.find(fd);
    if (itc == info.conns.end())
        return CLOSE;

    auto &conn = itc->second;
    conn.lastActivity = now;

    // fd 级限流
    if (!allow(conn.requestRate, requestStrategy, times, secs, now))
    {
        if (conn.requestRate.violations < 3)
        {
            logSecurity(
                "【限流】IP " + ip + " fd=" + std::to_string(fd) +
                " 请求过快，已丢弃",
                "[RATE] IP " + ip + " fd=" + std::to_string(fd) +
                " request dropped"
            );
            return DROP;
        }

        info.badScore++;

        logSecurity(
            "【安全】IP " + ip + " fd=" + std::to_string(fd) +
            " 多次违规，已断开",
            "[SECURITY] IP " + ip + " fd=" + std::to_string(fd) +
            " repeated abuse, closed"
        );

        if (info.badScore >= 15)
        {
            blacklist[ip] = now + std::chrono::minutes(30);

            logSecurity(
                "【封禁】IP " + ip + " 恶意请求，封禁 30 分钟",
                "[BAN] IP " + ip + " banned for 30 minutes"
            );
        }

        return CLOSE;
    }

    // path 级限流
    auto pc = pathConfig.find(std::string(path));
    if (pc != pathConfig.end())
    {
        auto &[ptimes, psecs] = pc->second;
        auto &pst = conn.pathRate[std::string(path)];

        if (!allow(pst, pathStrategy, ptimes, psecs, now))
        {
            logSecurity(
                "【安全】IP " + ip + " fd=" + std::to_string(fd) +
                " 访问路径 " + std::string(path) + " 过于频繁，已断开",
                "[SECURITY] IP " + ip + " fd=" + std::to_string(fd) +
                " path rate limited"
            );

            info.badScore++;
            return CLOSE;
        }
    }

    return ALLOW;
}
void stt::security::ConnectionLimiter::setPathLimit(const std::string &path, const int &times, const int &secs)
{
    pathConfig[path] = {times, secs};
}
bool stt::security::ConnectionLimiter::connectionDetect(const std::string &ip,const int &fd)
{
    if (connectionTimeout < 0)
        return false;

    auto it = table.find(ip);
    if (it == table.end())
        return false;

    auto &info = it->second;
    auto itc = info.conns.find(fd);
    if (itc == info.conns.end())
        return false;

    auto now = std::chrono::steady_clock::now();
    if (now - itc->second.lastActivity >
        std::chrono::seconds(connectionTimeout))
    {
        info.conns.erase(itc);
        if (info.activeConnections > 0)
            info.activeConnections--;
        return true;
    }

    return false;

}
void stt::security::ConnectionLimiter::setConnectStrategy(const RateLimitType &type)
{
    connectStrategy = type;
}

void stt::security::ConnectionLimiter::setRequestStrategy(const RateLimitType &type)
{
    requestStrategy = type;
}

void stt::security::ConnectionLimiter::setPathStrategy(const RateLimitType &type)
{
    pathStrategy = type;
}
void stt::security::ConnectionLimiter::clearIP(const std::string &ip,const int &fd)
{
    auto it = table.find(ip);
    if (it == table.end())
        return;

    auto &info = it->second;
    auto itc = info.conns.find(fd);
    if (itc == info.conns.end())
        return;

    info.conns.erase(itc);
    if (info.activeConnections > 0)
        info.activeConnections--;
}
inline void stt::security::ConnectionLimiter::logSecurity(const std::string &msgCN,const std::string &msgEN)
{
    if (stt::system::ServerSetting::logfile != nullptr)
    {
        if (stt::system::ServerSetting::language == "Chinese")
            stt::system::ServerSetting::logfile->writeLog(msgCN);
        else
            stt::system::ServerSetting::logfile->writeLog(msgEN);
    }
}
void stt::security::ConnectionLimiter::banIP(
    const std::string &ip,
    int banSeconds,
    const std::string &reasonCN,
    const std::string &reasonEN)
{
    auto now = std::chrono::steady_clock::now();

    std::chrono::steady_clock::time_point until;

    if (banSeconds < 0)
    {
        // 永久封禁（不会溢出、不会依赖 int）
        until = std::chrono::steady_clock::time_point::max();
    }
    else if (banSeconds == 0)
    {
        return;
    }
    else
    {
        // 防御性截断，避免 int 极值/误传
        if (banSeconds > 65535)
            banSeconds = 65535;

        until = now + std::chrono::seconds(banSeconds);
    }
    auto it = blacklist.find(ip);
    if (it != blacklist.end())
    {
        // 若已有封禁时间更晚，则不缩短
        if (it->second >= until)
            return;
    }
    blacklist[ip] = until;

    logSecurity(
        "【直接封禁】IP " + ip + "：" + reasonCN +
            (banSeconds < 0
                ? "（永久封禁）"
                : "，封禁 " + std::to_string(banSeconds) + " 秒"),
        "[DIRECT BAN] IP " + ip + ": " + reasonEN +
            (banSeconds < 0
                ? " (permanent)"
                : ", banned for " + std::to_string(banSeconds) + " seconds")
    );
}

void stt::security::ConnectionLimiter::unbanIP(const std::string &ip)
{
    auto it = blacklist.find(ip);
    if (it != blacklist.end())
    {
        blacklist.erase(it);

        logSecurity(
            "【解封】IP " + ip + " 已解除封禁",
            "[UNBAN] IP " + ip + " unbanned"
        );
    }
}
bool stt::security::ConnectionLimiter::isBanned(
    const std::string &ip) const
{
    auto it = blacklist.find(ip);
    if (it == blacklist.end())
        return false;

    return std::chrono::steady_clock::now() < it->second;
}
