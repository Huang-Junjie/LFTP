#include <iostream>
#include <fstream>
#include <unordered_map>
#include <WinSock2.h>
#pragma comment(lib,"Ws2_32.lib")
#pragma comment(lib, "Winmm.lib")
using namespace std;




/****************全局变量**************/
//客户端地址端口对应的socket
unordered_map<unsigned long, unordered_map<unsigned short, SOCKET>> clientSocket; 
//socket对应的客户端地址端口
unordered_map<SOCKET, sockaddr_in> socketClientAddr;
//每一个socket线程对应一个timeOut，和timeOutId
unordered_map<SOCKET, pair<unsigned int, unsigned int>> socketTimeOut;
/**************************************/

/********************关闭socket********************/
void closeSocket(SOCKET s) {
	unsigned long clientIP = socketClientAddr[s].sin_addr.s_addr;
	unsigned short clientPort = socketClientAddr[s].sin_port;
	clientSocket[clientIP].erase(clientPort);
	socketClientAddr.erase(s);
	socketTimeOut.erase(s);
}
/***************************************************/

/***********************重传函数********************/
void WINAPI resendFilePathRequire(UINT wTimerID, UINT msg, DWORD dwUser, DWORD dwl, DWORD dw2) {
	SOCKET s = dwUser;
	if (socketTimeOut[s].first > 60000) {
		cout << "disconnect client: (";
		cout << socketClientAddr[s].sin_addr.s_addr << ", " << socketClientAddr[s].sin_port << ")" << endl;
		timeKillEvent(socketTimeOut[s].second);
		closeSocket(s);
		return;
	}
	cout << "resent filePath to: (";
	cout << socketClientAddr[s].sin_addr.s_addr << ", " << socketClientAddr[s].sin_port << ")" << endl;
	socketTimeOut[s].first *= 2;
	timeKillEvent(socketTimeOut[s].second);
	socketTimeOut[s].second = timeSetEvent(socketTimeOut[s].first, 1, (LPTIMECALLBACK)resendFilePathRequire, DWORD(s), TIME_PERIODIC);
	sendto(s, "filePath", 8, 0, (SOCKADDR *)&(socketClientAddr[s]), sizeof(socketClientAddr[s]));
}
/**************************************************/

/***************线程函数***************/
DWORD WINAPI getFileFromClient(LPVOID lpParameter) {

	return 0;
}

DWORD WINAPI sendFileToClient(LPVOID lpParameter) {
	SOCKET s = *((SOCKET *)lpParameter);
	//响应客户端的请求，并且询问要发送的文件路径
	sendto(s, "filePath", 8, 0, (SOCKADDR *)&(socketClientAddr[s]), sizeof(socketClientAddr[s]));
	socketTimeOut[s].second = timeSetEvent(socketTimeOut[s].first, 1, (LPTIMECALLBACK)resendFilePathRequire, DWORD(s), TIME_PERIODIC);



	return 0;
}
/**************************************/





int main(int argc, char* argv[]) {
	//初始化Winsock相关的ddl
	WSAData wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
		cout << "WSAStartup error!" << endl;
		return 0;
	}

	//创建监听socket
	SOCKET listenSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (listenSocket == INVALID_SOCKET) {
		cout << "socket create failed!" << endl;
		return 0;
	}

	//服务器地址信息
	sockaddr_in serverAddr;
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_addr.s_addr = INADDR_ANY;
	serverAddr.sin_port = htons(8021);
	int serveraddrLen = sizeof(serverAddr);

	if (bind(listenSocket, (SOCKADDR *)&serverAddr, serveraddrLen) == SOCKET_ERROR) {
		cout << "bind listenSocket failed!";
		closesocket(listenSocket);
		return 0;
	}
	cout << "begin listen at port 8021" << endl;

	//客户端地址
	sockaddr_in clientAddr;
	int clientAddrLen = sizeof(clientAddr);
	unsigned long clientIP;
	unsigned short clientPort;
	
	/**
		监听客户端的请求；
		当listenSocket收到一个新的客户端IP/PORT的请求报文时，
		为该客户端创建一个socket，并为它创建一个新的线程;
		之后用新线程的socket给客户端发送消息;
		客户端收到响应后，使用新的socket的与服务器传输数据;
		若新的线程的socket迟迟得不到响应，则销毁该线程及socket
	**/
	char require[5];
	while (true) {
		if (recvfrom(listenSocket, require, 5, 0, (SOCKADDR *)&clientAddr, &clientAddrLen) != -1) {
			//获取客户端的IP, PORT
			clientIP = clientAddr.sin_addr.s_addr;
			clientPort = clientAddr.sin_port;
			//创建与客户端的数据传输socket
			if (clientSocket.count(clientIP) == 0 ||
				clientSocket[clientIP].count(clientPort) == 0 ||
				clientSocket[clientIP][clientPort] == 0) {
				//创建socket
				cout << "create socket for: (" << clientIP << ", " << clientPort << ")" << endl;
				SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
				//添加全局变量map信息
				clientSocket[clientIP][clientPort] = s;
				socketClientAddr[s] = clientAddr;
				socketTimeOut[s] = make_pair(2000, 0);
				//创建线程
				if (strncmp(require, "lsend", 5) == 0) {
					CreateThread(NULL, 0, getFileFromClient, (void *)&clientSocket[clientIP][clientPort], 0, NULL);
				}
				else if (strncmp(require, "lget", 4) == 0) {
					CreateThread(NULL, 0, sendFileToClient, (void *)&clientSocket[clientIP][clientPort], 0, NULL);
				}
			}
		}
	}

	return 0;
	WSACleanup();
}
