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
//socket对应的线程
unordered_map<SOCKET, HANDLE> socketThread;
//每一个socket线程对应一个timeOut，和timeOutId
unordered_map<SOCKET, pair<unsigned int, unsigned int>> socketTimeOut;
//每一个socket线程对应一个sendBase
unordered_map<SOCKET, int> socketSendBase;
//数据包结构体
struct packet{
	unsigned int seq;
	unsigned int buffDataLen;
	bool ifACKed;
	char buff[1024];
};

struct ackpacket {
	unsigned int ack;
	unsigned int rwnd;
};
//每一个socket线程对应一个packet数组
unordered_map<SOCKET, vector<packet>> socketPacket;
/**************************************/

/********************关闭socket********************/
void closeSocket(SOCKET s) {
	unsigned long clientIP = socketClientAddr[s].sin_addr.s_addr;
	unsigned short clientPort = socketClientAddr[s].sin_port;
	clientSocket[clientIP].erase(clientPort);
	socketClientAddr.erase(s);
	socketThread.erase(s);
	socketTimeOut.erase(s);
	socketSendBase.erase(s);
	socketPacket.erase(s);
}
/***************************************************/

/***********************重传函数********************/
void WINAPI resendFilePathRequire(UINT wTimerID, UINT msg, DWORD dwUser, DWORD dwl, DWORD dw2) {
	SOCKET s = dwUser;
	if (socketTimeOut[s].first > 60000) {
		cout << "disconnect from the client: (";
		cout << socketClientAddr[s].sin_addr.s_addr << ", " << socketClientAddr[s].sin_port << ")" << endl;
		TerminateThread(socketThread[s], 0);
		closeSocket(s);
		return;
	}
	cout << "resent filePath to: (";
	cout << socketClientAddr[s].sin_addr.s_addr << ", " << socketClientAddr[s].sin_port << ")" << endl;
	sendto(s, "filePath", 8, 0, (SOCKADDR *)&(socketClientAddr[s]), sizeof(socketClientAddr[s]));
	socketTimeOut[s].first *= 2;
	socketTimeOut[s].second = timeSetEvent(socketTimeOut[s].first, 1, (LPTIMECALLBACK)resendFilePathRequire, DWORD(s), TIME_ONESHOT);
}

void WINAPI resendFileData(UINT wTimerID, UINT msg, DWORD dwUser, DWORD dwl, DWORD dw2) {
	SOCKET s = dwUser;
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
	socketTimeOut[s].second = timeSetEvent(socketTimeOut[s].first, 1, (LPTIMECALLBACK)resendFilePathRequire, DWORD(s), TIME_ONESHOT);
	sockaddr_in clientAddr;
	char filePath[301];
	int pathLen;
	//收到要发给客户端的文件的路径
	if ((pathLen = recvfrom(s, filePath, 300, 0, (SOCKADDR *)&clientAddr, NULL)) != -1) {
		timeKillEvent(socketTimeOut[s].second);
		socketTimeOut[s].first = 2000;
		//防止错误的IP/端口发来的错误信息
		if (clientAddr.sin_addr.s_addr != socketClientAddr[s].sin_addr.s_addr ||
			clientAddr.sin_port != socketClientAddr[s].sin_port) {
			cout << "get message from error address: (" << endl;
			cout << socketClientAddr[s].sin_addr.s_addr << ", " << socketClientAddr[s].sin_port << ")" << endl;
			closeSocket(s);
			return 0;
		}
		filePath[pathLen] = '\0';
	}
	//打开文件
	ifstream file(filePath, ios_base::in | ios_base::binary);
	if (!file) {
		cout << "file open failed: (" << endl; 
		cout << socketClientAddr[s].sin_addr.s_addr << ", " << socketClientAddr[s].sin_port << ")" << endl;
		closeSocket(s);
		return 0;
	}

	int seq;
	int rwnd = 1000;
	int nextseqnum = 0;
	ackpacket senderMessage;
	while (!file.eof()) {
		while (!file.eof() && ((nextseqnum + 1001 - socketSendBase[s]) % 1001) < rwnd) {
			file.read((char*)(&(socketPacket[s][nextseqnum].buff)), 1024);
			socketPacket[s][nextseqnum].buffDataLen = file.gcount();
			if (socketPacket[s][nextseqnum].buffDataLen == 0) continue;
			socketPacket[s][nextseqnum].seq = seq;
			socketPacket[s][nextseqnum].ifACKed = false;
			seq++;
			sendto(s,(char *)(&socketPacket[s][nextseqnum]), sizeof(packet), 0, (SOCKADDR *)&(socketClientAddr[s]), sizeof(socketClientAddr[s]));
			nextseqnum = (nextseqnum + 1) & 1001;
		}
		socketTimeOut[s].second = timeSetEvent(socketTimeOut[s].first, 1, (LPTIMECALLBACK)resendFileData, DWORD(s), TIME_ONESHOT);
		if (recvfrom(s, (char *)&senderMessage, sizeof(senderMessage), 0, (SOCKADDR *)&clientAddr, NULL) != -1) {
			//防止错误的IP/端口发来的错误信息
			if (clientAddr.sin_addr.s_addr != socketClientAddr[s].sin_addr.s_addr ||
				clientAddr.sin_port != socketClientAddr[s].sin_port) {
				continue;
			}
			if (senderMessage.ack > socketPacket[s][socketSendBase[s]].seq) {
				socketSendBase[s] = (socketSendBase[s] + senderMessage.ack - socketPacket[s][socketSendBase[s]].seq) % 1001;
			}
			for (int i = socketSendBase[s]; i != nextseqnum; i = (i + 1) % 1001) {
				if (senderMessage.seq == socketPacket[s][i].seq) {
					socketSendBase[s] = (i + 1) % 1001;
					break;
					
				}
			}
		}
	}



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
			if (strncmp(require, "lsend", 5) != 0 && strncmp(require, "lget", 4) != 0) {
				continue;
			}
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
				socketSendBase[s] = 0;
				socketPacket[s] = vector<packet>(1001);
				//创建线程
				if (strncmp(require, "lsend", 5) == 0) {
					socketThread[s] = CreateThread(NULL, 0, getFileFromClient, (void *)&clientSocket[clientIP][clientPort], 0, NULL);
				}
				else if (strncmp(require, "lget", 4) == 0) {
					socketThread[s] = CreateThread(NULL, 0, sendFileToClient, (void *)&clientSocket[clientIP][clientPort], 0, NULL);
				}
			}
		}
	}

	return 0;
	WSACleanup();
}
