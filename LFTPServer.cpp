#include <iostream>
#include <fstream>
#include <unordered_map>
#include <vector>
#include <WinSock2.h>
#include <chrono>
#pragma comment(lib,"Ws2_32.lib")
#pragma comment(lib, "Winmm.lib")
using namespace std;
using namespace chrono;



/************************************************全局变量*******************************************/
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
//每一个socket线程对应一个sendBase（rcvBase也用该变量）
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
//判断接收buff的数据包是否已经读过
unordered_map<SOCKET, vector<bool>> ifVaildData;		
int udpBuffSize = 1000 * 1024;
/******************************************************************************************************/

/***********************************************函数***************************************************/
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
	ifVaildData.erase(s);
	closesocket(s);
}

unsigned int calculateTimeOut(unsigned int &eRTT, unsigned &dRTT, unsigned int sRTT) {
	eRTT = 0.875 * eRTT + 0.125 * sRTT;
	dRTT = 0.75 * dRTT + 0.25 * (eRTT > sRTT ? eRTT - sRTT : sRTT - eRTT);
	return eRTT + 4 * dRTT;
}
/*********************************************************************************************************/

/**********************************************定时器回调函数*********************************************************/
void WINAPI closeThread(UINT wTimerID, UINT msg, DWORD dwUser, DWORD dwl, DWORD dw2) {
	SOCKET s = dwUser;
	cout << "disconnect from the client: (";
	cout << socketClientAddr[s].sin_addr.s_addr << ", " << socketClientAddr[s].sin_port << ")" << endl;
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
/***********************************************************************************************************************/

/************************************************发送文件线程函数****************************************************************/
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
		cout << "open file failed: (";
		cout << socketClientAddr[s].sin_addr.s_addr << ", " << socketClientAddr[s].sin_port << ")" << endl;
		cout << "disconnect client: (";
		cout << socketClientAddr[s].sin_addr.s_addr << ", " << socketClientAddr[s].sin_port << ")" << endl;
		closeSocket(s);
		return 0;
	}

	cout << "begin senting file to: (";
	cout << socketClientAddr[s].sin_addr.s_addr << ", " << socketClientAddr[s].sin_port << ")" << endl;

	/**********************发送文件*********************/
	unsigned int seq = 0;
	unsigned int rwnd = 100;
	unsigned int nextseqnum = 0;
	unsigned int redundancy = 0;
	ackpacket ackMessage;
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
			sendto(s, (char *)(&socketPacket[s][nextseqnum]), sizeof(packet), 0, (SOCKADDR *)&(socketClientAddr[s]), sizeof(socketClientAddr[s]));
			cout << "send seq: " << seq << endl;
			nextseqnum = (nextseqnum + 1) % 1001;
			seq++;
		}
		if (recvfrom(s, (char *)&ackMessage, sizeof(ackMessage), 0, (SOCKADDR *)&clientAddr, &clientAddrLen) != -1) {
			//防止错误的IP/端口发来的信息
			if (clientAddr.sin_addr.s_addr != socketClientAddr[s].sin_addr.s_addr ||
				clientAddr.sin_port != socketClientAddr[s].sin_port) {
				continue;
			}

			//防止重传的filePath消息
			if (ackMessage.ack > seq) continue;

			rwnd = ackMessage.rwnd;
			cout << "smallest unAcked seq: " << socketPacket[s][socketSendBase[s]].seq << " receive ack: " << ackMessage.ack << endl;
			//ack > base: ack之前的都被确认，因此base = ack
			if (ackMessage.ack > socketPacket[s][socketSendBase[s]].seq) {
				socketTimeOut[s].first = calculateTimeOut(estimatedRTT[s], DevRTT[s],
													duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count()
													- ackMessage.timestamp + ackMessage.resDelay);
				socketSendBase[s] = (socketSendBase[s] + ackMessage.ack - socketPacket[s][socketSendBase[s]].seq) % 1001;
				redundancy = 0;
				timeKillEvent(socketTimeOut[s].second);
				if (socketSendBase[s] != nextseqnum) {
					socketTimeOut[s].second = timeSetEvent(socketTimeOut[s].first, 1, (LPTIMECALLBACK)resendFileData, DWORD(s), TIME_ONESHOT);
				}
			}
			//ack = base: 3次冗余ack，快速重传base
			else if (ackMessage.ack == socketPacket[s][socketSendBase[s]].seq) {
				socketTimeOut[s].first = calculateTimeOut(estimatedRTT[s], DevRTT[s],
													duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count()
													- ackMessage.timestamp + ackMessage.resDelay);
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
		if (recvfrom(s, (char *)&ackMessage, sizeof(ackMessage), 0, (SOCKADDR *)&clientAddr, &clientAddrLen) != -1) {
			//防止错误的IP/端口发来的信息
			if (clientAddr.sin_addr.s_addr != socketClientAddr[s].sin_addr.s_addr ||
				clientAddr.sin_port != socketClientAddr[s].sin_port) {
				continue;
			}

			//防止重传的filePath消息
			if (ackMessage.ack > seq) continue;

			rwnd = ackMessage.rwnd;
			cout << "smallest unAcked seq: " << socketPacket[s][socketSendBase[s]].seq << " receive ack: " << ackMessage.ack << endl;
			//ack > base: ack之前的都被确认，因此base = ack
			if (ackMessage.ack > socketPacket[s][socketSendBase[s]].seq) {
				socketTimeOut[s].first = calculateTimeOut(estimatedRTT[s], DevRTT[s],
					duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count()
					- ackMessage.timestamp + ackMessage.resDelay);
				socketSendBase[s] = (socketSendBase[s] + ackMessage.ack - socketPacket[s][socketSendBase[s]].seq) % 1001;
				redundancy = 0;
				timeKillEvent(socketTimeOut[s].second);
				if (socketSendBase[s] != nextseqnum) {
					socketTimeOut[s].second = timeSetEvent(socketTimeOut[s].first, 1, (LPTIMECALLBACK)resendFileData, DWORD(s), TIME_ONESHOT);
				}
			}
			//ack = base: 3次冗余ack，快速重传base
			else if (ackMessage.ack == socketPacket[s][socketSendBase[s]].seq) {
				socketTimeOut[s].first = calculateTimeOut(estimatedRTT[s], DevRTT[s],
					duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count()
					- ackMessage.timestamp + ackMessage.resDelay);
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
	while (recvfrom(s, (char *)&ackMessage, sizeof(ackMessage), 0, (SOCKADDR *)&clientAddr, &clientAddrLen) != -1) {
		if (strncmp((char *)&ackMessage, "FIN", 3) == 0) {
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
/**************************************************************************************************************************************/

/********************************************接受文件线程函数*************************************************************************/
DWORD WINAPI getFileFromClient(LPVOID lpParameter) {
	SOCKET s = *((SOCKET *)lpParameter);
	//等待客户端发送文件路径, 未响应，关闭该线程，并清空对应的数据
	socketTimeOut[s].second = timeSetEvent(10 * socketTimeOut[s].first, 1, (LPTIMECALLBACK)closeThread, DWORD(s), TIME_ONESHOT);
	unsigned int expectSeq = 0;		//期待收到的报文的seq
	unsigned int lastRcvSeq = 0;	//收到的报文的最大seq
	sockaddr_in clientAddr;
	int clientAddrLen = sizeof(clientAddr);
	packet rcvPacket;
	ackpacket ackMessage;
	char filePath[300];

	if (recvfrom(s, filePath, 300, 0, (SOCKADDR *)&clientAddr, &clientAddrLen) != -1) {
		timeKillEvent(socketTimeOut[s].second);
		//防止错误的IP/端口发来的信息
		if (clientAddr.sin_addr.s_addr != socketClientAddr[s].sin_addr.s_addr ||
			clientAddr.sin_port != socketClientAddr[s].sin_port) {
			cout << "get message from error address, ";
			cout << "disconnect client: (";
			cout << socketClientAddr[s].sin_addr.s_addr << ", " << socketClientAddr[s].sin_port << ")" << endl;
			closeSocket(s);
			return 0;
		}
		ackMessage.ack = 0;
		sendto(s, (char *)&ackMessage, sizeof(ackMessage), 0, (SOCKADDR *)&(socketClientAddr[s]), sizeof(socketClientAddr[s]));
	}
	else {
		timeKillEvent(socketTimeOut[s].second);
		cout << "get message from error address, ";
		cout << "disconnect client: (";
		cout << socketClientAddr[s].sin_addr.s_addr << ", " << socketClientAddr[s].sin_port << ")" << endl;
		closeSocket(s);
		return 0;
	}

	//创建文件
	ofstream file(filePath, ios_base::out | ios_base::binary);
	if (!file) {
		cout << "create file failed: (";
		cout << socketClientAddr[s].sin_addr.s_addr << ", " << socketClientAddr[s].sin_port << ")" << endl;
		cout << "disconnect client: (";
		cout << socketClientAddr[s].sin_addr.s_addr << ", " << socketClientAddr[s].sin_port << ")" << endl;
		closeSocket(s);
		return 0;
	}
	cout << "begin receiving file from: (";
	cout << socketClientAddr[s].sin_addr.s_addr << ", " << socketClientAddr[s].sin_port << ")" << endl;

	while (true) {
		if (recvfrom(s, (char *)&rcvPacket, sizeof(packet), 0, (SOCKADDR *)&clientAddr, &clientAddrLen) != -1) {
			//防止错误的IP/端口发来的信息
			if (clientAddr.sin_addr.s_addr != socketClientAddr[s].sin_addr.s_addr ||
				clientAddr.sin_port != socketClientAddr[s].sin_port) {
				continue;
			}

			timeKillEvent(socketTimeOut[s].second);

			//判断是不是挥手报文
			if (strncmp((char *)&rcvPacket, "FIN", 3) == 0) {
				file.close();
				cout << "reveive file succeed: (";
				cout << socketClientAddr[s].sin_addr.s_addr << ", " << socketClientAddr[s].sin_port << ")" << endl;
				cout << "disconnect client: (";
				cout << socketClientAddr[s].sin_addr.s_addr << ", " << socketClientAddr[s].sin_port << ")" << endl;
				closeSocket(s);
				return 0;
			}

			//若一分钟未收到数据，则断开连接
			socketTimeOut[s].second = timeSetEvent(60000, 1, (LPTIMECALLBACK)closeThread, DWORD(1), TIME_ONESHOT);

			//比期望序号大的失序报文，立即发送冗余ack
			if (rcvPacket.seq > expectSeq) {
				if (rcvPacket.seq > lastRcvSeq) lastRcvSeq = rcvPacket.seq;
				unsigned int index = (socketSendBase[s] + rcvPacket.seq - expectSeq) % 1000;
				memcpy((void *)&(socketPacket[s][index]), (void *)&rcvPacket, sizeof(packet));
				ifVaildData[s][index] == true;
				//发送冗余ack
				ackMessage.resDelay = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count() - rcvPacket.timestamp;
				ackMessage.timestamp = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
				ackMessage.ack = expectSeq;
				ackMessage.rwnd = 1000 - (lastRcvSeq + 1 - expectSeq);
				sendto(s, (char *)&ackMessage, sizeof(ackMessage), 0, (SOCKADDR *)&socketClientAddr[s], sizeof(socketClientAddr[s]));
			}
			//比期望序号小的报文
			else if (rcvPacket.seq < expectSeq) {
				ackMessage.resDelay = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count() - rcvPacket.timestamp;
				ackMessage.timestamp = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
				ackMessage.ack = expectSeq;
				ackMessage.rwnd = 1000 - (lastRcvSeq + 1 - expectSeq);
				sendto(s, (char *)&ackMessage, sizeof(ackMessage), 0, (SOCKADDR *)&socketClientAddr[s], sizeof(socketClientAddr[s]));
			}
			else {
				if (rcvPacket.seq > lastRcvSeq) lastRcvSeq = rcvPacket.seq;
				memcpy((void *)&(socketPacket[s][socketSendBase[s]]), (void *)&rcvPacket, sizeof(packet));
				ifVaildData[s][socketSendBase[s]] = true;
				//写入文件
				while (ifVaildData[s][socketSendBase[s]]) {
					file.write(socketPacket[s][socketSendBase[s]].buff, socketPacket[s][socketSendBase[s]].buffDataLen);
					ifVaildData[s][socketSendBase[s]] = false;
					socketSendBase[s] = (socketSendBase[s] + 1) % 1000;
					expectSeq++;
				}
				//发送ack
				ackMessage.resDelay = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count() - rcvPacket.timestamp;
				ackMessage.timestamp = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
				ackMessage.ack = expectSeq;
				ackMessage.rwnd = 1000 - (lastRcvSeq + 1 - expectSeq);
				sendto(s, (char *)&ackMessage, sizeof(ackMessage), 0, (SOCKADDR *)&socketClientAddr[s], sizeof(socketClientAddr[s]));
			}
		}
	}
	return 0;
}
/*************************************************************************************************************************************/


int main(int argc, char* argv[]) {
/***************************************初始化************************************************/
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
/******************************************************************************************************/


	
/***************************************************************************************************************
		监听客户端的请求；
		当listenSocket收到一个新的客户端IP/PORT的请求报文时，为该客户端创建一个socket，并为它创建一个新的线程;
		listenSocket再将为该客户端创建的socket的信息发给客户端，客户端与服务器新线程的socket进行数据传输;
		若新的线程的socket超过一段时间未得到响应，则销毁该线程及socket及对应变量
**************************************************************************************************************/
	unsigned long clientIP;
	unsigned short clientPort;
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
				estimatedRTT[s] = 2 * (res.timestamp - message.timestamp);
				DevRTT[s] = 0;
				socketSendBase[s] = 0;
				//创建线程
				if (strncmp(message.req, "lsend", 5) == 0) {
					socketPacket[s] = vector<packet>(1000);
					ifVaildData[s] = vector<bool>(1000, false);
					if (setsockopt(s, SOL_SOCKET, SO_RCVBUF, (const char *)&udpBuffSize, sizeof(udpBuffSize)) == SOCKET_ERROR) {
						cout << "set udp rcvbuff failed!" << endl;
					}
					socketThread[s] = CreateThread(NULL, 0, getFileFromClient, (void *)&clientSocket[clientIP][clientPort], 0, NULL);
				}
				else if (strncmp(message.req, "lget", 4) == 0) {
					socketPacket[s] = vector<packet>(1001);
					if (setsockopt(s, SOL_SOCKET, SO_SNDBUF, (const char *)&udpBuffSize, sizeof(udpBuffSize)) == SOCKET_ERROR) {
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
/********************************************************************************************************************/
	return 0;
	WSACleanup();
}
