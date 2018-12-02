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
unordered_map<SOCKET, unsigned int> socketSendBase;
//数据包结构体
struct packet{
	unsigned int seq;
	unsigned int buffDataLen;
	char buff[1024];
};
//ack包结构体,含ack和接收窗口大小
struct ackpacket {
	unsigned int ack;
	unsigned int rwnd;
};
//每一个socket线程对应一个packet数组
unordered_map<SOCKET, vector<packet>> socketPacket;
int udpRcvBuffSize = 1000 * sizeof(packet);
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

/*******************定时器回调函数********************/
void WINAPI resendFilePathRequire(UINT wTimerID, UINT msg, DWORD dwUser, DWORD dwl, DWORD dw2) {
	SOCKET s = dwUser;
	if (socketTimeOut[s].first > 60000) {
		cout << "disconnect from the client: (";
		cout << socketClientAddr[s].sin_addr.s_addr << ", " << socketClientAddr[s].sin_port << ")" << endl;
		TerminateThread(socketThread[s], 0);
		closeSocket(s);
		return;
	}
	cout << "resent filePath require to: (";
	cout << socketClientAddr[s].sin_addr.s_addr << ", " << socketClientAddr[s].sin_port << ")" << endl;
	sendto(s, "filePath", 8, 0, (SOCKADDR *)&(socketClientAddr[s]), sizeof(socketClientAddr[s]));
	socketTimeOut[s].first *= 2;
	socketTimeOut[s].second = timeSetEvent(socketTimeOut[s].first, 1, (LPTIMECALLBACK)resendFilePathRequire, DWORD(s), TIME_ONESHOT);
}

void WINAPI resendFileData(UINT wTimerID, UINT msg, DWORD dwUser, DWORD dwl, DWORD dw2) {
	SOCKET s = dwUser;
	if (socketTimeOut[s].first > 60000) {
		cout << "disconnect from the client: (";
		cout << socketClientAddr[s].sin_addr.s_addr << ", " << socketClientAddr[s].sin_port << ")" << endl;
		TerminateThread(socketThread[s], 0);
		closeSocket(s);
		return;
	}
	cout << "timeout resend seq: " << socketPacket[s][socketSendBase[s]].seq << endl;
	sendto(s, (char *)(&socketPacket[s][socketSendBase[s]]), sizeof(packet), 0, (SOCKADDR *)&(socketClientAddr[s]), sizeof(socketClientAddr[s]));
	socketTimeOut[s].first *= 2;
	socketTimeOut[s].second = timeSetEvent(socketTimeOut[s].first, 1, (LPTIMECALLBACK)resendFileData, DWORD(s), TIME_ONESHOT);
}

void WINAPI resendFIN(UINT wTimerID, UINT msg, DWORD dwUser, DWORD dwl, DWORD dw2) {
	SOCKET s = dwUser;
	if (socketTimeOut[s].first > 60000) {
		cout << "disconnect from the client: (";
		cout << socketClientAddr[s].sin_addr.s_addr << ", " << socketClientAddr[s].sin_port << ")" << endl;
		TerminateThread(socketThread[s], 0);
		closeSocket(s);
		return;
	}
	cout << "resent FIN to: (";
	cout << socketClientAddr[s].sin_addr.s_addr << ", " << socketClientAddr[s].sin_port << ")" << endl;
	sendto(s, "FIN", 3, 0, (SOCKADDR *)&(socketClientAddr[s]), sizeof(socketClientAddr[s]));
	socketTimeOut[s].first *= 2;
	socketTimeOut[s].second = timeSetEvent(socketTimeOut[s].first, 1, (LPTIMECALLBACK)resendFIN, DWORD(s), TIME_ONESHOT);
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
	int clientAddrLen = sizeof(clientAddr);
	//收到要发给客户端的文件的路径
	if (recvfrom(s, filePath, 300, 0, (SOCKADDR *)&clientAddr, &clientAddrLen) != -1) {
		timeKillEvent(socketTimeOut[s].second);
		socketTimeOut[s].first = 2000;
		//防止错误的IP/端口发来的信息
		if (clientAddr.sin_addr.s_addr != socketClientAddr[s].sin_addr.s_addr ||
			clientAddr.sin_port != socketClientAddr[s].sin_port) {
			cout << "get message from error address: (";
			cout << socketClientAddr[s].sin_addr.s_addr << ", " << socketClientAddr[s].sin_port << ")" << endl;
			cout << "disconnect client: (";
			cout << socketClientAddr[s].sin_addr.s_addr << ", " << socketClientAddr[s].sin_port << ")" << endl;
			closeSocket(s);
			return 0;
		}
	}
	//打开文件
	ifstream file(filePath, ios_base::in | ios_base::binary);
	if (!file) {
		cout << "file open failed: ("; 
		cout << socketClientAddr[s].sin_addr.s_addr << ", " << socketClientAddr[s].sin_port << ")" << endl;
		cout << "disconnect client: (";
		cout << socketClientAddr[s].sin_addr.s_addr << ", " << socketClientAddr[s].sin_port << ")" << endl;
		closeSocket(s);
		return 0;
	}

	cout << "begin sent file to: (";
	cout << socketClientAddr[s].sin_addr.s_addr << ", " << socketClientAddr[s].sin_port << ")" << endl;

	unsigned int seq = 0;
	unsigned int rwnd = 1000;
	unsigned int nextseqnum = 0;
	unsigned int redundancy = 0;
	ackpacket senderMessage;
	socketTimeOut[s].first = 10000;
	while (!file.eof()) {
		while (!file.eof() && ((nextseqnum + 1001 - socketSendBase[s]) % 1001) < rwnd) {
			if (socketSendBase[s] == nextseqnum) {
				socketTimeOut[s].second = timeSetEvent(socketTimeOut[s].first, 1, (LPTIMECALLBACK)resendFileData, DWORD(s), TIME_ONESHOT);
			}
			file.read((char*)(&(socketPacket[s][nextseqnum].buff)), 1024);
			socketPacket[s][nextseqnum].buffDataLen = file.gcount();
			if (socketPacket[s][nextseqnum].buffDataLen == 0) continue;
			socketPacket[s][nextseqnum].seq = seq;
			seq++;
			sendto(s,(char *)(&socketPacket[s][nextseqnum]), sizeof(packet), 0, (SOCKADDR *)&(socketClientAddr[s]), sizeof(socketClientAddr[s]));
			cout << "send seq: " << seq << endl;
			nextseqnum = (nextseqnum + 1) % 1001;
		}
		if (recvfrom(s, (char *)&senderMessage, sizeof(senderMessage), 0, (SOCKADDR *)&clientAddr, &clientAddrLen) != -1) {
			//防止错误的IP/端口发来的信息
			if (clientAddr.sin_addr.s_addr != socketClientAddr[s].sin_addr.s_addr ||
				clientAddr.sin_port != socketClientAddr[s].sin_port) {
				continue;
			}

			rwnd = senderMessage.rwnd;
			cout << "smallest unAcked seq: " << socketPacket[s][socketSendBase[s]].seq << " receive ack: " << senderMessage.ack << endl;
			//ack > base: ack之前的都被确认，因此base = ack
			if (senderMessage.ack > socketPacket[s][socketSendBase[s]].seq) {
				socketTimeOut[s].first = 2000;
				socketSendBase[s] = (socketSendBase[s] + senderMessage.ack - socketPacket[s][socketSendBase[s]].seq) % 1001;
				redundancy = 0;
				timeKillEvent(socketTimeOut[s].second);
				if (socketSendBase[s] != nextseqnum) {
					socketTimeOut[s].second = timeSetEvent(socketTimeOut[s].first, 1, (LPTIMECALLBACK)resendFileData, DWORD(s), TIME_ONESHOT);
				}
			}
			//ack = base: 3次冗余ack，快速重传base
			else if (senderMessage.ack == socketPacket[s][socketSendBase[s]].seq) {
				redundancy++;
				if (redundancy == 3) {
					sendto(s, (char *)(&socketPacket[s][socketSendBase[s]]), sizeof(packet), 0, (SOCKADDR *)&(socketClientAddr[s]), sizeof(socketClientAddr[s]));
					cout << "quik resend seq: " << socketPacket[s][socketSendBase[s]].seq << endl;
				}
			}
			//ack < base：base之前的包都已经被确认，因此不用响应小于base的ack
		}
	}
	file.close();
	while (socketSendBase[s] != nextseqnum) {
		if (recvfrom(s, (char *)&senderMessage, sizeof(senderMessage), 0, (SOCKADDR *)&clientAddr, &clientAddrLen) != -1) {
			//防止错误的IP/端口发来的信息
			if (clientAddr.sin_addr.s_addr != socketClientAddr[s].sin_addr.s_addr ||
				clientAddr.sin_port != socketClientAddr[s].sin_port) {
				continue;
			}

			rwnd = senderMessage.rwnd;
			cout << "smallest unAcked seq: " << socketPacket[s][socketSendBase[s]].seq << " receive ack: " << senderMessage.ack  << endl;
			//ack > base: ack之前的都被确认，因此base = ack
			if (senderMessage.ack > socketPacket[s][socketSendBase[s]].seq) {
				socketTimeOut[s].first = 2000;
				socketSendBase[s] = (socketSendBase[s] + senderMessage.ack - socketPacket[s][socketSendBase[s]].seq) % 1001;
				redundancy = 0;
				timeKillEvent(socketTimeOut[s].second);
				if (socketSendBase[s] != nextseqnum) {
					socketTimeOut[s].second = timeSetEvent(socketTimeOut[s].first, 1, (LPTIMECALLBACK)resendFileData, DWORD(s), TIME_ONESHOT);
				}
			}
			//ack = base: 3次冗余ack，快速重传base
			else if (senderMessage.ack == socketPacket[s][socketSendBase[s]].seq) {
				redundancy++;
				if (redundancy == 3) {
					sendto(s, (char *)(&socketPacket[s][socketSendBase[s]]), sizeof(packet), 0, (SOCKADDR *)&(socketClientAddr[s]), sizeof(socketClientAddr[s]));
					cout << "quik resend seq: " << socketPacket[s][socketSendBase[s]].seq << endl;
				}
			}
			//ack < base：base之前的包都已经被确认，因此不用响应小于base的ack
		}
	}
	//发送挥手报文
	sendto(s, "FIN", 3, 0, (SOCKADDR *)&(socketClientAddr[s]), sizeof(socketClientAddr[s]));
	socketTimeOut[s].second = timeSetEvent(socketTimeOut[s].first, 1, (LPTIMECALLBACK)resendFIN, DWORD(s), TIME_ONESHOT);
	while (recvfrom(s, (char *)&senderMessage, sizeof(senderMessage), 0, (SOCKADDR *)&clientAddr, &clientAddrLen) != -1) {
		if (strncmp((char *)&senderMessage, "FIN", 3) == 0) {
			timeKillEvent(socketTimeOut[s].second);
			break;
		}
	}
	//断开连接
	cout << "disconnect from the client: (";
	cout << socketClientAddr[s].sin_addr.s_addr << ", " << socketClientAddr[s].sin_port << ")" << endl;
	closeSocket(s);
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

	//用于创建socket时的bind，当port为0时，系统自动分配port
	sockaddr_in newAddr;
	newAddr.sin_family = AF_INET;
	newAddr.sin_addr.s_addr = INADDR_ANY;
	newAddr.sin_port = 0;

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
				SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
				if (bind(s, (SOCKADDR *)&newAddr, sizeof(newAddr)) == SOCKET_ERROR) {
					cout << "create socket failed for: (" << clientIP << ", " << clientPort << ")" << endl;
					continue;
				}
				cout << "create socket succeed for: (" << clientIP << ", " << clientPort << ")" << endl;
				//添加全局变量map信息
				clientSocket[clientIP][clientPort] = s;
				socketClientAddr[s] = clientAddr;
				socketTimeOut[s] = make_pair(2000, 0);
				socketSendBase[s] = 0;
				socketPacket[s] = vector<packet>(1001);
				//创建线程
				if (strncmp(require, "lsend", 5) == 0) {
					setsockopt(s, SOL_SOCKET, SO_RCVBUF, (const char *)&udpRcvBuffSize, sizeof(udpRcvBuffSize));
					socketThread[s] = CreateThread(NULL, 0, getFileFromClient, (void *)&clientSocket[clientIP][clientPort], 0, NULL);
				}
				else if (strncmp(require, "lget", 4) == 0) {
					setsockopt(s, SOL_SOCKET, SO_SNDBUF, (const char *)&udpRcvBuffSize, sizeof(udpRcvBuffSize));
					socketThread[s] = CreateThread(NULL, 0, sendFileToClient, (void *)&clientSocket[clientIP][clientPort], 0, NULL);
				}
			}
		}
	}

	return 0;
	WSACleanup();
}
