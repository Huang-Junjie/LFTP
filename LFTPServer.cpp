﻿#include <iostream>
#include <fstream>
#include <unordered_map>
#include <vector>
#include <WinSock2.h>
#include <chrono>
#pragma comment(lib,"Ws2_32.lib")
#pragma comment(lib, "Winmm.lib")
using namespace std;
using namespace chrono;



/****************全局变量**************/
SOCKET listenSocket;
sockaddr_in serverAddr; //服务器监听地址信息
sockaddr_in clientAddr; //客户端地址
int addrLen = sizeof(sockaddr_in);
sockaddr_in portzeroAddr; //用于创建线程socket时的bind，当port为0时，系统自动分配port
//客户端地址端口对应的socket
unordered_map<unsigned long, unordered_map<unsigned short, SOCKET>> clientSocket;
//socket对应的客户端地址端口
unordered_map<SOCKET, sockaddr_in> socketClientAddr;
//socket对应的线程
unordered_map<SOCKET, HANDLE> socketThread;
//每一个socket线程对应一个timeOut，和timeOutId
unordered_map<SOCKET, pair<unsigned int, unsigned int>> socketTimeOut;
unordered_map<SOCKET, unsigned int> estimatedRTT;
unordered_map<SOCKET, unsigned int> DevRTT;
//每一个socket线程对应一个sendBase
unordered_map<SOCKET, unsigned int> socketSendBase;
//数据包结构体
struct packet {
	unsigned int seq;
	unsigned int buffDataLen;
	char buff[1024];
	long long timestamp;
};
//ack包结构体
struct ackpacket {
	unsigned int ack;
	unsigned int rwnd;
	unsigned resDelay;
	long long timestamp;
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
	estimatedRTT.erase(s);
	DevRTT.erase(s);
	closesocket(s);
}

unsigned calculateTimeOut(unsigned int &eRTT, unsigned &dRTT, unsigned int sRTT) {
	eRTT = 0.875 * eRTT + 0.125 * sRTT;
	dRTT = 0.75 * dRTT + 0.25 * (eRTT > sRTT ? eRTT - sRTT : sRTT - eRTT);
	return eRTT + 4 * dRTT;
}
/***************************************************/

/*******************定时器回调函数********************/
void WINAPI closeThread(UINT wTimerID, UINT msg, DWORD dwUser, DWORD dwl, DWORD dw2) {
	SOCKET s = dwUser;
	TerminateThread(socketThread[s], 0);
	closeSocket(s);
}

void WINAPI resendFileData(UINT wTimerID, UINT msg, DWORD dwUser, DWORD dwl, DWORD dw2) {
	SOCKET s = dwUser;
	if (socketTimeOut[s].first > 30000) {
		cout << "disconnect from the client: (";
		cout << socketClientAddr[s].sin_addr.s_addr << ", " << socketClientAddr[s].sin_port << ")" << endl;
		TerminateThread(socketThread[s], 0);
		closeSocket(s);
		return;
	}
	cout << "timeout resend seq: " << socketPacket[s][socketSendBase[s]].seq << endl;
	socketPacket[s][socketSendBase[s]].timestamp = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
	sendto(s, (char *)(&socketPacket[s][socketSendBase[s]]), sizeof(packet), 0, (SOCKADDR *)&(socketClientAddr[s]), sizeof(socketClientAddr[s]));
	socketTimeOut[s].first *= 2;
	socketTimeOut[s].second = timeSetEvent(socketTimeOut[s].first, 1, (LPTIMECALLBACK)resendFileData, DWORD(s), TIME_ONESHOT);
}

void WINAPI resendFIN(UINT wTimerID, UINT msg, DWORD dwUser, DWORD dwl, DWORD dw2) {
	SOCKET s = dwUser;
	if (socketTimeOut[s].first > 10000) {
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
	//等待客户端发送文件路径, 未响应，关闭该线程，并清空对应的数据
	socketTimeOut[s].second = timeSetEvent(10 * socketTimeOut[s].first, 1, (LPTIMECALLBACK)closeThread, DWORD(s), TIME_ONESHOT);
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
			cout << "get message from error address, ";
			cout << "disconnect client: (";
			cout << socketClientAddr[s].sin_addr.s_addr << ", " << socketClientAddr[s].sin_port << ")" << endl;
			closeSocket(s);
			return 0;
		}
	}
	else {
		timeKillEvent(socketTimeOut[s].second);
		cout << "get message from error address, ";
		cout << "disconnect client: (";
		cout << socketClientAddr[s].sin_addr.s_addr << ", " << socketClientAddr[s].sin_port << ")" << endl;
		closeSocket(s);
		return 0;
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
	unsigned int rwnd = 100;
	unsigned int nextseqnum = 0;
	unsigned int redundancy = 0;
	ackpacket senderMessage;
	while (!file.eof()) {
		while (!file.eof() && ((nextseqnum + 1001 - socketSendBase[s]) % 1001) < rwnd) {
			if (socketSendBase[s] == nextseqnum) {
				socketTimeOut[s].second = timeSetEvent(2000, 1, (LPTIMECALLBACK)resendFileData, DWORD(s), TIME_ONESHOT);
			}
			file.read((char*)(&(socketPacket[s][nextseqnum].buff)), 1024);
			socketPacket[s][nextseqnum].buffDataLen = file.gcount();
			if (socketPacket[s][nextseqnum].buffDataLen == 0) continue;
			socketPacket[s][nextseqnum].seq = seq;
			socketPacket[s][nextseqnum].timestamp = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
			seq++;
			sendto(s, (char *)(&socketPacket[s][nextseqnum]), sizeof(packet), 0, (SOCKADDR *)&(socketClientAddr[s]), sizeof(socketClientAddr[s]));
			cout << "send seq: " << seq << endl;
			nextseqnum = (nextseqnum + 1) % 1001;
		}
		if (recvfrom(s, (char *)&senderMessage, sizeof(senderMessage), 0, (SOCKADDR *)&clientAddr, &clientAddrLen) != -1) {
			//防止错误的IP/端口发来的信息
			if (clientAddr.sin_addr.s_addr != socketClientAddr[s].sin_addr.s_addr ||
				clientAddr.sin_port != socketClientAddr[s].sin_port) {
				continue;
			}

			//防止重传的filePath消息
			if (senderMessage.ack > seq) continue;

			rwnd = senderMessage.rwnd;
			cout << "smallest unAcked seq: " << socketPacket[s][socketSendBase[s]].seq << " receive ack: " << senderMessage.ack << endl;
			//ack > base: ack之前的都被确认，因此base = ack
			if (senderMessage.ack > socketPacket[s][socketSendBase[s]].seq) {
				socketTimeOut[s].first = calculateTimeOut(estimatedRTT[s], DevRTT[s],
													duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count()
													- senderMessage.timestamp + senderMessage.resDelay);
				socketSendBase[s] = (socketSendBase[s] + senderMessage.ack - socketPacket[s][socketSendBase[s]].seq) % 1001;
				redundancy = 0;
				timeKillEvent(socketTimeOut[s].second);
				if (socketSendBase[s] != nextseqnum) {
					socketTimeOut[s].second = timeSetEvent(socketTimeOut[s].first, 1, (LPTIMECALLBACK)resendFileData, DWORD(s), TIME_ONESHOT);
				}
			}
			//ack = base: 3次冗余ack，快速重传base
			else if (senderMessage.ack == socketPacket[s][socketSendBase[s]].seq) {
				socketTimeOut[s].first = calculateTimeOut(estimatedRTT[s], DevRTT[s],
													duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count()
													- senderMessage.timestamp + senderMessage.resDelay);
				redundancy++;
				if (redundancy == 3) {
					socketPacket[s][socketSendBase[s]].timestamp = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
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

			//防止重传的filePath消息
			if (senderMessage.ack > seq) continue;

			rwnd = senderMessage.rwnd;
			cout << "smallest unAcked seq: " << socketPacket[s][socketSendBase[s]].seq << " receive ack: " << senderMessage.ack << endl;
			//ack > base: ack之前的都被确认，因此base = ack
			if (senderMessage.ack > socketPacket[s][socketSendBase[s]].seq) {
				socketTimeOut[s].first = calculateTimeOut(estimatedRTT[s], DevRTT[s],
					duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count()
					- senderMessage.timestamp + senderMessage.resDelay);
				socketSendBase[s] = (socketSendBase[s] + senderMessage.ack - socketPacket[s][socketSendBase[s]].seq) % 1001;
				redundancy = 0;
				timeKillEvent(socketTimeOut[s].second);
				if (socketSendBase[s] != nextseqnum) {
					socketTimeOut[s].second = timeSetEvent(socketTimeOut[s].first, 1, (LPTIMECALLBACK)resendFileData, DWORD(s), TIME_ONESHOT);
				}
			}
			//ack = base: 3次冗余ack，快速重传base
			else if (senderMessage.ack == socketPacket[s][socketSendBase[s]].seq) {
				socketTimeOut[s].first = calculateTimeOut(estimatedRTT[s], DevRTT[s],
					duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count()
					- senderMessage.timestamp + senderMessage.resDelay);
				redundancy++;
				if (redundancy == 3) {
					socketPacket[s][socketSendBase[s]].timestamp = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
					sendto(s, (char *)(&socketPacket[s][socketSendBase[s]]), sizeof(packet), 0, (SOCKADDR *)&(socketClientAddr[s]), sizeof(socketClientAddr[s]));
					cout << "quik resend seq: " << socketPacket[s][socketSendBase[s]].seq << endl;
				}
			}
			//ack < base：base之前的包都已经被确认，因此不用响应小于base的ack
		}
	}
	//发送挥手报文
	cout << "file transfer succeed for: (";
	cout << socketClientAddr[s].sin_addr.s_addr << ", " << socketClientAddr[s].sin_port << ")" << endl;
	sendto(s, "FIN", 3, 0, (SOCKADDR *)&(socketClientAddr[s]), sizeof(socketClientAddr[s]));
	socketTimeOut[s].second = timeSetEvent(socketTimeOut[s].first, 1, (LPTIMECALLBACK)resendFIN, DWORD(s), TIME_ONESHOT);
	while (recvfrom(s, (char *)&senderMessage, sizeof(senderMessage), 0, (SOCKADDR *)&clientAddr, &clientAddrLen) != -1) {
		if (strncmp((char *)&senderMessage, "FIN", 3) == 0) {
			timeKillEvent(socketTimeOut[s].second);
			break;
		}
	}
	//断开连接
	timeKillEvent(socketTimeOut[s].second);
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
	listenSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (listenSocket == INVALID_SOCKET) {
		cout << "socket create failed!" << endl;
		return 0;
	}

	serverAddr.sin_family = AF_INET;
	serverAddr.sin_addr.s_addr = INADDR_ANY;
	serverAddr.sin_port = htons(8021);

	portzeroAddr.sin_family = AF_INET;
	portzeroAddr.sin_addr.s_addr = INADDR_ANY;
	portzeroAddr.sin_port = 0;

	if (bind(listenSocket, (SOCKADDR *)&serverAddr, addrLen) == SOCKET_ERROR) {
		cout << "bind listenSocket failed!";
		closesocket(listenSocket);
		return 0;
	}
	cout << "begin listen at port 8021" << endl;

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
	struct {
		char req[8];
		long long timestamp;
	} message;

	struct {
		sockaddr_in newAddr;
		long long timestamp;
	} res;

	while (true) {
		if (recvfrom(listenSocket, (char *)&message, sizeof(message), 0, (SOCKADDR *)&clientAddr, &addrLen) != -1) {
			if (strncmp(message.req, "lsend", 5) != 0 && strncmp(message.req, "lget", 4) != 0) {
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
				if (bind(s, (SOCKADDR *)&portzeroAddr, sizeof(portzeroAddr)) == SOCKET_ERROR) {
					cout << "create socket failed for: (" << clientIP << ", " << clientPort << ")" << endl;
					closesocket(s);
					continue;
				}
				else {
					getsockname(s, (SOCKADDR *)&(res.newAddr), &addrLen);
					res.timestamp = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
					sendto(listenSocket, (char *)&res, sizeof(res), 0, (SOCKADDR *)&clientAddr, addrLen);
				}
				cout << "create socket succeed for: (" << clientIP << ", " << clientPort << ")" << endl;
				//添加全局变量map信息
				clientSocket[clientIP][clientPort] = s;
				socketClientAddr[s] = clientAddr;
				socketTimeOut[s] = make_pair(4 * (res.timestamp - message.timestamp), 0);
				estimatedRTT[s] = 0;
				DevRTT[s] = 0;
				socketSendBase[s] = 0;
				socketPacket[s] = vector<packet>(1001);
				//创建线程
				if (strncmp(message.req, "lsend", 5) == 0) {
					if (setsockopt(s, SOL_SOCKET, SO_RCVBUF, (const char *)&udpRcvBuffSize, sizeof(udpRcvBuffSize)) == SOCKET_ERROR) {
						cout << "set udp rcvbuff failed!" << endl;
					}
					socketThread[s] = CreateThread(NULL, 0, getFileFromClient, (void *)&clientSocket[clientIP][clientPort], 0, NULL);
				}
				else if (strncmp(message.req, "lget", 4) == 0) {
					if (setsockopt(s, SOL_SOCKET, SO_SNDBUF, (const char *)&udpRcvBuffSize, sizeof(udpRcvBuffSize)) == SOCKET_ERROR) {
						cout << "set udp sendbuff failed!" << endl;
					}
					socketThread[s] = CreateThread(NULL, 0, sendFileToClient, (void *)&clientSocket[clientIP][clientPort], 0, NULL);
				}
			}
			else {
				getsockname(clientSocket[clientIP][clientPort], (SOCKADDR *)&(res.newAddr), &addrLen);
				res.timestamp = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
				sendto(listenSocket, (char *)&res, sizeof(res), 0, (SOCKADDR *)&clientAddr, addrLen);
			}
		}
	}

	return 0;
	WSACleanup();
}
