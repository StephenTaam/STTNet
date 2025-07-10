#include"../include/sttnet.h"
using namespace std;
using namespace stt::file;
using namespace stt::time;
using namespace stt::data;
using namespace stt::network;
using namespace stt::system;

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
					cout<<k<<endl;
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
                                        cout<<test<<endl;
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
        cerr<<"file has already exist"<<endl;
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
bool stt::file::FileTool::copy(const string &a,const string &b)
{
	ifstream a1;
	ofstream b1;
	a1.open(a,ios::in);
	b1.open(b,ios::out);
//	if(a1.is_open()==false||b1.is_open()==false)
//		return false;
	string kk;
	while(getline(a1,kk))
	      b1<<kk<<endl;
	a1.close();
	b1.close();
	return true;
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
        {
            cerr<<"没有打开文件"<<endl;
            return false;
        }
        //看是否需要unlockmemory
        //如果还是锁住说明是突然关闭，那么理应回退内存，操作失败 
        unique_lock<mutex> testlock(che,try_to_lock);
        if(!testlock.owns_lock())//拿不到锁
        {
            unlockMemory(true);
        }

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
            data.clear();
        else
        {
            if(data_binary!=nullptr)
            {
                delete data_binary;
                data_binary=nullptr;
            }
            if(backUp_binary!=nullptr)
            {
                delete backUp_binary;
                backUp_binary=nullptr;
            }
        }
        //关闭fd
        //::close(fd);
        //fd=-1;
        //完成
        flag=false;
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
            if(data_binary!=nullptr)
                delete data_binary;
            if(backUp_binary!=nullptr)
                delete backUp_binary;
            size=FileTool::get_file_size(fileName);
            fin.open(fileName,ios::binary);
            
            if(size!=0)
            {
                malloced=size*multiple;
            }
            else
            {
                malloced=multiple_backup;
            }
            
            data_binary=new char[malloced];
            backUp_binary=new char[malloced];

            fin.read(data_binary,size);
            memcpy(backUp_binary,data_binary,size);

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
        flag=true;
        return flag;
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
        if(linePos<=this->data.size()&&(linePos+num-1)<=this->data.size())
        {
            int ii=1;
            while(ii<=num)
            {
                data+=this->data[linePos-1+ii-1]+"\n";
                ii++;
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
        if(this->data.size()<linePos)
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
            cout<<"lock找不到文件"<<endl;
        }
        else
            ii->second.lock.lock();
    }
    void stt::file::File::unlockfl2()
    {
        //if(l1use)
        unique_lock<mutex> lock2(l1);
        auto ii=fl2.find(fileName);
        if(ii==fl2.end())
            cout<<"unlock找不到文件"<<endl;
        else
            ii->second.lock.unlock();
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
        che.lock();
        //线程上锁
        lockfl2();
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
        toMemory();
        backUp=data;
        return true;
    }
    bool stt::file::File::unlockMemory(const bool &rec)
    {
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
            unlockfl2();
            if(!binary)
                data=backUp;
            else
            {
                memcpy(data_binary,backUp_binary,size2);
                size1=size2;
            }

            if(rec==false)
            {
                fl1.unlock();
                //flag_lock=false;
                che.unlock();
                return false;
            }
            else
            {
                fl1.unlock();
                //flag_lock=false;
                che.unlock();
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
            unlockfl2();
            fl1.unlock();
            //flag_lock=false;
            che.unlock();
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
        if(this->data.size()<linePos)
            return -1;
        for(int pos=linePos;pos<=this->data.size();pos++)
        {
            if(this->data[pos-1].find(targetString)!=string::npos)
            {
                return pos;
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
            if(linePos>this->data.size())
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
            this->data.pop_back();
        }
        else
        {
            if(linePos>this->data.size())
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
            this->data[this->data.size()-1]=data;
        }
        else
        {
            if(linePos>this->data.size())
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
        if(pos+1<=size1&&pos+size<=size1)
        {
            //读内存
            memcpy(data,data_binary+pos,size);
            return true;
        }
        return false;
    }
    bool stt::file::File::write(const char *data,const size_t &pos,const size_t &size)
    {
        lockMemory();
        return unlockMemory(!writeC(data,pos,size));
    }
    bool stt::file::File::writeC(const char *data,const size_t &pos,const size_t &size)
    {
        //判断是否越界(有没有超出限制)
        if(((pos+1)<=malloced)&&((pos+size)<=malloced))
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
        lockMemory();
        formatC();
        unlockMemory();
    }
    void stt::file::File::formatC()
    {
        //格式化内存
        memset(data_binary,0,size1);
        //改变size1的值
        size1=0;
    }
    
    ostream& stt::time::operator<<(ostream &os,const Duration &a)
    {
        os<<"day="<<a.day<<" "<<a.hour<<":"<<a.min<<":"<<a.sec<<"."<<a.msec;
        return os;
    }
    chrono::system_clock::time_point stt::time::DateTime::strToTimePoint(const string &timeStr,const string &format)
    {
        //获取毫秒保存
        string sss;
        auto pos=format.find(".sss");
        if(pos==string::npos)
            sss="000";
        else
            sss=timeStr.substr(pos+1,3);
        //创建一个新的日期和格式字符串的拷贝并且消掉毫秒部分
        string cformat=format;
        string ctimeStr=timeStr;

        if(pos!=string::npos)
        {
            cformat.replace(pos,4,"");
            ctimeStr.replace(pos,4,"");
        }

        //先把yyyy格式转化为gettime接受的形式
        pos=cformat.find("yyyy");
        if(pos!=string::npos)
        {
            cformat.replace(pos,4,"%Y");
        }
        pos=cformat.find("mm");
        if(pos!=string::npos)
        {
            cformat.replace(pos,2,"%m");
        }
        pos=cformat.find("dd");
        if(pos!=string::npos)
        {
            cformat.replace(pos,2,"%d");
        }
        pos=cformat.find("hh");
        if(pos!=string::npos)
        {
            cformat.replace(pos,2,"%H");
        }
        pos=cformat.find("mi");
        if(pos!=string::npos)
        {
            cformat.replace(pos,2,"%M");
        }
        pos=cformat.find("ss");
        if(pos!=string::npos)
        {
            cformat.replace(pos,2,"%S");
        }
        //开始反向解析日期字符串
            //先用istringstream和gettime反向解析为tm结构体
        tm t;
        istringstream ss(ctimeStr);
        ss>>get_time(&t,cformat.c_str());
        if(ss.fail())
        {
            cerr<<"fail to parse time string to tm struct"<<endl;
        }
        t.tm_isdst = -1; //tm结构体要补完 防止没有初始化导致有时有可能行为不确定
        //cout<<t.tm_year<<endl;
        //cout<<t.tm_mon<<endl;
        //cout<<t.tm_mday<<endl;
        //cout<<t.tm_hour<<endl;
        //cout<<t.tm_min<<endl;
        //cout<<t.tm_sec<<endl;
            //tm结构体转化为timet
        //setenv("TZ","UTC0",1);
        //tzset();//设置时区为utc并且禁用夏令时，调用tzset更新环境变量
        time_t tt=mktime(&t);
        //cout<<tt<<endl;
            //timet转换为chrono::system_clock::time_point
        chrono::system_clock::time_point tp=chrono::system_clock::from_time_t(tt);
        
        //cout<<tp.time_since_epoch().count()<<endl;
        
            //检查是否存在毫秒
        if(sss!="000")
        {
            int msec=stoi(sss);
            tp+=Milliseconds(msec);
        }
        //else//为了转化为毫秒精度的chrono::system_clock::time_point,没有就设置为0
        //{
        ///    tp+=Milliseconds(0);
        //}
        return tp;
    }
    string& stt::time::DateTime::timePointToStr(const chrono::system_clock::time_point &tp,string &timeStr,const string &format)
    {
        string cformat=format;
        //先把yyyy格式转化为puttime接受的形式
        auto pos=cformat.find("yyyy");
        if(pos!=string::npos)
        {
            cformat.replace(pos,4,"%Y");
        }
        pos=cformat.find("mm");
        if(pos!=string::npos)
        {
            cformat.replace(pos,2,"%m");
        }
        pos=cformat.find("dd");
        if(pos!=string::npos)
        {
            cformat.replace(pos,2,"%d");
        }
        pos=cformat.find("hh");
        if(pos!=string::npos)
        {
            cformat.replace(pos,2,"%H");
        }
        pos=cformat.find("mi");
        if(pos!=string::npos)
        {
            cformat.replace(pos,2,"%M");
        }
        pos=cformat.find("ss");
        if(pos!=string::npos)
        {
            cformat.replace(pos,2,"%S");
        }

        
        time_t t_tp;
        t_tp=chrono::system_clock::to_time_t(tp);
        //to_time_t得到的时间可能处理不了久远的、时间 自己写实现办法 转化为timet
        //string a="1970-01-01T00:00:00";
        //chrono::system_clock::time_point b=DateTime::strToTimePoint(a);
        
       //cout<<b.time_since_epoch().count()<<endl;
       //cout<<tp.time_since_epoch().count()<<endl;

          //  s=chrono::duration_cast<chrono::duration<uint64_t>>(b-tp);
          //  t_tp=0-s.count();

        //

        tm tm_tp;
        localtime_r(&t_tp,&tm_tp);
        ostringstream oss;
        oss<<put_time(&tm_tp,cformat.c_str());
        timeStr=oss.str();
        //检查是否有毫秒
        pos=timeStr.find("sss");
        if(pos!=string::npos)
        {
            Milliseconds milliseconds=chrono::duration_cast<Milliseconds>(tp.time_since_epoch())%1000;
            oss.str("");
            oss<<setfill('0')<<setw(3)<<milliseconds.count();
            timeStr.replace(pos,3,oss.str());
        }
        return timeStr;
    }
    string& stt::time::DateTime::getTime(string &timeStr,const string &format)
    {
        chrono::time_point<chrono::system_clock> now=chrono::system_clock::now();
        timePointToStr(now,timeStr,format);
        return timeStr;
    }
    Duration& stt::time::DateTime::dTOD(const Milliseconds& d1,Duration &D1)
    {
        uint64_t msec=d1.count();
        D1.day=msec/(24*60*60*1000);
        msec=msec%(24*60*60*1000);
        D1.hour=msec/(60*60*1000);
        msec=msec%(60*60*1000);
        D1.min=msec/(60*1000);
        msec=msec%(60*1000);
        D1.sec=msec/(1000);
        msec=msec%(1000);
        D1.msec=msec;
        return D1;
    }
    Milliseconds& stt::time::DateTime::DTOd(const Duration &D1,Milliseconds& d1)
    {
        uint64_t msec=0;
        msec+=static_cast<uint64_t>(D1.day)*86400000;
        msec+=static_cast<uint64_t>(D1.hour)*60*60*1000;
        msec+=static_cast<uint64_t>(D1.min)*60*1000;
        msec+=static_cast<uint64_t>(D1.sec)*1000;
        msec+=D1.msec;
        d1=Milliseconds(msec);
        return d1;
    }
    bool stt::time::DateTime::convertFormat(string &timeStr,const string &oldFormat,const string &newFormat)
    {
        string str=newFormat;
        auto pos1=oldFormat.find("yyyy");
        auto pos2=str.find("yyyy");
        if(pos1!=string::npos)//新字符串比起旧的缺失了不要紧 如果新的比起旧的新加了 那就是缺少数据 会导致字符串数据不完整 直接报错就行了
        {
            if(pos2!=string::npos)
                str.replace(pos2,4,timeStr.substr(pos1,4));
        }
        else
            return false;
        pos1=oldFormat.find("mm");
        pos2=str.find("mm");
        if(pos1!=string::npos)
        {
            if(pos2!=string::npos)
                str.replace(pos2,2,timeStr.substr(pos1,2));
        }
        else
            return false;
        pos1=oldFormat.find("dd");
        pos2=str.find("dd");
        if(pos1!=string::npos)
        {
            if(pos2!=string::npos)
                str.replace(pos2,2,timeStr.substr(pos1,2));
        }
        else
            return false;
        pos1=oldFormat.find("hh");
        pos2=str.find("hh");
        if(pos1!=string::npos)
        {
            if(pos2!=string::npos)
                str.replace(pos2,2,timeStr.substr(pos1,2));
        }
        else
            return false;
        pos1=oldFormat.find("mi");
        pos2=str.find("mi");
        if(pos1!=string::npos)
        {
            if(pos2!=string::npos)
                str.replace(pos2,2,timeStr.substr(pos1,2));
        }
        else
            return false;
        pos1=oldFormat.find("ss");
        pos2=str.find("ss");
        if(pos1!=string::npos)
        {
            if(pos2!=string::npos)
                str.replace(pos2,2,timeStr.substr(pos1,2));
        }
        else
            return false;
        pos1=oldFormat.find("sss");
        pos2=str.find("sss");
        if(pos1!=string::npos)
        {
            if(pos2!=string::npos)
                str.replace(pos2,3,timeStr.substr(pos1,3));
        }
        else
            return false;
        timeStr=str;
        return true;
    }
    Duration& stt::time::DateTime::calculateTime(const string &time1,const string &time2,Duration &result,const string &format1,const string &format2)
    {
        Milliseconds dt;
        chrono::system_clock::time_point t1=strToTimePoint(time1,format1);
        chrono::system_clock::time_point t2=strToTimePoint(time2,format2);
        dt=chrono::duration_cast<Milliseconds>(t1-t2);
        dTOD(dt,result);
        return result;
    }
    string& stt::time::DateTime::calculateTime(const string &time1,const Duration &time2,string &result,const string &am,const string &format1,const string &format2)
    {
        chrono::system_clock::time_point t1=strToTimePoint(time1,format1);
        Milliseconds t2;
        DTOd(time2,t2);
        chrono::system_clock::time_point t3;
        if(am=="+")
            t3=t1+t2;
        else
            t3=t1-t2;
        time_t t_tp=chrono::system_clock::to_time_t(t3);
        timePointToStr(t3,result,format2);
        return result;
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
            dt.day=-1;
            dt.hour=-1;
            dt.min=-1;
            dt.sec=-1;
            dt.msec=-1;
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
            dt.day=-1;
            dt.hour=-1;
            dt.min=-1;
            dt.sec=-1;
            dt.msec=-1;
            return dt;
        }
        end=chrono::steady_clock::now();
        dTOD(chrono::duration_cast<Milliseconds>(end-start),dt);
        return dt;
    }
    bool stt::time::DateTime::compareTime(const string &time1,const string &time2,const string &format1,const string &format2)
    {
        auto t1=strToTimePoint(time1,format1);
        auto t2=strToTimePoint(time2,format2);
        if(t1.time_since_epoch()>=t2.time_since_epoch())
            return true;
        else
            return false;
    }
    
    bool stt::file::LogFile::openFile(const string &fileName,const string &timeFormat,const string &contentFormat)
    {
        this->timeFormat=timeFormat;
        this->contentFormat=contentFormat;
        return File::openFile(fileName,true,0,0,0664);
    }
    bool stt::file::LogFile::writeLog(const string &data)
    {
        string content;
        getTime(content,timeFormat);
        content+=contentFormat+data;
        return appendLine(content);
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
        if(!isOpen())
            return false;
        int linePos=1;
        string data;
        string time;
        int timeSize=timeFormat.size();
        if(!lockMemory())
            return false;
        while(readLineC(data,linePos))
        {
            time=data.substr(0,timeSize);
            if((date1=="1"||compareTime(time,date1,timeFormat,timeFormat)==true)&&(date2=="2"||compareTime(time,date2,timeFormat,timeFormat)==false))
            {
                deleteLineC(linePos);
                unlockMemory(false);
                linePos--;
            }
            linePos++;
        }
        //if(chee.owns_lock())
        //    unlockMemory(true);
        unique_lock<mutex> testlock(che,try_to_lock);
        if(!testlock.owns_lock())
            unlockMemory(true);
        
        return true;
    }
    
    bool stt::data::CryptoUtil::encryptSymmetric(const unsigned char *before,const size_t &length,const unsigned char *passwd,const unsigned char *iv,unsigned char *after)
    {
        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        if (!ctx) return false;
        int len;
        int ciphertext_len;
        // 初始化加密操作
        if (1 != EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, passwd, iv)) 
        {
            EVP_CIPHER_CTX_free(ctx);
            return false;
        }
        // 加密数据
        if (1 != EVP_EncryptUpdate(ctx, after, &len, before, length))
        {
            EVP_CIPHER_CTX_free(ctx);
            return false;
        }
        ciphertext_len = len;
        // 加密完成
        if (1 != EVP_EncryptFinal_ex(ctx, after + len, &len)) 
        {
            EVP_CIPHER_CTX_free(ctx);
            return false;
        }
        ciphertext_len += len;
        EVP_CIPHER_CTX_free(ctx);
        return ciphertext_len;
    }
    bool stt::data::CryptoUtil::decryptSymmetric(const unsigned char *before,const size_t &length,const unsigned char *passwd,const unsigned char *iv,unsigned char *after)
    {
        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        if (!ctx) return false;

        int len;
        int plaintext_len;

        // 初始化解密操作
        if (1 != EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, passwd, iv)) 
        {
            EVP_CIPHER_CTX_free(ctx);
            return false;
        }

        // 解密数据
        if (1 != EVP_DecryptUpdate(ctx, after, &len, before, length)) 
        {
            EVP_CIPHER_CTX_free(ctx);
            return false;
        }
        plaintext_len = len;

        // 解密完成
        if (1 != EVP_DecryptFinal_ex(ctx, after + len, &len)) 
        {
            EVP_CIPHER_CTX_free(ctx);
            return false;
        }
        plaintext_len += len;

        EVP_CIPHER_CTX_free(ctx);
        return plaintext_len;
    }
string& stt::data::CryptoUtil::sha1(const string &ori_str,string &result)
{
	unsigned char hash[SHA_DIGEST_LENGTH];
	SHA1((unsigned char*)ori_str.c_str(),ori_str.length(),hash);
    char hash1[SHA_DIGEST_LENGTH];
    for(int i=0;i<SHA_DIGEST_LENGTH;i++)
        hash1[i]=static_cast<char>(hash[i]);
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
    for(int i=1;i<=8;i++)
    {
        if((input&'z'+6)=='z'+6)
            result+='1';
        else
            result+='0';
        input=input<<1;
    }
    return result;
}
string& stt::data::BitUtil::bitOutput(const string &input,string &result)
{
    result.clear();
    for(char cc:input)
    {
        for(int i=1;i<=8;i++)
        {
            if((cc&'z'+6)=='z'+6)
                result+='1';
            else
                result+='0';
            cc=cc<<1;
        }
    }
    return result;
}
char& stt::data::BitUtil::bitOutput_bit(char input,const int pos,char &result)
{
    input=input<<(pos-1);
    if((input&'z'+6)=='z'+6)
        result='1';
    else
        result='0';
    return result;
}
unsigned long& stt::data::BitUtil::bitStrToNumber(const string &input,unsigned long &result)
{
    result=0;
    int mul=input.length()-1;
    for(const char &cc:input)
    {
        if(cc=='1')
            result+=pow(2,mul);
        mul--;
    }
    return result;
}
unsigned long& stt::data::BitUtil::bitToNumber(const string &input,unsigned long &result)
{
    string rr;
    bitOutput(input,rr);
    result=bitStrToNumber(rr,result);
    return result;
}
char& stt::data::BitUtil::toBit(const string &input,char &result)
{
    int pos=0;
    result='z'-122;
    unsigned char cc;
    for(const char &ii:input)
    {
        if(ii=='1')
        {
            cc='z'+6;
            cc=cc>>(pos%8);
            result=result|cc;
        }
        pos++;
    }
    return result;
}
string& stt::data::BitUtil::toBit(const string &input,string &result)
{
    result.clear();
    int pos=0;
    char bb='z'-122;
    unsigned char cc;
    for(const char &ii:input)
    {
        if(ii=='1')
        {
            cc='z'+6;
            cc=cc>>pos;
            bb=bb|cc;
        }
        pos++;
        if(pos==8)
        {
            pos=0;
            result+=bb;
            bb='z'-122;
        }
    }
    return result;
}
    long stt::data::RandomUtil::getRandomNumber(const long &a,const long &b)
    {
        random_device rd;
        mt19937_64 generator(rd());
        uniform_int_distribution<long long> dist(a,b);
        return dist(generator);
    }
    string& stt::data::RandomUtil::getRandomStr_base64(string &str,const int &length)
    {
        str.clear();
		// 定义包含可能字符的字符串
    	const string characters = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz+/";

    	// 设置随机数引擎和分布
    	random_device rd;
    	mt19937 generator(rd());
    	uniform_int_distribution<int> distribution(0, characters.size() - 1);

    	// 生成随机字符串
    	
    	for (int i = 1; i <=length-length%4; i++) //长度为randomNumber
		{
        	str += characters[distribution(generator)];
    	}
        for(int i=1;i<=length%4;i++)
        {
            str+='=';
        }
        return str;
    }
string& stt::data::RandomUtil::generateMask_4(string &mask)
{
    mask.clear();
		// 定义包含可能字符的字符串
    	const string characters = "01";
        string bitStr;
    	// 设置随机数引擎和分布
    	random_device rd;
    	mt19937 generator(rd());
    	uniform_int_distribution<int> distribution(0, characters.size() - 1);

    	// 生成随机字符串
    	
    	for (int i = 1; i <=32; i++) //长度为randomNumber
		{
        	bitStr += characters[distribution(generator)];
    	}
        BitUtil::toBit(bitStr,mask);
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
    size_t stt::data::HttpStringUtil::get_split_str(const string& ori_str,string &str,const string &a,const string &b,const size_t &pos)
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
        size_t ii=ori_str.find(name);
	    if(ii==string::npos)
	    {
		    str="";
		    return str;
	    }
		    get_split_str(ori_str,str,name+": ","\r\n");
		    //del_space_str(str);
		    return str;
    }
    string& stt::data::HttpStringUtil::get_value_str(const string& ori_str,string &str,const string& name)
{
    size_t ii=ori_str.find("?");
    if(ii==string::npos)
        ii=0;
	ii=ori_str.find(name+"=",ii);
	if(ii==string::npos)
	{
		str="";
		return str;
	}
	//分两种情况，可能是最后一个，或者不是最后一个。&分隔开很关键
	if(ori_str.find("&",ii)==string::npos)//最后一个
	{
		ii=ori_str.find("=",ii);
		str=ori_str.substr(ii+1,ori_str.length()-ii-1);
		//del_space_str(str);//去掉空格
		return str;
	}
	else
	{
		get_split_str(ori_str,str,name+"=","&");
		//del_space_str(str);
		return str;
	}
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
    int& stt::data::NumberStringConvertUtil::toInt(const string&ori_str,int &result,const int &i)
    {
        try
        {
            result=stoi(ori_str);
        }
        catch(...)
        {
            result=i;
            return result;
        }
        return result;
    }
	long& stt::data::NumberStringConvertUtil::toLong(const string&ori_str,long &result,const long &i)
    {
        try
        {
            result=stol(ori_str);
        }
        catch(...)
        {
            result=i;
            return result;
        }
        return result;
    }
	float& stt::data::NumberStringConvertUtil::toFloat(const string&ori_str,float &result,const float &i)
    {
        try
        {
            result=stof(ori_str);
        }
        catch(...)
        {
            result=i;
            return result;
        }
        return result;
    }
	double& stt::data::NumberStringConvertUtil::toDouble(const string&ori_str,double &result,const double &i)
    {
        try
        {
            result=stod(ori_str);
        }
        catch(...)
        {
            result=i;
            return result;
        }
        return result;
    }
    bool& stt::data::NumberStringConvertUtil::toBool(const string&ori_str,bool &result)
    {
        if(ori_str=="true"||ori_str=="TRUE"||ori_str=="True")
        {
            result=true;
            return result;
        }
        else
        {
            result=false;
            return result;
        }
    }
    
    
    
    
    
string& stt::data::NumberStringConvertUtil::strto16(const string &ori_str,string &result)//字符串转化为16进制字符串
{
    int size=ori_str.length();
    const char *buffer1;
    char buffer2[2*size+1];
    buffer2[2*size]=0;
    buffer1=ori_str.c_str();
    for(int i=0;i<size;i++)
        snprintf(buffer2+i*2,3,"%02x",buffer1[i]);
    result.assign(buffer2);
    return result;
}
    int& stt::data::NumberStringConvertUtil::str16toInt(const string&ori_str,int &result,const int &i)
    {
        int bit=ori_str.length()-1;
        result=0;
        for(auto &ii:ori_str)
        {
            if(ii=='0')
                result+=0*pow(16,bit);
            else if(ii=='1')
                result+=1*pow(16,bit);
            else if(ii=='2')
                result+=2*pow(16,bit);
            else if(ii=='3')
                result+=3*pow(16,bit);
            else if(ii=='4')
                result+=4*pow(16,bit);
            else if(ii=='5')
                result+=5*pow(16,bit);
            else if(ii=='6')
                result+=6*pow(16,bit);
            else if(ii=='7')
                result+=7*pow(16,bit);
            else if(ii=='8')
                result+=8*pow(16,bit);
            else if(ii=='9')
                result+=9*pow(16,bit);
            else if(ii=='a'||ii=='A')
                result+=10*pow(16,bit);
            else if(ii=='b'||ii=='B')
                result+=11*pow(16,bit);
            else if(ii=='c'||ii=='C')
                result+=12*pow(16,bit);
            else if(ii=='d'||ii=='D')
                result+=13*pow(16,bit);
            else if(ii=='e'||ii=='E')
                result+=14*pow(16,bit);
            else if(ii=='f'||ii=='F')
                result+=15*pow(16,bit);
            else
            {
                result=i;
                break;
            }
            bit--;
        }
        return result;
    }
    // Function to encode a string using Base64
std::string stt::data::EncodingUtil::base64_encode(const std::string &input) {
    BIO *bio, *b64;
    BUF_MEM *bufferPtr;

    b64 = BIO_new(BIO_f_base64());
    bio = BIO_new(BIO_s_mem());
    bio = BIO_push(b64, bio);

    BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL); // Ignore newlines
    BIO_write(bio, input.c_str(), input.length());
    BIO_flush(bio);
    BIO_get_mem_ptr(bio, &bufferPtr);

    std::string encoded_data(bufferPtr->data, bufferPtr->length);
    BIO_free_all(bio);
    return encoded_data;
}

// Function to decode a Base64 encoded string
std::string stt::data::EncodingUtil::base64_decode(const std::string &input) {
    BIO *bio, *b64;
    char *buffer = (char *)malloc(input.length());
    memset(buffer, 0, input.length());

    b64 = BIO_new(BIO_f_base64());
    bio = BIO_new_mem_buf(input.c_str(), input.length());
    bio = BIO_push(b64, bio);

    BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL); // Ignore newlines
    int decoded_length = BIO_read(bio, buffer, input.length());
    std::string decoded_data(buffer, decoded_length);
    BIO_free_all(bio);
    free(buffer);

    return decoded_data;
}



string& stt::data::EncodingUtil::maskCalculate(string &data,const string &mask)
{
    const char *m=mask.data();
    int ii=0;
    for(char &cc:data)
    {
        cc=cc^m[ii%4];
        ii++;
    }
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
    mask.clear();
		// 定义包含可能字符的字符串
    	const string characters = "01";
        string bitStr;
    	// 设置随机数引擎和分布
    	random_device rd;
    	mt19937 generator(rd());
    	uniform_int_distribution<int> distribution(0, characters.size() - 1);

    	// 生成随机字符串
    	
    	for (int i = 1; i <=32; i++) //长度为randomNumber
		{
        	bitStr += characters[distribution(generator)];
    	}
        BitUtil::toBit(bitStr,mask);
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
        return val.asString();
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
        string result=a;
        result.erase(a.length()-2);
        if(a.length()>3)
            result+=",";
        result+=b.substr(1);
        return result;
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
        fcntl(fd,F_GETFL,flags&~O_NONBLOCK);
        flag1=false;
        if(sec!=-1)
        {
            struct timeval tv;
            tv.tv_sec = sec;  // 设置接收超时时间为 5 秒
            tv.tv_usec = 0;

            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        }
        this->sec=sec;
    }
    void stt::network::UdpFDHandler::blockSet(const int &sec)
    {
        int flags=fcntl(fd,F_GETFL,0);//获取当前标志
        fcntl(fd,F_GETFL,flags&~O_NONBLOCK);
        flag1=false;
        if(sec!=-1)
        {
            struct timeval tv;
            tv.tv_sec = sec;  // 设置接收超时时间为 5 秒
            tv.tv_usec = 0;

            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        }
        this->sec=sec;
    }
    void stt::network::TcpFDHandler::unblockSet()
    {
        int flags=fcntl(fd,F_GETFL,0);//获取当前标志
        fcntl(fd,F_SETFL,flags|O_NONBLOCK);
        flag1=true;
    }
    void stt::network::UdpFDHandler::unblockSet()
    {
        int flags=fcntl(fd,F_GETFL,0);//获取当前标志
        fcntl(fd,F_SETFL,flags|O_NONBLOCK);
        flag1=true;
    }
    bool stt::network::TcpFDHandler::multiUseSet()
    {
        int opt;
        if(!flag2)//设置为multiuse
            opt=1;
        else
            opt=0;
        if(setsockopt(fd,SOL_SOCKET,SO_REUSEADDR|SO_REUSEPORT,&opt,sizeof(opt))<0)
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
        if(setsockopt(fd,SOL_SOCKET,SO_REUSEADDR|SO_REUSEPORT,&opt,sizeof(opt))<0)
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
        this->fd=fd;
        this->flag1=flag1;
        this->flag2=flag2;
        this->ssl=ssl;
        if(ssl!=nullptr)
            signal(SIGPIPE,SIG_IGN);
        if(flag1==true)
            unblockSet();
        else 
            blockSet(sec);
        if(flag2==true)
            multiUseSet();
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
        if(cle)
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
        fd=socket(AF_INET,SOCK_STREAM,0);
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
            if(!initCTX(ca,cert,passwd))
            {
                this->TLS=false;
                return false;
            }
            ssl=SSL_new(ctx);
            if(ssl==NULL)
            {
                perror("new ssl wrong");
                ERR_print_errors_fp(stderr);
                this->TLS=false;
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
        ::close(fd);
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
        ERR_load_BIO_strings();
        //SSL_library_init();

        // 我们使用SSL V3,V2
        if((ctx = SSL_CTX_new(TLS_client_method())) == NULL)
            return false;
        //校验对方证书，使用系统自带的权威的ca证书
        SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);
        if(SSL_CTX_load_verify_locations(ctx, ca, NULL)<=0)
        {
            cerr<<"ca false"<<endl;
            return false;
        }
        //可能的话加载自己的证书和私钥
        if(cert!=""&&passwd!="")
        {
            if(SSL_CTX_use_certificate_chain_file(ctx, cert) <=0)
            {
                cerr<<"加载证书失败"<<endl;
                return false;
            }
            SSL_CTX_set_default_passwd_cb_userdata(ctx, (void*)passwd);
            if(SSL_CTX_use_PrivateKey_file(ctx, key, SSL_FILETYPE_PEM) <=0)
            {
                cerr<<"密码失败"<<endl;
                return false;
            }
            //判断私钥是否正确
            if(!SSL_CTX_check_private_key(ctx))
            {
                cerr<<"密码错误"<<endl;
                return false;
            }
        }
        signal(SIGPIPE,SIG_IGN);
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
        //connect
        hostent *k1;
        k1=gethostbyname(ip.c_str());//DNS解析
        if(k1==nullptr)
        {
            cerr<<"get host by name failed"<<endl;
            close();
            return false;
        }
        struct sockaddr_in k2;
        memset(&k2,0,sizeof(k2));
        k2.sin_family=AF_INET;
        k2.sin_port=htons(port);
        memcpy(&k2.sin_addr,k1->h_addr,k1->h_length);
        if(::connect(fd,(struct sockaddr*)&k2,sizeof(k2))!=0)
        {
            perror("connect");
            close();
            return false;
        }
        if(TLS)//套上TLS加密
        {
            SSL_set_connect_state(ssl);
            SSL_set_fd(ssl,fd);
            int ret=SSL_connect(ssl);
            if(ret != 1)
            {
                printf("%s\n", SSL_state_string_long(ssl));
                printf("ret = %d, ssl get error %d\n", ret, SSL_get_error(ssl, ret));
                return false;
            }
        }
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
        if(!isConnect())
            return -99;
        int size=data.size();
        int totalSize=0;
        int pos=0;
        int result;
        if(block)
        {
            if(ssl==nullptr)
            {
                while((result=::send(fd,data.substr(pos).c_str(),size-totalSize,MSG_NOSIGNAL)))
                {
                    if(result==0)
                    {
                        flag3=0;
                        break;
                    }
                    else if(result<0)
                    {
                        if(errno==EAGAIN||errno==EWOULDBLOCK)
                            continue;
                        perror("send");
                        break;
                    }
                    else
                    {
                        totalSize+=result;
                        pos=totalSize-1;
                        if(totalSize>=size)
                            break;
                    }
                }
            }
            else
            {
                while((result=SSL_write(ssl,data.substr(pos).c_str(),size-totalSize)))
                {
                    if(result==0)
                    {
                        flag3=0;
                        break;
                    }
                    else if(result<0)
                    {
                        if(errno==EAGAIN||errno==EWOULDBLOCK)
                            continue;
                        perror("send");
                        break;
                    }
                    else
                    {
                        totalSize+=result;
                        pos=totalSize-1;
                        if(totalSize>=size)
                            break;
                    }
                }
            }
        }
        else
        {
            if(ssl==nullptr)
            {
                totalSize=::send(fd,data.c_str(),data.size(),MSG_NOSIGNAL);
                if(totalSize<0)
                {
                    if(errno==EAGAIN||errno==EWOULDBLOCK)
                        totalSize=-100;
                }
            }
            else
            {
                totalSize=SSL_write(ssl,data.c_str(),data.size());
                if(totalSize<0)
                {
                    if(errno==EAGAIN||errno==EWOULDBLOCK)
                        totalSize=-100;
                }
            }
        }
        return totalSize;
    }
    
    int stt::network::UdpFDHandler::sendData(const string &data,const string &ip,const int &port,const bool &block)
    {
        if(fd==-1)
            return -99;
        //填充目标信息
        hostent *k1;
        k1=gethostbyname(ip.c_str());//DNS解析
        if(k1==nullptr)
        {
            cerr<<"get host by name failed"<<endl;
            return -98;
        }
        struct sockaddr_in k2;
        memset(&k2,0,sizeof(k2));
        k2.sin_family=AF_INET;
        k2.sin_port=htons(port);
        memcpy(&k2.sin_addr,k1->h_addr,k1->h_length);
        //发送
        int size=data.size();
        int totalSize=0;
        int pos=0;
        int result;
        if(block)
        {
            while((result=::sendto(fd,data.substr(pos).c_str(),size-totalSize,MSG_NOSIGNAL,(struct sockaddr*)&k2,sizeof(k2))))
            {
                if(result<0)
                {
                    if(errno==EAGAIN||errno==EWOULDBLOCK)
                        continue;
                    perror("send");
                    break;
                }
                else
                {
                    totalSize+=result;
                    pos=totalSize-1;
                    if(totalSize>=size)
                        break;
                }
            }
        }
        else
        {
            totalSize=::sendto(fd,data.c_str(),data.size(),MSG_NOSIGNAL,(struct sockaddr*)&k2,sizeof(k2));
            if(totalSize<0)
            {
                if(errno==EAGAIN||errno==EWOULDBLOCK)
                    totalSize=-100;
            }
        }
        return totalSize;
    }
    
    int stt::network::TcpFDHandler::sendData(const char *data,const uint64_t &length,const bool &block)
    {
        if(!isConnect())
            return -99;
        int totalSize=0;
        int pos=0;
        int result;
        if(block)
        {
            if(ssl==nullptr)
            {
                while((result=::send(fd,data+pos,length-totalSize,MSG_NOSIGNAL)))
                {
                    if(result==0)
                    {
                        flag3=0;
                        break;
                    }
                    else if(result<0)
                    {
                        if(errno==EAGAIN||errno==EWOULDBLOCK)
                            continue;
                        perror("send");
                        break;
                    }
                    else
                    {
                        totalSize+=result;
                        pos=totalSize-1;
                        if(totalSize>=length)
                            break;
                    }
                }
            }
            else
            {
                while((result=::SSL_write(ssl,data+pos,length-totalSize)))
                {
                    if(result==0)
                    {
                        flag3=0;
                        break;
                    }
                    else if(result<0)
                    {
                        if(errno==EAGAIN||errno==EWOULDBLOCK)
                            continue;
                        perror("send");
                        break;
                    }
                    else
                    {
                        totalSize+=result;
                        pos=totalSize-1;
                        if(totalSize>=length)
                            break;
                    }
                }
            }
        }
        else
        {
            if(ssl==nullptr)
            {
                totalSize=::send(fd,data,length,MSG_NOSIGNAL);
                if(totalSize<0)
                {
                    if(errno==EAGAIN||errno==EWOULDBLOCK)
                        totalSize=-100;
                }
            }
            else
            {
                totalSize=SSL_write(ssl,data,length);
                if(totalSize<0)
                {
                    if(errno==EAGAIN||errno==EWOULDBLOCK)
                        totalSize=-100;
                }
            }
        }
        return totalSize;
    }
    
    int stt::network::UdpFDHandler::sendData(const char *data,const uint64_t &length,const string &ip,const int &port,const bool &block)
    {
        if(fd==-1)
            return -99;
        //填充目标信息
        hostent *k1;
        k1=gethostbyname(ip.c_str());//DNS解析
        if(k1==nullptr)
        {
            cerr<<"get host by name failed"<<endl;
            return -98;
        }
        struct sockaddr_in k2;
        memset(&k2,0,sizeof(k2));
        k2.sin_family=AF_INET;
        k2.sin_port=htons(port);
        memcpy(&k2.sin_addr,k1->h_addr,k1->h_length);
        //发送
        int totalSize=0;
        int pos=0;
        int result;
        if(block)
        {
            while((result=::sendto(fd,data+pos,length-totalSize,MSG_NOSIGNAL,(struct sockaddr*)&k2,sizeof(k2))))
            {
                if(result<0)
                {
                    if(errno==EAGAIN||errno==EWOULDBLOCK)
                        continue;
                    perror("send");
                    break;
                }
                else
                {
                    totalSize+=result;
                    pos=totalSize-1;
                    if(totalSize>=length)
                        break;
                }
            }
        }
        else
        {
            totalSize=::sendto(fd,data,length,MSG_NOSIGNAL,(struct sockaddr*)&k2,sizeof(k2));
            if(totalSize<0)
            {
                if(errno==EAGAIN||errno==EWOULDBLOCK)
                    totalSize=-100;
            }
        }
        return totalSize;
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
        else
        {
            if(flag1==true&&size<0&&(errno==EAGAIN||errno==EWOULDBLOCK))
                return -100;
            perror("recv()");
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
        if(size>0)
        {
            data=string(buffer.get(),size);
            port=chenfan.sin_port;
	        ip.assign(inet_ntoa(chenfan.sin_addr));
        }
        else
        {
            if((errno==EAGAIN||errno==EWOULDBLOCK))
                return -100;
            perror("recv()");
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
        if(size<0)
        {
            if(flag1==true&&(errno==EAGAIN||errno==EWOULDBLOCK))
                return -100;
            perror("recv()");
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
            perror("recv()");
        }
        port=chenfan.sin_port;
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
	        if(bind(fd,(struct sockaddr*)&k,sizeof(k))!=0)//成
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
	        if(bind(fd,(struct sockaddr*)&k,sizeof(k))!=0)//成
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
        if(sendData(ss)!=ss.length())
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
                    body=totalRecv.substr(pos);
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
                        body=totalRecv.substr(pos);
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

        //套上套接字完成，后面进行发送请求报文操作

        //拼接报文
        string ss;
        ss="GET "+locPara+" HTTP/1.1\r\n"\
            +"Host: "+ip+":"+to_string(port)+"\r\n"\
            +header1+"\r\n"\
            +header+"\r\n";

        //发送
        if(k.sendData(ss)!=ss.length())
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
                    body=totalRecv.substr(pos);
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
                        body=totalRecv.substr(pos);
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
                cout<<ip<<endl;
                cout<<port<<endl;
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
        if(sendData(ss)!=ss.length())
            return false;
        //接收
        string totalRecv="";
        string recv;
        while(recv.find("\r\n\r\n")==string::npos)
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
                    this->body=totalRecv.substr(pos);
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
                        this->body=totalRecv.substr(pos);
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
        if(k.sendData(ss)!=ss.length())
            return false;
        //接收
        string totalRecv="";
        string recv;
        while(recv.find("\r\n\r\n")==string::npos)
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
                    this->body=totalRecv.substr(pos);
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
                        this->body=totalRecv.substr(pos);
                        totalRecv.erase(pos);
                        this->header=totalRecv;
                        flag=true;
                        return true;
                    }
                }
            }
        }
    }
    bool stt::network::TcpServer::setTLS(const char *cacert,const char *key,const char *passwd,const char *ca)
    {
        if(TLS)
            redrawTLS();
        // 初始化
        SSLeay_add_ssl_algorithms();
        OpenSSL_add_all_algorithms();
        SSL_load_error_strings();
        ERR_load_BIO_strings();
        // 我们使用SSL V3,V2
        if((ctx = SSL_CTX_new(SSLv23_method())) == NULL)
        {
            cerr<<"new ctx wrong"<<endl;
            return false;
        }

        // 要求校验对方证书，这里建议使用SSL_VERIFY_FAIL_IF_NO_PEER_CERT，详见https://blog.csdn.net/u013919153/article/details/78616737
        //对于服务器端来说如果使用的是SSL_VERIFY_PEER且服务器端没有考虑对方没交证书的情况，会出现只能访问一次，第二次访问就失败的情况。
        SSL_CTX_set_verify(ctx, SSL_VERIFY_FAIL_IF_NO_PEER_CERT, NULL);

        // 加载CA的证书
        if(!SSL_CTX_load_verify_locations(ctx, ca, NULL))
        {
            cerr<<"load ca wrong"<<endl;
            return false;
        }
        // 加载自己的证书
        if(SSL_CTX_use_certificate_chain_file(ctx, cacert) <= 0)
        {
            cerr<<"load cert wrong"<<endl;
            return false;
        }
        //assert(SSL_CTX_use_certificate_file(ctx, "cacert.pem", SSL_FILETYPE_PEM) > 0);
        // 加载自己的私钥 
        if(passwd!="")
            SSL_CTX_set_default_passwd_cb_userdata(ctx, (void*)passwd);
        if(SSL_CTX_use_PrivateKey_file(ctx, key, SSL_FILETYPE_PEM) <= 0)
        {
            cerr<<"load key wrong"<<endl;
            return false;
        }
 
        // 判定私钥是否正确  
        if(!SSL_CTX_check_private_key(ctx))
        {
            cerr<<"key wrong"<<endl;
            return false;
        }

        TLS=true;
        signal(SIGPIPE,SIG_IGN);
        return true;
    }
    void stt::network::TcpServer::redrawTLS()
    {
        if(TLS)
        {
            SSL_CTX_free(ctx);
            TLS=false;
        }
    }
    bool stt::network::TcpServer::startListen(const int &port,const int &threads)
    {
        if(threads<=0)
            return false;
        if(isListen())
        {
            //是否是改变端口的监听？
            if(this->port==port)
                return true;
            else
            {
                stopListen();
                flag=false;
            }
        }
        //this->logfile=logfile;
        //memset(solvingFD,0,sizeof(int)*maxFD);
        //clientfd=new TcpFDInf[maxFD];
        cv=new condition_variable[threads];
        //socket准备
        fd=socket(AF_INET,SOCK_STREAM,0);
        if(fd<0)
        {
            perror("socket");
            return false;
        }
        //设置unblock和mutiuse
        int flags=fcntl(fd,F_GETFL,0);//获取当前标志
        fcntl(fd,F_SETFL,flags|O_NONBLOCK);
        int opt=1;
        if(setsockopt(fd,SOL_SOCKET,SO_REUSEADDR|SO_REUSEPORT,&opt,sizeof(opt))<0)
        {
            cerr<<"set multi failed"<<endl;
            perror("setsockopt");
            return false;
        }
        //bind
        struct sockaddr_in k;
        memset(&k,0,sizeof(k));
        k.sin_family=AF_INET;
        k.sin_port=htons(port);
        this->port=port;
        k.sin_addr.s_addr=htonl(INADDR_ANY);
        if(bind(fd,(struct sockaddr*)&k,sizeof(k))!=0)
        {
            perror("bind");
            ::close(fd);
            return false;
        }
        //listen
        if(listen(fd,5)!=0)
        {
            perror("listen");
            ::close(fd);
            return false;
        }
        //this->logfile=logfile;
        flag1=true;
        flag2=false;
        this->unblock=true;
        for(int sj=0;sj<threads;sj++)
            thread(&TcpServer::consumer,this,sj).detach();
        thread(&TcpServer::epolll,this,maxFD+10).detach();
        this->consumerNum=threads;
        flag=true;
        //this->logfile=logfile;
        return true;
    }
    bool stt::network::TcpServer::stopListen()
    {
        if(!isListen())
        {
            return true;
        }
        shutdown(fd,SHUT_RDWR);
        ::close(fd);
        //随后取消掉几个监听和消费线程
        flag1=false;
        for(int ii=0;ii<consumerNum;ii++)
            cv[ii].notify_all();
        while(consumerNum!=0||!flag2)
        {
            flag1=false;//不断提醒关闭
            for(int ii=0;ii<consumerNum;ii++)
                cv[ii].notify_all();
        }
        flag=false;
        return true;
    }
    bool stt::network::TcpServer::close()
    {
        if(isListen())
        {
            if(!stopListen())
                return false;
        }
        unique_lock<mutex> lock2(lc1);
        //unique_lock<mutex> lock1(ltl1);
        for(auto &ii:clientfd)
        {
            if(this->security_open)
                connectionLimiter.clearIP(ii.second.ip);
            //auto jj=tlsfd.find(ii.first);
            if(ii.second.ssl!=nullptr)//这个套接字启用了tls
            {
                SSL_shutdown(ii.second.ssl);
                SSL_free(ii.second.ssl);
            }
            shutdown(ii.first,SHUT_RDWR);
            ::close(ii.first);
        }
        clientfd.clear();
        //key.clear();
        //functionT.clear();
        //redrawTLS();
        return true;
    }
    bool stt::network::TcpServer::close(const int &fd)
    {
        unique_lock<mutex> lock2(lc1);
        //unique_lock<mutex> lock1(ltl1);
        auto ii=clientfd.find(fd);
        if(ii==clientfd.end())
        {
            return false;
        }
        else
        {
            if(this->security_open)
                connectionLimiter.clearIP(ii->second.ip);
            //auto jj=tlsfd.find(fd);
            if(ii->second.ssl!=nullptr)
            {
                SSL_shutdown(ii->second.ssl);
                SSL_free(ii->second.ssl);
            }
            ::close(fd);
            clientfd.erase(ii);
        }
        return true;
    }
    void stt::network::TcpServer::epolll(const int &evsNum)
    {
        int epollFD=epoll_create(1);//创建epoll句柄
        epoll_event ev;//epoll事件的数据结构
        ev.data.fd=fd;
        ev.events=EPOLLIN|EPOLLET;//边缘触发
        epoll_ctl(epollFD,EPOLL_CTL_ADD,fd,&ev);//把事件放入epoll中

        //检查连接表里面是不是有套接字，有的话加入
        if(!clientfd.empty())
        {
            unique_lock<mutex> lock6(lc1);
            for(auto &ii:clientfd)
            {
                cout<<"has:"<<ii.first<<endl;
                ev.data.fd=ii.first;
                
                    ev.events=EPOLLIN|EPOLLET;//边缘触发

                epoll_ctl(epollFD,EPOLL_CTL_ADD,ii.first,&ev);//把事件放入epoll中
            }
        }
        epoll_event *evs=new epoll_event[evsNum];//存放epoll返回的事件

        //用来accept的
        struct sockaddr_in k;
        socklen_t k_len=sizeof(k);
        //用来加密accept的
        SSL *ssl;
        int ret;
        TcpFDInf inf;
        if(stt::system::ServerSetting::logfile!=nullptr)
        {
            if(stt::system::ServerSetting::language=="Chinese")
                stt::system::ServerSetting::logfile->writeLog("tcp server epoll打开");
            else
                stt::system::ServerSetting::logfile->writeLog("tcp server epoll has opened");
        }
        while(flag1)
        {
            //监听等待，一秒钟检查一次flag条件是否满足
            int infds=epoll_wait(epollFD,evs,evsNum,1000);
            if(infds<=0)//<0失败=0超时
            {
                continue;
            }
            else//有事发生
            {
                for(int ii=0;ii<infds;ii++)
                {
                    if(evs[ii].data.fd==fd)//有新的连接
                    {
                        while(1)
                        {  

                            k_len = sizeof(k);
                            int cfd=accept(fd,(struct sockaddr*)&k,&k_len);
                            if(cfd<0)
                            {
                                if(errno==EAGAIN||errno==EWOULDBLOCK)
                                    break;//全部连接都accept了
                                else//真的失败
                                {
                                    if(stt::system::ServerSetting::logfile!=nullptr)
                                    {
                                        if(stt::system::ServerSetting::language=="Chinese")
                                            stt::system::ServerSetting::logfile->writeLog("tcp server epoll:accept错误 error="+errno);
                                        else
                                            stt::system::ServerSetting::logfile->writeLog("tcp server epoll:accept failed error="+errno);
                                    }
                                    continue;
                                }
                            }
                            
                            string ip(inet_ntoa(k.sin_addr));//获取客户端的ip
                            
                            if(this->security_open)
                            {
                            if(!connectionLimiter.allow(ip))
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
                            inf.ssl=nullptr;
                            if(TLS)//加密accept
                            {
                                ssl=SSL_new(ctx);
                                if(ssl==nullptr)
                                {
                                    cerr<<"new ssl wrong"<<endl;
                                    continue;
                                }
                                SSL_set_accept_state(ssl);
                                //关联这个fd和ss
                                SSL_set_fd(ssl,cfd);
                                //tls accept
                                ret=SSL_accept(ssl);
                                if(ret!=1)
                                {
                                    //printf("SSL_accept error: %s\n", ERR_error_string(ERR_get_error(), nullptr));
    	                            //printf("SSL_accept returned %d, SSL error code: %d\n", ret, SSL_get_error(ssl, ret));
                                    ::close(cfd);
                                    continue;
                                }
                                //unique_lock<mutex> lock1(ltl1);
                                //tlsfd.emplace(cfd,ssl);//emplace???erase???
                                inf.ssl=ssl;
                            }
                            //cout<<"ok"<<endl;
                            //epoll注册
                            ev.data.fd=cfd;
                            
                                ev.events=EPOLLIN|EPOLLERR | EPOLLHUP | EPOLLRDHUP|EPOLLET;//边缘触发

                            epoll_ctl(epollFD,EPOLL_CTL_ADD,cfd,&ev);
                            cout<<"listen:"<<cfd<<endl;
                            //对象表注册
                            string port=to_string(k.sin_port);//获取客户端的端口
                            //string ip(inet_ntoa(k.sin_addr));//获取客户端的ip
                            inf.fd=cfd;
                            inf.ip=ip;
                            inf.port=port;
                            inf.status=0;
                            inf.data="";
                            unique_lock<mutex> lock6(lc1);
                            clientfd.emplace(cfd,inf);
                            lock6.unlock();
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
                    else//有数据上来了
                    {
                        if(evs[ii].events&(EPOLLERR | EPOLLHUP | EPOLLRDHUP))
                        {

                                
                                if(stt::system::ServerSetting::logfile!=nullptr)
                                {
                                    if(stt::system::ServerSetting::language=="Chinese")
                                        stt::system::ServerSetting::logfile->writeLog("tcp server epoll:收到连接关闭消息：fd= "+to_string(evs[ii].data.fd)+" 已经放入队列");
                                    else
                                        stt::system::ServerSetting::logfile->writeLog("tcp server epoll:receive connection close message: fd= "+to_string(evs[ii].data.fd)+" and it has been pushed into queue");
                                }
                                

                                fdQueue.push(QueueFD{evs[ii].data.fd,true});
                                cv[evs[ii].data.fd%maxFD].notify_all();
                            

                        }
                        else
                        {
                            if(stt::system::ServerSetting::logfile!=nullptr)
                            {
                                if(stt::system::ServerSetting::language=="Chinese")
                                    stt::system::ServerSetting::logfile->writeLog("tcp server epoll:收到新数据：fd= "+to_string(evs[ii].data.fd));
                                else
                                    stt::system::ServerSetting::logfile->writeLog("tcp server epoll:has received new data: fd= "+to_string(evs[ii].data.fd));
                            }
                            

                            fdQueue.push(QueueFD{evs[ii].data.fd,false});
                            cv[evs[ii].data.fd%maxFD].notify_all();
                        }
                    }
                }
            }
        }
        delete evs;
        if(stt::system::ServerSetting::logfile!=nullptr)
        {
            if(stt::system::ServerSetting::language=="Chinese")
                stt::system::ServerSetting::logfile->writeLog("tcp服务器监听的epoll退出");
            else
                stt::system::ServerSetting::logfile->writeLog("tcp server's listening epoll quit");
        }
        cout<<"epoll quit"<<endl;
        flag2=true;
    }
    void stt::network::TcpServer::consumer(const int &threadID)
    {
        TcpFDHandler k;
        TcpFDInf inf;
        unique_lock<mutex> ul1(lq1);
        bool first=true;
        if(stt::system::ServerSetting::logfile!=nullptr)
        {
            if(stt::system::ServerSetting::language=="Chinese")
            stt::system::ServerSetting::logfile->writeLog("tcp server consumer "+to_string(threadID)+" 打开");
            else
                stt::system::ServerSetting::logfile->writeLog("tcp server consumer "+to_string(threadID)+" 打开");
        }
        while(flag1)
        {
            if(first)
                first=!first;
            else
                ul1.lock();
            //unique_lock<mutex> ul1(lq1);
            while(fdQueue.empty())
            {
                cv[threadID].wait(ul1);
                if(!flag1)
                {
                    ul1.unlock();
                    break;
                }
            }
            if(!flag1)
            {
                ul1.unlock();
                break;
            }
            
            QueueFD cclientfd=fdQueue.front();
            fdQueue.pop();
            
            ul1.unlock();

            if(cclientfd.close)
            {
                TcpServer::close(cclientfd.fd);
                if(stt::system::ServerSetting::language=="Chinese")
                stt::system::ServerSetting::logfile->writeLog("tcp server consumer "+to_string(threadID)+" : 完成关闭fd= "+to_string(cclientfd.fd));
                else
                    stt::system::ServerSetting::logfile->writeLog("tcp server consumer "+to_string(threadID)+" :has closed fd= "+to_string(cclientfd.fd));
                continue;
            }

            if(stt::system::ServerSetting::logfile!=nullptr)
            {
                if(stt::system::ServerSetting::language=="Chinese")
                stt::system::ServerSetting::logfile->writeLog("tcp server consumer "+to_string(threadID)+" : 正在处理fd= "+to_string(cclientfd.fd));
                else
                    stt::system::ServerSetting::logfile->writeLog("tcp server consumer "+to_string(threadID)+" :now handleing fd= "+to_string(cclientfd.fd));
            }
            
            
            unique_lock<mutex> lock6(lc1);
            auto jj=clientfd.find(cclientfd.fd);
            if(jj==clientfd.end())//can not find fd information,we need to writedown this error and close this fd
            {
                lock6.unlock();
                continue;
            }
            else
            {
                k.setFD(cclientfd.fd,jj->second.ssl,unblock);
                inf=jj->second;
            }
            lock6.unlock();
            
            if(!fc(k,inf))
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
    bool stt::network::HttpServerFDHandler::sendBack(const string &data,const string &header,const string &code,const string &header1)
    {
        string result="HTTP/1.1 "+code+"\r\nContent-Length: "+\
             to_string(data.length())+"\r\n"+\
             header1+\
             header+"\r\n"+\
             data;
        if(sendData(result)!=result.length())
            return false;
        return true;
    }
    
    bool stt::network::HttpServerFDHandler::sendBack(char *data,const size_t &length,const string &header,const string &code,const string &header1)
    {
        string ii="HTTP/1.1 "+code+"\r\nContent-Length: "+\
             to_string(length)+"\r\n"+\
             header1+\
             header+"\r\n";
        char *buffer=new char[length+ii.length()];
        memcpy(buffer,ii.c_str(),ii.length());
        memcpy(buffer+ii.length(),data,length);
        if(sendData(buffer,length+ii.length())!=length+ii.length())
        {
            delete buffer;
            return false;
        }
        delete buffer;
        return true;
    }
    
    int stt::network::HttpServerFDHandler::solveRequest(TcpFDInf &TcpInf)
    {
        string recv="";
        int ret=1;
        while(ret>0)
        {
            TcpInf.data+=recv;
            recv.clear();
            ret=recvData(recv,8192);//8kb one time
        }
        if(ret<=0)
        {
            if(ret!=-100)
                return -1;
        }

        //数据准备完成，开始判断

        if(TcpInf.status==0||TcpInf.status==1)
        {
            auto pos=TcpInf.data.find("\r\n\r\n");
            if(pos==string::npos)
            {
                if(TcpInf.status==0)
                    TcpInf.status=1;
                return 0;
            }
            
            //转到状态2或者3
            TcpInf.HttpInf.header=TcpInf.data.substr(0,pos);
            HttpStringUtil::get_split_str(TcpInf.HttpInf.header,TcpInf.HttpInf.locPara," "," ");
            HttpStringUtil::get_location_str(TcpInf.HttpInf.locPara,TcpInf.HttpInf.loc);
            HttpStringUtil::getPara(TcpInf.HttpInf.locPara,TcpInf.HttpInf.para);
            TcpInf.HttpInf.body="";

            TcpInf.data=TcpInf.data.substr(pos+2);
            if(TcpInf.data.find("Transfer-Encoding: chunked")!=string::npos)//状态2
            {
                TcpInf.status=2;
                if(TcpInf.data.find("0\r\n\r\n")==string::npos)
                {
                    return 0;
                }
                string size;
                int ssize;
                size_t ppos=0;
                while(1)
                {
                    HttpStringUtil::get_split_str(TcpInf.data,size,"\r\n","\r\n",ppos);
                    ppos=TcpInf.data.find("\r\n",ppos+2);
                    NumberStringConvertUtil::str16toInt(size,ssize);
                    if(ssize==-1)
                    {
                        return -1;
                    }
                    else if(ssize==0)
                    {
                        TcpInf.data=TcpInf.data.substr(ppos+5);
                        TcpInf.status=0;
                        return 1;
                    }
                    else//读取数据
                    {
                        TcpInf.HttpInf.body+=TcpInf.data.substr(ppos+2,ssize);
                        ppos=ppos+2+ssize;
                    }
                }
            }
            else//状态3
            {
                TcpInf.status=3;
                string size;
                long ssize;
                HttpStringUtil::get_split_str(TcpInf.HttpInf.header,size,"Content-Length: ","\r\n");
                NumberStringConvertUtil::toLong(size,ssize);

                if(ssize==-1)
                {
                    if(TcpInf.HttpInf.header.find("GET")!=string::npos)
                    {
                        TcpInf.status=0;
                        return 1;
                    }
                    else
                        return -1;
                }
                else if(ssize==0)
                {
                    TcpInf.status=0;
                    return 1;
                }
                else if(ssize>0)
                {
                    if(TcpInf.data.length()-2>=ssize)
                    {
                        TcpInf.HttpInf.body=TcpInf.data.substr(2,ssize);
                        if(TcpInf.data.length()-2==ssize)
                            TcpInf.data.clear();
                        else
                            TcpInf.data=TcpInf.data.substr(ssize+2);
                        TcpInf.status=0;
                        return 1;
                    }
                    else
                    {
                        return 0;
                    }
                }
            }
        }
        else if(TcpInf.status==2)
        {
                if(TcpInf.data.find("0\r\n\r\n")==string::npos)
                {
                    return 0;
                }
                string size;
                int ssize;
                size_t ppos=0;
                while(1)
                {
                    HttpStringUtil::get_split_str(TcpInf.data,size,"\r\n","\r\n",ppos);
                    ppos=TcpInf.data.find("\r\n",ppos+2);
                    NumberStringConvertUtil::str16toInt(size,ssize);
                    if(ssize==-1)
                    {
                        return -1;
                    }
                    else if(ssize==0)
                    {
                        TcpInf.data=TcpInf.data.substr(ppos+5);
                        TcpInf.status=0;
                        return 1;
                    }
                    else//读取数据
                    {
                        TcpInf.HttpInf.body+=TcpInf.data.substr(ppos+2,ssize);
                        ppos=ppos+2+ssize;
                    }
                }
        }   
        else if(TcpInf.status==3)
        {
                string size;
                long ssize;
                HttpStringUtil::get_split_str(TcpInf.HttpInf.header,size,"Content-Length: ","\r\n");
                NumberStringConvertUtil::toLong(size,ssize);

                if(ssize==-1)
                {
                    if(TcpInf.HttpInf.header.find("GET")!=string::npos)
                    {
                        TcpInf.status=0;
                        return 1;
                    }
                    else
                        return -1;
                }
                else if(ssize==0)
                {
                    TcpInf.status=0;
                    return 1;
                }
                else if(ssize>0)
                {
                    if(TcpInf.data.length()-2>=ssize)
                    {
                        TcpInf.HttpInf.body=TcpInf.data.substr(2,ssize);
                        if(TcpInf.data.length()-2==ssize)
                            TcpInf.data.clear();
                        else
                            TcpInf.data=TcpInf.data.substr(ssize+2);
                        TcpInf.status=0;
                        return 1;
                    }
                    else
                    {
                        return 0;
                    }
                }
        }

        return -1;

    }
    void stt::network::HttpServer::consumer(const int &threadID)
    {
        HttpServerFDHandler k;
        //TcpFDInf &Tcpinf;
        unique_lock<mutex> ul1(lq1);
        bool first=true;
        if(stt::system::ServerSetting::logfile!=nullptr)
        {
            if(stt::system::ServerSetting::language=="Chinese")
                stt::system::ServerSetting::logfile->writeLog("http server consumer "+to_string(threadID)+" 打开");
            else
                stt::system::ServerSetting::logfile->writeLog("http server consumer "+to_string(threadID)+" has opened");
        }

        while(flag1)
        {
            if(first)
                first=!first;
            else
                ul1.lock();
            //unique_lock<mutex> ul1(lq1);
            while(fdQueue.empty())
            {
                cv[threadID].wait(ul1);
                if(!flag1)
                {
                    break;
                }
            }
            if(!flag1)
            {
                ul1.unlock();
                break;
            }
            QueueFD cclientfd=fdQueue.front();
            fdQueue.pop();
            
            ul1.unlock();
            if(cclientfd.close)
            {
                TcpServer::close(cclientfd.fd);
                if(stt::system::ServerSetting::language=="Chinese")
                stt::system::ServerSetting::logfile->writeLog("tcp server consumer "+to_string(threadID)+" : 完成关闭fd= "+to_string(cclientfd.fd));
                else
                    stt::system::ServerSetting::logfile->writeLog("tcp server consumer "+to_string(threadID)+" :has closed fd= "+to_string(cclientfd.fd));
                continue;
            }

            unique_lock<mutex> lock6(lc1);
            auto jj=clientfd.find(cclientfd.fd);
            if(jj==clientfd.end())//can not find fd information,we need to writedown this error and close this fd
            {
                lock6.unlock();
                continue;
            }
            else
            {
                k.setFD(cclientfd.fd,jj->second.ssl,unblock);
                TcpFDInf &Tcpinf=jj->second;
            lock6.unlock();

             if(stt::system::ServerSetting::logfile!=nullptr)
             {
                if(stt::system::ServerSetting::language=="Chinese")
                    stt::system::ServerSetting::logfile->writeLog("http server consumer "+to_string(threadID)+" : 开始处理fd= "+to_string(cclientfd.fd));
                else
                    stt::system::ServerSetting::logfile->writeLog("http server consumer "+to_string(threadID)+" : now start solveing fd= "+to_string(cclientfd.fd));
             }
            
            
             if(stt::system::ServerSetting::logfile!=nullptr)
             {
                if(stt::system::ServerSetting::language=="Chinese")
                    stt::system::ServerSetting::logfile->writeLog("http server consumer "+to_string(threadID)+" : fd= "+to_string(cclientfd.fd)+" 正在解析请求...");
                else
                    stt::system::ServerSetting::logfile->writeLog("http server consumer "+to_string(threadID)+" : fd= "+to_string(cclientfd.fd)+" now solveing request...");
             }

            int ret=k.solveRequest(Tcpinf);
            if(ret==-1)
            {
                    TcpServer::close(cclientfd.fd);
                    if(stt::system::ServerSetting::logfile!=nullptr)
                    {
                        if(stt::system::ServerSetting::language=="Chinese")
                            stt::system::ServerSetting::logfile->writeLog("http server consumer "+to_string(threadID)+" : fd= "+to_string(cclientfd.fd)+" 解析请求失败或者是对方已经关闭连接，服务器已经关闭这个连接");
                        else
                            stt::system::ServerSetting::logfile->writeLog("http server consumer "+to_string(threadID)+" : fd= "+to_string(cclientfd.fd)+" solved request fail or host had closed connection.now server has closed this connection");
                    }
                continue;
            }

            else if(ret==1)
            {
                cout<<"fd="<<cclientfd.fd<<endl;
                if(stt::system::ServerSetting::logfile!=nullptr)
                {
                    if(stt::system::ServerSetting::language=="Chinese")
                    {
                        stt::system::ServerSetting::logfile->writeLog("http server consumer "+to_string(threadID)+" : fd= "+to_string(cclientfd.fd)+" 正在处理请求... \n*******请求信息：*********\nheader= "+Tcpinf.HttpInf.header+"\nbody="+Tcpinf.HttpInf.body+"\n*************************");
                    }
                    else
                    {
                        stt::system::ServerSetting::logfile->writeLog("http server consumer "+to_string(threadID)+" : fd= "+to_string(cclientfd.fd)+" now handleing request...\n*******request information：*********\nheader= "+Tcpinf.HttpInf.header+"\nbody="+Tcpinf.HttpInf.body+"\n*************************");
                    }

                }
                if(!fc(Tcpinf.HttpInf,k))
                {
                
                    close(cclientfd.fd);
                    if(stt::system::ServerSetting::logfile!=nullptr)
                    {
                        if(stt::system::ServerSetting::language=="Chinese")
                            stt::system::ServerSetting::logfile->writeLog("http server consumer "+to_string(threadID)+" : 处理fd= "+to_string(cclientfd.fd)+"失败或者是对方已经关闭连接，已经关闭连接");
                        else
                           stt::system::ServerSetting::logfile->writeLog("http server consumer "+to_string(threadID)+" : handle fd= "+to_string(cclientfd.fd)+"fail or host had closed connection.now server has closed this connection");
                    }

                }

                else
                {
                    if(stt::system::ServerSetting::logfile!=nullptr)
                    {
                        if(stt::system::ServerSetting::language=="Chinese")
                            stt::system::ServerSetting::logfile->writeLog("http server consumer "+to_string(threadID)+" : 处理fd= "+to_string(cclientfd.fd)+"完成");
                        else
                            stt::system::ServerSetting::logfile->writeLog("http server consumer "+to_string(threadID)+" : has solved fd= "+to_string(cclientfd.fd)+"sucessfully");
                    }
                }
            }
            else if(ret==0)
            {
                    if(stt::system::ServerSetting::logfile!=nullptr)
                    {
                        if(stt::system::ServerSetting::language=="Chinese")
                            stt::system::ServerSetting::logfile->writeLog("http server consumer "+to_string(threadID)+" : 解析fd= "+to_string(cclientfd.fd)+"未完成 等待新的数据继续解析");
                        else
                            stt::system::ServerSetting::logfile->writeLog("http server consumer "+to_string(threadID)+" : fd= "+to_string(cclientfd.fd)+"wait new data to continue solve this request");
                    }
            }
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

    
    bool stt::network::EpollSingle::endListen()
    {
        flag1=false;
        while(flag2);
        return true;
    }
    void stt::network::EpollSingle::epolll()
    {
        int epollFD=epoll_create(1);
        epoll_event ev;
        ev.data.fd=fd;
        if(flag==false)
            ev.events=EPOLLIN|EPOLLET;
        else
            ev.events=EPOLLIN;
        epoll_ctl(epollFD,EPOLL_CTL_ADD,fd,&ev);
        epoll_event evs;

        DateTime timer1;
        Duration dt;
        DateTime timer2;
        Duration t;
        while(flag1)
        {
            if(!timer1.isStart())
                timer1.startTiming();
            int infds=epoll_wait(epollFD,&evs,1,1000);
            if(infds<0)
                continue;
            else if(infds==0)
            {
                if(!flag3)
                {
                    dt=timer1.checkTime();
                    if(dt>=this->dt)
                    {
                        if(!fcTimeOut(fd))
                            break;
                    }
                }
                else
                {
                    if(!timer2.startTiming())//说明已经不是第一次，而是在计时中了
                    {
                        t=timer2.checkTime();
                        if(t>=this->t)
                            break;
                    }
                }
            }
            else//有事件发生了
            {
                if(flag3)//打开了退出倒计时
                {
                    flag3=false;
                    timer2.endTiming();
                }
                timer1.endTiming();
                if(!fc(fd))
                    break;
            }
        }
        //只要跳出了这个循环就意味着要退出了
        //cout<<"epoll quit"<<endl;
        fcEnd(fd);
        dt=Duration{0,20,0,0,0};
        flag1=true;
        flag3=false;
        flag2=false;
    }
    void stt::network::EpollSingle::startListen(const int &fd,const bool &flag,const Duration &dt)
    {
        if(isListen())
        {
            endListen();
        }
        this->fd=fd;
        this->flag=flag;
        this->dt=dt;
        thread(&EpollSingle::epolll,this).detach();
        flag2=true;
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
        char ccode[2];
        memcpy(ccode,&code,2);
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
        string httpURL=url;
        auto pos=httpURL.find("ws");
        httpURL.replace(pos,2,"http");
        string wbKey;
        RandomUtil::getRandomStr_base64(wbKey,24);
        HttpClient k;
        if(!k.getRequestFromFD(TcpFDHandler::getFD(),TcpFDHandler::ssl,httpURL,HttpStringUtil::createHeader("Upgrade","websocket","Connection","Upgrade","Sec-WebSocket-Key",wbKey),"Sec-WebSocket-Version: 13"))
            return false;
        if(!k.isReturn())
            return false;
        if(k.header.find("HTTP/1.1 101")==string::npos)
            return false;
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
                    cout<<"收到对端关闭帧";
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
                    cout<<"收到对端心跳:"<<totalResult<<endl;
                    this->sendMessage(totalResult,"1010");
                    isRec=false;
                }
                else if(code=="1010")
                {
                    cout<<"收到对端心跳响应"<<endl;
                    isRec=false;
                }
                BitUtil::bitOutput_bit(b1,1,b1);
            }while(BitUtil::bitOutput(b1,code).substr(0,1)=="0"&&BitUtil::bitOutput(b1,code).substr(4)=="0000"&&isRec);  

            //用回调函数处理
            if(isRec)
                return this->fc(totalResult,*this);//处理失败epoll会退出
            return true;
        };
        auto endfc=[this](const int &fd)->void
        {
            this->close1();
        };
        auto timeoutfc=[this](const int &fd)->bool
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
    bool stt::network::WebSocketClient::sendMessage(const string &message,const string &type)
    {
        if(!isConnect())
        {
            cerr<<"websocket对象没有连接,发送失败"<<endl;
            return false;
        }
        string typee="1000"+type;
        string mess=message;
        unsigned long size=message.length();//生成extern payload len前的东西,默认一片长度为8的数据帧发完数据

        if(size<=125)
        {
            cout<<"send1+size="<<size<<"+::"<<message<<endl;
            string header;
            BitUtil::toBit(typee,header);

            char cc;
            memcpy(&cc,&size,1);
            cc=cc|'z'+6;
        
            //设置mask
            string mask;
            EncodingUtil::generateMask_4(mask);
        
            //mask处理数据
            EncodingUtil::maskCalculate(mess,mask);
            //拼接数据帧
            mess=header+cc+mask+mess;
        }
        else if(size<=65535)
        {
            cout<<"send2+size="<<size<<"+::"<<message<<endl;
            unsigned short tt=(unsigned short)size;
            tt=htons(tt);
            string header;
            BitUtil::toBit(typee+"11111110",header);
            char ss[2];//把int数据转换成char，底层二进制数据不变，用指针复制内存的办法
            memcpy(ss,&tt,2);
            //设置mask
            string mask;
            EncodingUtil::generateMask_4(mask);
            EncodingUtil::maskCalculate(mess,mask);
            //拼接数据帧
            mess=header+string(ss,2)+mask+mess;
        }
        else
        {
            cout<<"send3+size="<<size<<"+::"<<message<<endl;
            NetworkOrderUtil::htonl_ntohl_64(size);
            string header;
            BitUtil::toBit(typee+"11111111",header);
            char ss[8];//把int数据转换成char，底层二进制数据不变，用指针复制内存的办法
            memcpy(ss,&size,sizeof(size));
            //设置mask
            string mask;
            EncodingUtil::generateMask_4(mask);
            //mask处理数据
            EncodingUtil::maskCalculate(mess,mask);
            //拼接数据帧
            mess=header+string(ss,8)+mask+mess;
        }
    

        //发送数据帧
        if(sendData(mess)!=mess.length())
        {
            cout<<"send failed"<<endl;
            return false;
        }
        return true;
    }
    
    int stt::network::WebSocketServerFDHandler::getMessage(TcpFDInf &Tcpinf,WebSocketFDInformation &Websocketinf)
    {
            string recv="";
            int ret=1;
            while(ret>0)
            {
                Tcpinf.data+=recv;
                recv.clear();
                ret=recvData(recv,8192);
            }
            if(ret<=0&&ret!=-100)
                return -1;
            //数据接收完，开始处理
            
            while(1)
            {
                //第一个字节
                if(Tcpinf.status==0||Tcpinf.status==1)
                {
                    if(Tcpinf.status==0)
                        Tcpinf.status=1;
                    if(Tcpinf.data.length()<1)
                        return 4;
                    char b1=Tcpinf.data[0];
                    string code;
                    BitUtil::bitOutput(b1,code);
                    if(code.substr(0,1)=="1")
                    {
                        
                        Websocketinf.fin=true;
                    }
                    else
                    {
                        
                        Websocketinf.fin=false;
                    }
                    code=code.substr(4);

                    if(code=="1000")
                    {
                        Websocketinf.message_type=1;
                    }
                    else if(code=="1001")
                    {
                        Websocketinf.message_type=3;
                    }
                    else if(code=="1010")
                    {
                        Websocketinf.message_type=2;
                    }
                    else
                       Websocketinf.message_type=0;
                    Tcpinf.status=2;

                    if(Tcpinf.data.length()>1)
                        Tcpinf.data=Tcpinf.data.substr(1);
                    else
                    {
                        Tcpinf.data.clear();
                        return 4;
                    }
                    Websocketinf.recv_length=1;
                    
                }
                //长度
                if(Tcpinf.status==2)
                {
                    
                    unsigned long sizee;//payload len
                    if(Websocketinf.recv_length==1)
                    {
                        if(Tcpinf.data.length()<1)
                            return 4;
                        char b2=Tcpinf.data[0];
                        string ssize;
                        BitUtil::bitOutput(b2,ssize);
                        ssize=ssize.substr(1);
                        BitUtil::bitStrToNumber(ssize,sizee);//单字节里的是大端序 
                        if(sizee==126)
                            Websocketinf.recv_length=2;
                        else if(sizee==127)
                            Websocketinf.recv_length=8;
                        else
                        {
                            Tcpinf.status=3;
                            Websocketinf.recv_length=sizee;
                            if(Tcpinf.data.length()<=1)
                            {
                                Tcpinf.data.clear();
                                return 4;
                            }
                            else
                                Tcpinf.data=Tcpinf.data.substr(1);
                        }
                    }
                    if(Websocketinf.recv_length==2&&Tcpinf.status==2)
                    {
                    
                        if(Tcpinf.data.length()<=1)
                        {
                            Tcpinf.data.clear();
                            return 4;
                        }
                        Tcpinf.data=Tcpinf.data.substr(1);
                        sizee=BitUtil::bitToNumber(Tcpinf.data.substr(0,2),sizee);
                        Tcpinf.status=3;
                        Websocketinf.recv_length=sizee;
                        if(Tcpinf.data.length()<=2)
                        {
                            Tcpinf.data.clear();
                            return 4;
                        }
                        else
                            Tcpinf.data=Tcpinf.data.substr(2);
                    }
                    else if(Websocketinf.recv_length==8&&Tcpinf.status==2)
                    {
                        
                        if(Tcpinf.data.length()<=1)
                        {
                            Tcpinf.data.clear();
                            return 4;
                        }
                        Tcpinf.data=Tcpinf.data.substr(1);
                        sizee=BitUtil::bitToNumber(Tcpinf.data.substr(0,8),sizee);
                        Tcpinf.status=3;
                        Websocketinf.recv_length=sizee;
                        if(Tcpinf.data.length()<=8)
                        {
                            Tcpinf.data.clear();
                            return 4;
                        }
                        else
                            Tcpinf.data=Tcpinf.data.substr(8);
                    }
                    
                    
                }
                
                //接收mask
                if(Tcpinf.status==3)
                {
                    
                    if(Tcpinf.data.length()<4)
                        return 4;
                    Websocketinf.mask=Tcpinf.data.substr(0,4);

                    Tcpinf.status=4;

                    if(Tcpinf.data.length()<5)
                    {
                        if(Websocketinf.recv_length!=0)
                        {
                            Tcpinf.data.clear();
                            return 4;
                        }
                    }
                    Tcpinf.data=Tcpinf.data.substr(4);
                    
                }
                
                //接收数据
                if(Tcpinf.status==4)
                {
                    if(Websocketinf.recv_length!=0)
                    {
                    if(Tcpinf.data.length()<1)
                        return 4;
                   
                    string a;
                    if(Tcpinf.data.length()<=Websocketinf.recv_length)
                    {
                        a=Tcpinf.data;
                        Websocketinf.recv_length=Websocketinf.recv_length-Tcpinf.data.length();
                        Tcpinf.data.clear();
                    }
                    else
                    {
                        a=Tcpinf.data.substr(0,Websocketinf.recv_length);
                        Tcpinf.data=Tcpinf.data.substr(Websocketinf.recv_length);
                        Websocketinf.recv_length=0;
                       
                    }
                    EncodingUtil::maskCalculate(a,Websocketinf.mask);
                    Websocketinf.message+=a;
                    if(Websocketinf.recv_length!=0)
                        return 4;
                    }

                    Tcpinf.status=0;
                    if(Websocketinf.message_type==1)
                    {
                        cout<<"收到对端关闭帧";
                        return 1;
                    }
                    else if(Websocketinf.message_type==2)
                    {
                        cout<<"收到对端心跳响应"<<endl;
                        return 2;
                    }
                    else if(Websocketinf.message_type==3)
                    {
                        cout<<"收到对端心跳:"<<Websocketinf.message<<endl;
                        sendMessage(Websocketinf.message,"1010");
                        return 3;
                    }
                    else
                    {
                        if(Websocketinf.fin)
                            break;   
                    }
                    
                    
                }
            }
 
            
             return Websocketinf.message_type;
    }
    bool stt::network::WebSocketServerFDHandler::sendMessage(const string &msg,const string &type)
    {
        string typee="1000"+type;
        string mess=msg;
        unsigned long size=msg.length();//生成extern payload len前的东西,默认一片长度为8的数据帧发完数据

        if(size<=125)
        {
            string header;
            BitUtil::toBit(typee,header);

            char cc;
            memcpy(&cc,&size,1);
            cc=cc&'z'+5;
        
        
            //拼接数据帧
            mess=header+cc+mess;
        }
        else if(size<=65535)
        {
            unsigned short tt=(unsigned short)size;
            tt=htons(tt);
            string header;
            BitUtil::toBit(typee+"01111110",header);
            char ss[2];//把int数据转换成char，底层二进制数据不变，用指针复制内存的办法
            memcpy(ss,&tt,2);
        
            //拼接数据帧
            mess=header+string(ss,2)+mess;
        }
        else
        {
            NetworkOrderUtil::htonl_ntohl_64(size);
            string header;
            BitUtil::toBit(typee+"01111111",header);
            char ss[8];//把int数据转换成char，底层二进制数据不变，用指针复制内存的办法
            memcpy(ss,&size,sizeof(size));
            //拼接数据帧
            mess=header+string(ss,8)+mess;
        }
        //发送数据帧
        if(sendData(mess)!=mess.length())
            return false;
        return true;
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
        sendMessage(fd,closeCodeAndMessage,"1000");
        TcpServer::close(fd);
    }
    void stt::network::WebSocketServer::closeAck(const int &fd,const short &code,const string &message)
    {
        char ccode[2];
        memcpy(ccode,&code,2);
        string codee(ccode,2);
        codee+=message;
        sendMessage(fd,codee,"1000");
        TcpServer::close(fd);
    }
    bool stt::network::WebSocketServer::close(const int &fd,const string &closeCodeAndMessage)
    {
        unique_lock<mutex> lock(lwb);
        auto ii=wbclientfd.find(fd);
        if(ii==wbclientfd.end()||ii->second.closeflag==true)
        {
            return false;
        }
        else
        {
            lock.unlock();
            WebSocketServerFDHandler k;
            k.setFD(fd,getSSL(fd),unblock);
            if(!k.sendMessage(closeCodeAndMessage,"1000"))
            {
                
                
                    lock.lock();
                    auto ii=wbclientfd.find(fd);
                    if(ii!=wbclientfd.end())
                    {
                        TcpServer::close(fd);
                        wbclientfd.erase(ii);
                    }
                    lock.unlock();
                
                
                return true;
            }
            ii->second.closeflag=true;
            return true;
        }
    }
    bool stt::network::WebSocketServer::close(const int &fd,const short &code,const string &message)
    {
        unique_lock<mutex> lock(lwb);
        auto ii=wbclientfd.find(fd);
        if(ii==wbclientfd.end()||ii->second.closeflag==true)//找不到或者已经发送过了
        {
            return false;
        }
        else
        {
            lock.unlock();
            char ccode[2];
            memcpy(ccode,&code,2);
            string codee(ccode,2);
            codee+=message;
            WebSocketServerFDHandler k;
            k.setFD(fd,getSSL(fd),unblock);
            if(!k.sendMessage(codee,"1000"))//发送失败会自动删除在记录表里的
            {
                
                    lock.lock();
                    auto ii=wbclientfd.find(fd);
                    if(ii!=wbclientfd.end())
                    {
                        TcpServer::close(fd);
                        wbclientfd.erase(ii);
                    }
                    lock.unlock();
                
                return true;
            }
            ii->second.closeflag=true;
            return true;
        }
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
            k.setFD(fd,getSSL(fd),unblock);
            if(!k.sendMessage(closeCodeAndMessage,"1000"))
            {
                TcpServer::close(fd);
                wbclientfd.erase(ii);
                return false;
            }
            ii->second.closeflag=true;
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
            char ccode[2];
            memcpy(ccode,&code,2);
            string codee(ccode,2);
            codee+=message;
            WebSocketServerFDHandler k;
            k.setFD(fd,getSSL(fd),unblock);
            if(!k.sendMessage(codee,"1000"))//发送失败会自动删除在记录表里的
            {
                TcpServer::close(fd);
                wbclientfd.erase(ii);
                return false;
            }
            ii->second.closeflag=true;
            return true;
        }
    }
    
    void stt::network::WebSocketServer::consumer(const int &threadID)
    {
        HttpServerFDHandler k;
        //TcpFDInf &Tcpinf;
        WebSocketServerFDHandler k1;
        unique_lock<mutex> ul1(lq1);
        bool first=true;
        if(stt::system::ServerSetting::logfile!=nullptr)
        {
            if(stt::system::ServerSetting::language=="Chinese")
                stt::system::ServerSetting::logfile->writeLog("websocket server consumer "+to_string(threadID)+" 打开");
            else
                stt::system::ServerSetting::logfile->writeLog("websocket server consumer "+to_string(threadID)+" has opened");
        }
        while(flag1)
        {
            if(first)
                first=!first;
            else
                ul1.lock();
            //unique_lock<mutex> ul1(lq1);
            while(fdQueue.empty())
            {
                cv[threadID].wait(ul1);
                if(!flag1)
                {
                    break;
                }
            }
            if(!flag1)
            {
                ul1.unlock();
                break;
            }
            QueueFD cclientfd=fdQueue.front();
            fdQueue.pop();
            
            ul1.unlock();
            if(cclientfd.close)
            {
                TcpServer::close(cclientfd.fd);
                if(stt::system::ServerSetting::language=="Chinese")
                stt::system::ServerSetting::logfile->writeLog("tcp server consumer "+to_string(threadID)+" : 完成关闭fd= "+to_string(cclientfd.fd));
                else
                    stt::system::ServerSetting::logfile->writeLog("tcp server consumer "+to_string(threadID)+" :has closed fd= "+to_string(cclientfd.fd));
                continue;
            }

            unique_lock<mutex> lock6(lc1);
            auto ii=clientfd.find(cclientfd.fd);
            if(ii==clientfd.end())//can not find fd information,we need to writedown this error and close this fd
            {
                lock6.unlock();
                continue;
            }
            else
            {
                k.setFD(cclientfd.fd,ii->second.ssl,unblock);
                TcpFDInf &Tcpinf=ii->second;
            lock6.unlock();
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
                

                int ret=k.solveRequest(Tcpinf);
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
                    winf.locPara=Tcpinf.HttpInf.locPara;
                    winf.header=Tcpinf.HttpInf.header;
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
                    string key;
                    HttpStringUtil::get_value_header(Tcpinf.HttpInf.header,key,"Sec-WebSocket-Key");
                    key+="258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
                    string result="";
                    CryptoUtil::sha1(key,result);
                    key=EncodingUtil::base64_encode(result);
                    result=HttpStringUtil::createHeader("Upgrade","websocket","Connection","Upgrade","Sec-WebSocket-Accept",key);
                    //清理接收缓冲区可能的数据遗漏
                    Tcpinf.data.clear();
                    if(!k.sendBack("",result,"101"))
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
            }
            else//已经进行了握手操作
            {
                
                int r=k1.getMessage(Tcpinf,jj->second);

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
                            stt::system::ServerSetting::logfile->writeLog("websocket server consumer "+to_string(threadID)+" : fd= "+to_string(cclientfd.fd)+" 收到常规信息: "+ jj->second.message);
                        else 
                            stt::system::ServerSetting::logfile->writeLog("websocket server consumer "+to_string(threadID)+" : fd= "+to_string(cclientfd.fd)+" has received normal message:"+ jj->second.message);
                    }
                    jj->second.response=::time(0);
                    if(!fc(jj->second.message,*this,jj->second))//回调函数失败
                    {
                        close(cclientfd.fd);
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
    SSL* stt::network::TcpServer::getSSL(const int &fd)
    {
        //SSL *ssl;
        //unique_lock<mutex> lock1(ltl1);
        //auto ii=tlsfd.find(fd);
        //if(ii==tlsfd.end())
        //    ssl=nullptr;
        //else
        //    ssl=ii->second;
        unique_lock<mutex> lock1(lc1);
        auto ii=clientfd.find(fd);
        if(ii==clientfd.end())
            return nullptr;
        return ii->second.ssl;
    }
    void stt::network::WebSocketServer::HB()
    {
        HBflag=true;
        HBflag1=true;
        time_t now;
        while(HBflag1)
        {
            now=::time(0);
            unique_lock<mutex> lock(lwb);
            //if(stt::system::ServerSetting::logfile!=nullptr)stt::system::ServerSetting::logfile->writeLog("lwb上锁！！！！！！！！！！");
            for(auto ii=wbclientfd.begin();ii!=wbclientfd.end();)
            {
                if(ii->second.HBTime!=0)//已经发送心跳
                {
                    if(now-ii->second.response>secb)//超时
                    {
                        cout<<ii->first<<endl;
                        //if(!closeWithoutLock(ii->first))//如果发送失败就已经被erase了 如果就成功就还要等待后续再erase... 这个时候是占有锁的，跳出循环后别的线程才能占有锁erase
                        //    continue;
                        
                        if(ii->second.closeflag!=true)
                        {
                            short code=1000;
                            char ccode[2];
                            memcpy(ccode,&code,2);
                            string codee(ccode,2);
                            codee+="bye";
                            WebSocketServerFDHandler k;
                            k.setFD(ii->first,getSSL(ii->first),unblock);
                            if(!k.sendMessage(codee,"1000"))//发送失败会自动删除在记录表里的
                            {
                                TcpServer::close(ii->first);
                                ii=wbclientfd.erase(ii);
                                continue;
                            }
                            ii->second.closeflag=true;
                        }
                    }
                }
                else//检查是否需要发送心跳
                {
                    if(now-ii->second.response>seca)
                    {
                        cout<<"send"<<endl;
                        if(!sendMessage(ii->first,"心跳","1001"))//发送心跳失败直接关闭
                        {
                            ii=wbclientfd.erase(ii);
                            TcpServer::close(ii->first);
                            continue;
                        }
                        ii->second.HBTime=now;
                    }
                }
                ++ii;
            }
            //if(stt::system::ServerSetting::logfile!=nullptr)stt::system::ServerSetting::logfile->writeLog("lwb解锁！！！！！！！！！！");
            //cout<<"lwb解锁！！！！！！！！！！"<<endl;
            lock.unlock();
            sleep(30);
        }
        HBflag=false;
    }
    bool stt::network::WebSocketServer::close()
    {
        if(!TcpServer::close())
            return false;
        HBflag1=false;
        while(HBflag);
        return true;
    }
    void stt::network::WebSocketServer::sendMessage(const string &msg,const string &type)
    {
        unique_lock<mutex> lock(lwb);
        for(auto ii=wbclientfd.begin();ii!=wbclientfd.end();)
        {
            if(!sendMessage(ii->first,msg,type))//发送失败直接关闭
            {
                ii=wbclientfd.erase(ii);
                TcpServer::close(ii->first);
                continue;
            }
            ++ii;
        }
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
void stt::system::ServerSetting::setExceptionHandling()
{
    for(int ii=0;ii<=64;ii++)
        signal(ii,SIG_IGN);
    //SIGSEGV
    signal(SIGSEGV,signalSIGSEGV);
    //SIGABRT
    signal(SIGABRT,signalSIGABRT);
    //terminate全局终止函数
    set_terminate(signalterminated);
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
    cout<<"join id= "<<pid<<endl;
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
            memcpy(p[ii].name,name,MAX_PROCESS_NAME);
            memcpy(p[ii].argv0,argv0,20);
            memcpy(p[ii].argv1,argv1,20);
            memcpy(p[ii].argv2,argv2,20);
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
    cout<<"renew= "<<pid<<endl;
    plock.wait();
    for(int ii=0;ii<MAX_PROCESS_INF;ii++)
    {
        if(p[ii].pid==pid)
        {
            p[ii].pid=pid;
            p[ii].lastTime=now;
            cout<<"renwe*** ";
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
            //检查是否存在
                //发送信号杀死
                //检查是否杀死
                int sec=0;
                while(kill(p[ii].pid,0)!=-1&&sec<=7)
                {
                    kill(p[ii].pid,15);
                    sleep(1);
                    sec++;
                }

                while(kill(p[ii].pid,0)!=-1)
                {
                    kill(p[ii].pid,9);
                    sleep(1);
                }
            
            //清理信息
            plock.wait();
            p[ii].pid=0;
            p[ii].lastTime=0;
            plock.post();
            //重新启动
            Process::startProcess(p[ii].name,-1,p[ii].argv0,p[ii].argv1,p[ii].argv2);
        }
    }
    return true;
}
bool stt::system::HBSystem::deleteFromHBS()
{
    if(isJoin&&(p!=nullptr))
    {
        cout<<"清理信息"<<endl;
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
    cout<<"析构函数运行  ~HBS"<<endl;
    if(p!=nullptr)
    {
        if(!deleteFromHBS()||shmdt(p)==-1)
            cerr<<"close shared memory failed"<<endl;
    }
}

bool stt::security::ConnectionLimiter::allow(const std::string &ip)
{
    auto ii=connectionTable.find(ip);
    //找到
    if(ii!=connectionTable.end())
    {
        if(ii->second.connectionNum<connectionLimit)//检查是否达到上限
        {
            //检查速度
            //首先清空大于一秒内的
            chrono::steady_clock::time_point now=chrono::steady_clock::now();
            while(!ii->second.connectionTimeQueue.empty()&&now-ii->second.connectionTimeQueue.front()>chrono::seconds(1))
            {
                ii->second.connectionTimeQueue.pop_front();
            }

            if(ii->second.connectionTimeQueue.size()<connectionRateLimit)//满足条件
            {
                ii->second.connectionNum++;
                ii->second.connectionTimeQueue.push_back(now);
                return true;
            }
            else
                return false;
        }
        else
            return false;
    }
    //找不到
    else
    {
        chrono::steady_clock::time_point now=chrono::steady_clock::now();
        connectionTable.emplace(ip,IPInformation{1,deque<chrono::steady_clock::time_point>{now}});
        return true;
    }
}

void stt::security::ConnectionLimiter::clearIP(const std::string &ip)
{
    auto ii=connectionTable.find(ip);
    if(ii!=connectionTable.end())
    {
        ii->second.connectionNum=0;
    }
}