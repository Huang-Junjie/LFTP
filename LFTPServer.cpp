#include <iostream>
#include <fstream>
#include <unordered_map>
#include <WinSock2.h>
#pragma comment(lib,"Ws2_32.lib")
using namespace std;




/****************全局变量**************/
//客户端地址端口对应一个socket
unordered_map<unsigned long, unordered_map<unsigned short, SOCKET>> clientSocket; 




/**************************************/


/***************线程函数***************/
DWORD WINAPI getFileFromClient(LPVOID lpParameter) {

	return 0;
}

DWORD WINAPI sendFileToClient(LPVOID lpParameter) {
	SOCKET * s = (SOCKET *)lpParameter;
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

	//创建socket
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

	//服务器新建的socket的地址信息
	sockaddr_in newServerAddr;
	int newServerAddrLen = sizeof(newServerAddr);
	
	/**
		监听客户端的请求；
		当一个新的客户IP/PORT的请求报文到来时（第一次握手），
		为它创建一个socket，发送该socket的端口号给客户端（第二次握手），并为该客户端创建新的线程；
		客户端收到该端口号后，再给服务器监听端口号发送一次请求(lsend或lget)（第三次握手）；
		若新的线程的socket一直收不到数据，则销毁该线程及socket
	**/
	char require[5];
	unsigned short newPortNumber;
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
				cout << "create socket for: " << clientIP << ", " << clientPort << endl;
				clientSocket[clientIP][clientPort] = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
				//获取新socket的地址信息，将新socket的port发给客户端
				getsockname(clientSocket[clientIP][clientPort], (SOCKADDR*)&newServerAddr, &newServerAddrLen);
				newPortNumber = newServerAddr.sin_port;
				cout << "return socket port number for: " << clientIP << ", " << clientPort << endl;
				sendto(listenSocket, (char *)&newPortNumber, 16, 0, (SOCKADDR *)&clientAddr, clientAddrLen);
				//创建线程
				if (strncmp(require, "lsend", 5) == 0) {
					HANDLE hThread1 = CreateThread(NULL, 0, getFileFromClient, (void *)&clientSocket[clientIP][clientPort], 0, NULL);
				}
				else if (strncmp(require, "lget", 4) == 0) {
					HANDLE hThread1 = CreateThread(NULL, 0, sendFileToClient, (void *)&clientSocket[clientIP][clientPort], 0, NULL);
				}
			}
			else {
				cout << "return socket port number for: " << clientIP << ", " << clientPort << endl;
				sendto(listenSocket, (char *)&newPortNumber, 16, 0, (SOCKADDR *)&clientAddr, clientAddrLen);
			}






			

	
		}
	}

	return 0;
	WSACleanup();
}
