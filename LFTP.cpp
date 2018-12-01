#include <iostream>
#include <fstream>
#include <WinSock2.h>
#include <Windows.h>
#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Winmm.lib")

using namespace std;



/***************全局变量****************************/
unsigned int timeOut = 2000;		//初始时timeOut定为2s
unsigned int timeOutId;
sockaddr_in serverAddr;	
int serveraddrLen;
SOCKET s;
struct data {
	int seq;
	int ack;
	char buff[1024];
};
/**************************************************/


/***********************重传函数********************/
void WINAPI resendGetRequire(UINT wTimerID, UINT msg, DWORD dwUser, DWORD dwl, DWORD dw2) {
	if (timeOut > 60000) {
		cout << "connect server failed!" << endl;
		exit(0);
	}
	cout << "request timeout, resent lget request" << endl;
	timeOut *= 2;
	timeKillEvent(timeOutId);
	timeOutId = timeSetEvent(timeOut, 1, (LPTIMECALLBACK)resendGetRequire, DWORD(1), TIME_PERIODIC);
	sendto(s, "lget", 4, 0, (SOCKADDR *)&serverAddr, serveraddrLen);
}

void WINAPI resendSendRequire(UINT wTimerID, UINT msg, DWORD dwUser, DWORD dwl, DWORD dw2) {
	if (timeOut > 60000) {
		cout << "connect server failed!" << endl;
		exit(0);
	}
	cout << "request timeout, resent lsend request" << endl;
	timeOut *= 2;
	timeKillEvent(timeOutId);
	timeOutId = timeSetEvent(timeOut, 1, (LPTIMECALLBACK)resendSendRequire, DWORD(1), TIME_PERIODIC);
	sendto(s, "lsend", 4, 0, (SOCKADDR *)&serverAddr, serveraddrLen);
}
/**************************************************/


int main(int argc, char* argv[]) {
	//检查参数
	if (argc != 4) {
		cout << "Please input: LFTP {lsend | lget} <myserver> <mylargefile>" << endl;
	}

	//初始化Winsock相关的ddl
	WSAData wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
		cout << "WSAStartup error!" << endl;
		return 0;
	}

	
	if (s == INVALID_SOCKET) {
		cout << "socket create failed!" << endl;
		return 0;
	}

	//服务器地址信息
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_addr.s_addr = inet_addr(argv[2]);
	serverAddr.sin_port = htons(8021);
	serveraddrLen = sizeof(serverAddr);

	//创建socket
	s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

/**************************************************************************************************/
	//上传文件
	if (strncmp(argv[1], "lsend", 5) == 0) {
		//读取文件
		ifstream file(argv[2], ios_base::in | ios_base::binary);
		if (!file) {
			cout << "file read failed!" << endl;
		}

		//向服务器发送请求
		/*sendto(s, "lsend", 5, 0, (SOCKADDR *)&serverAddr, serveraddrLen);
		timeOutId = timeSetEvent(timeOut, 1, (LPTIMECALLBACK)resendSendRequire, DWORD(1), TIME_PERIODIC);
		if (recvfrom(s, (char *)&dataPortNumber, 16, 0, (SOCKADDR *)&serverAddr, &serveraddrLen) != -1) {
			timeKillEvent(timeOutId);
			cout << "连接服务器成功！"  << endl;
		}*/

		while (1) {

		}
	}
/********************************************************************************************************/
/********************************************************************************************************/
	//下载文件
	else if (strncmp(argv[1], "lget", 4) == 0) {
		//向服务器发送请求
		char response[8];
		sendto(s, "lget", 4, 0, (SOCKADDR *)&serverAddr, serveraddrLen);
		timeOutId = timeSetEvent(timeOut, 1, (LPTIMECALLBACK)resendGetRequire, DWORD(1), TIME_PERIODIC);
		if (recvfrom(s, response, 8, 0, (SOCKADDR *)&serverAddr, &serveraddrLen) != -1) {
			timeKillEvent(timeOutId);
			if (strncmp(response, "filePath", 4) == 0) {
				cout << "连接服务器成功！" << endl;
			}
			else {
				cout << "get an incorrect response!" << endl;
				return 0;
			}
		}
		
		//向服务器发送要下载的文件路径



		





	}
/*************************************************************************************************************/
	//命令错误
	else {
		cout << "Please input: LFTP {lsend | lget} <myserver> <mylargefile>" << endl;
		return 0;
	}
/************************************************************************************************************/
	return 0;
	WSACleanup();
}
