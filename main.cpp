#include"include/sttnet.h"
using namespace std;
using namespace stt::file;
using namespace stt::time;
using namespace stt::data;
using namespace stt::network;
using namespace stt::system;
//set global variables
//设置全局变量
LogFile lf;
HttpServer *httpserver;
WebSocketServer *s;
int main(int argc,char *argv[])
{
	//init logfile and use logfile system and ignore all signal but 15. And all error signals or unknown exception will transmit signal 15.
	//初始化日志文件，启用日志系统和设置系统信号；屏蔽除了15外所有信号，所有错误信号和异常都会发送信号15
	//remember to set the second parameter if you want your logfile write in Chinese (default in English)
	//如果你想日志系统使用中文记得填入第二个参数（默认英文）
	//ServerSetting::init(&lf,"Chinese");

	//new a HttpServer Objection
	//新建一个HttpServer对象
	httpserver=new HttpServer(500,false);

	//set a callbacl function after signal 15 to quit decently.
	//设置收到信号15后的回调函数，为了优雅退出程序。
	signal(15,[](int signal){
		//lf.writeLog("have received signal 15... Now ready to quit.收到信号15，正在执行退出前的流程");//you can write logfile in English or Chinese as you like here//这里可以按照自己喜好写中文或者英文日志
		delete httpserver;
		/*
			...
			*********You can write the necessary exit process here*************
			*********可以在这里继续写退出之前的处理流程*****************
			...
		*/
	});

	//set a callback function to handle http request
	//设置回调函数处理Http请求
	httpserver->setFunction([](const HttpRequestInformation &info, HttpServerFDHandler &conn) -> bool {

		char data[10]="pong";
		char data1[20]="404 Not Found";
		char header[2]="\0";
		char code[5]="200";
		char code1[5]="404";
		char buffer[1024]="HTTP/1.1 200\r\nContent-Length: 4\r\n\r\npong";
		char buffer1[1024]="HTTP/1.1 404\r\nContent-Length: 11\r\n\r\n404NotFound";
        if (info.loc == "/ping") {
            if(!conn.sendData(buffer,strlen(buffer),false))
				return false;
        } else {
            if(!conn.sendData(buffer1,strlen(buffer1),false))
				return false;
        }
        return true;
    });

	//start listen port 8080 and add logfile to this server objection.
	//监听8080端口，并且加入日志文件到这个服务对象
	httpserver->startListen(8080);

	/*
		s=new WebSocketServer;
		s->setFunction([](const std::string &msg, WebSocketServer &k,const WebSocketFDInformation &inf) -> bool {
			return k.sendMessage(inf.fd,msg);
    });
	s->setStartFunction([](const WebSocketFDInformation &inf, WebSocketServer &k) -> void {
			sleep(10);
			k.close(inf.fd);
    });
	s->startListen(5055);
	*/

	
	//block the main thread
	//阻塞主线程
	pause();
	return 0;
}
