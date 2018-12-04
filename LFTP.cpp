#include <iostream>
#include <fstream>
#include <WinSock2.h>
#include <Windows.h>
#include <chrono>
#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Winmm.lib")
using namespace std;
using namespace chrono;


/**********************************全局变量****************************/
unsigned int timeOut = 2000;		//初始时timeOut定为2s
unsigned int timeOutId;
sockaddr_in serverListenAddr;	
sockaddr_in serverDataAddr;
int serveraddrLen;
SOCKET s;
char * filePath;
char * serverIp;
unsigned int estimatedRTT;
unsigned int DevRTT;
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
packet packetBuff[1001];
bool ifVaildData[1000] = { false };
int udpBuffSize = 1000 * sizeof(packet);
struct {
	char req[8];
	long long timestamp;
} message;

struct {
	sockaddr_in addr;
	long long timestamp;
} res;
/****上传文件时的变量*****/
unsigned int seq = 0;			//序号
unsigned int rwnd = 100;		//接受窗口
float cwnd = 1;					//拥塞窗口
int ssthresh = 64;				//阈值
unsigned int sendbase;			//第一个未确认的数据包在缓冲区的下标
unsigned int nextseqnum = 0;	//下一个将文件读入缓冲区的下标
unsigned int redundancy = 0;	//冗余ack计数
ackpacket ackMessage;			//接收到的ack信息
/**********************************************************************/

/********************************************函数*********************************************************/
unsigned int calculateTimeOut(unsigned int &eRTT, unsigned &dRTT, unsigned int sRTT) {
	eRTT = 0.875 * eRTT + 0.125 * sRTT;
	dRTT = 0.75 * dRTT + 0.25 * (eRTT > sRTT ? eRTT - sRTT : sRTT - eRTT);
	return eRTT + 4 * dRTT;
}
/********************************************************************************************************/

/************************************定时器回调函数*******************************************************/
void WINAPI resendGetRequire(UINT wTimerID, UINT msg, DWORD dwUser, DWORD dwl, DWORD dw2) {
	if (timeOut > 30000) {
		cout << "connect server failed!" << endl;
		exit(0);
	}
	cout << "request timeout, resent lget request" << endl;
	struct {
		char req[8];
		unsigned int timestamp;
	} message;
	memcpy((char *)&message.req, "lget", 4);
	message.timestamp = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
	sendto(s, (char *)&message, sizeof(message), 0, (SOCKADDR *)&serverListenAddr, serveraddrLen);
	timeOut *= 2;
	timeOutId = timeSetEvent(timeOut, 1, (LPTIMECALLBACK)resendGetRequire, DWORD(1), TIME_ONESHOT);
}

void WINAPI resendSendRequire(UINT wTimerID, UINT msg, DWORD dwUser, DWORD dwl, DWORD dw2) {
	if (timeOut > 30000) {
		cout << "connect server failed!" << endl;
		exit(0);
	}
	cout << "request timeout, resent lsend request" << endl;
	sendto(s, "lsend", 4, 0, (SOCKADDR *)&serverListenAddr, serveraddrLen);
	timeOut *= 2;
	timeOutId = timeSetEvent(timeOut, 1, (LPTIMECALLBACK)resendSendRequire, DWORD(1), TIME_ONESHOT);
}

void WINAPI resendFilePath(UINT wTimerID, UINT msg, DWORD dwUser, DWORD dwl, DWORD dw2) {
	if (timeOut > 30000) {
		cout << "disconnect from the server!" << endl;
		exit(0);
	}
	cout << "timeout, resent file path" << endl;
	sendto(s, filePath, strlen(filePath) + 1, 0, (SOCKADDR *)&serverDataAddr, serveraddrLen);
	timeOut *= 2;
	timeOutId = timeSetEvent(timeOut, 1, (LPTIMECALLBACK)resendFilePath, DWORD(1), TIME_ONESHOT);
}

void WINAPI disconnect(UINT wTimerID, UINT msg, DWORD dwUser, DWORD dwl, DWORD dw2) {
	cout << "file transfer abort, disconnect from the server!" << endl;
	sendto(s, "FIN", 3, 0, (SOCKADDR *)&serverDataAddr, serveraddrLen);
	exit(0);
}

void WINAPI resendFileData(UINT wTimerID, UINT msg, DWORD dwUser, DWORD dwl, DWORD dw2) {
	SOCKET s = dwUser;
	if (timeOut > 30000) {
		cout << "disconnect from server";
		exit(0);
	}
	cout << "timeout resend seq: " << packetBuff[sendbase].seq << endl;
	packetBuff[sendbase].timestamp = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
	sendto(s, (char *)(&packetBuff[sendbase]), sizeof(packet), 0, (SOCKADDR *)&(serverDataAddr), sizeof(serverDataAddr));
	ssthresh = cwnd / 2;
	cwnd = 1;
	redundancy = 0;
	timeOut *= 2;
	timeOutId = timeSetEvent(timeOut, 1, (LPTIMECALLBACK)resendFileData, DWORD(s), TIME_ONESHOT);
}
/**************************************************************************************************/


int main(int argc, char* argv[]) {
/*************************初始化各变量***************************************************/
	//检查参数
	if (argc != 4) {
		cout << "Please input: LFTP {lsend | lget} <myserver> <mylargefile>" << endl;
		return 0;
	}
	filePath = argv[3];
	serverIp = argv[2];

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
	serverListenAddr.sin_family = AF_INET;
	serverListenAddr.sin_addr.s_addr = inet_addr(argv[2]);
	serverListenAddr.sin_port = htons(8021);
	serveraddrLen = sizeof(serverListenAddr);

	//用于客户端socket的bind，当port为0时，系统自动分配port
	sockaddr_in clientAddr;
	clientAddr.sin_family = AF_INET;
	clientAddr.sin_addr.s_addr = INADDR_ANY;
	clientAddr.sin_port = 0;

	//创建socket
	s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	//修该udp缓冲区大小
	if (strncmp(argv[1], "lget", 4) == 0) {
		setsockopt(s, SOL_SOCKET, SO_RCVBUF, (const char *)&udpBuffSize, sizeof(udpBuffSize));
	}
	else if (strncmp(argv[1], "lsend", 5) == 0) {
		setsockopt(s, SOL_SOCKET, SO_SNDBUF, (const char *)&udpBuffSize, sizeof(udpBuffSize));
	}
	if (bind(s, (SOCKADDR *)&clientAddr, sizeof(clientAddr)) == SOCKET_ERROR) {
		cout << "bind socket failed!";
		closesocket(s);
		return 0;
	}
/*************************************************************************************************/

/******************************************上传文件*************************************************/
	if (strncmp(argv[1], "lsend", 5) == 0) {
		//向服务器发送请求
		memcpy((char *)&message.req, "lsend", 5);
		message.timestamp = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
		sendto(s, (char *)&message, sizeof(message), 0, (SOCKADDR *)&serverListenAddr, serveraddrLen);
		timeOutId = timeSetEvent(timeOut, 1, (LPTIMECALLBACK)resendGetRequire, DWORD(1), TIME_ONESHOT);

		//获得服务器为客户端创建的socket地址，并发送要上传的文件名
		if (recvfrom(s, (char *)&res, sizeof(res), 0, NULL, NULL) != -1) {
			cout << "connect server succeed!" << endl;
			timeKillEvent(timeOutId);
			memcpy((char *)&serverDataAddr, (char *)&res.addr, sizeof(serverDataAddr));
			serverDataAddr.sin_addr.s_addr = inet_addr(serverIp);
			estimatedRTT = 2 * (duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count() - res.timestamp);
			timeOut = 2 * estimatedRTT;
			sendto(s, filePath, strlen(filePath) + 1, 0, (SOCKADDR *)&serverDataAddr, sizeof(serverDataAddr));
			timeOutId = timeSetEvent(timeOut, 1, (LPTIMECALLBACK)resendFilePath, DWORD(1), TIME_ONESHOT);
		}
		else {
			cout << "connect server failed!" << endl;
			return 0;
		}

		//创建文件
		ifstream file(filePath, ios_base::in | ios_base::binary);
		if (!file) {
			cout << "open file failed: " << endl;
			return 0;
		}
		cout << "begin uploading file to server" << endl;

		sockaddr_in rcvDataAddr;
		if (recvfrom(s, (char *)&ackMessage, sizeof(ackMessage), 0, (SOCKADDR *)&rcvDataAddr, &serveraddrLen) != -1) {
			if (rcvDataAddr.sin_addr.s_addr != serverDataAddr.sin_addr.s_addr ||
				rcvDataAddr.sin_port != serverDataAddr.sin_port) {
				timeKillEvent(timeOutId);
				cout << "get message from error address,  disconnect server!" << endl;
				return 0;
			}
		}

		/*******************开始上传文件************************/
		while (!file.eof()) {
			while (!file.eof() && ((nextseqnum + 1001 - sendbase) % 1001) < min(rwnd, cwnd)) {
				if (sendbase == nextseqnum) {
					timeOutId = timeSetEvent(2000, 1, (LPTIMECALLBACK)resendFileData, DWORD(s), TIME_ONESHOT);
				}
				file.read((char*)(&(packetBuff[nextseqnum].buff)), 1024);
				packetBuff[nextseqnum].buffDataLen = file.gcount();
				if (packetBuff[nextseqnum].buffDataLen == 0) continue;
				packetBuff[nextseqnum].seq = seq;
				packetBuff[nextseqnum].timestamp = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
				sendto(s, (char *)(&packetBuff[nextseqnum]), sizeof(packet), 0, (SOCKADDR *)&(serverDataAddr), sizeof(serverDataAddr));
				cout << "send seq: " << seq << endl;
				nextseqnum = (nextseqnum + 1) % 1001;
				seq++;
			}
			if (recvfrom(s, (char *)&ackMessage, sizeof(ackMessage), 0, (SOCKADDR *)&rcvDataAddr, &serveraddrLen) != -1) {
				//防止错误的IP/端口发来的信息
				if (rcvDataAddr.sin_addr.s_addr != serverDataAddr.sin_addr.s_addr ||
					rcvDataAddr.sin_port != serverDataAddr.sin_port) {
					continue;
				}

				//防止重传的filePath消息
				if (ackMessage.ack > seq) continue;

				rwnd = ackMessage.rwnd;
				cout << "smallest unAcked seq: " << packetBuff[sendbase].seq << " receive ack: " << ackMessage.ack << endl;
				//ack > base: ack之前的都被确认，因此base = ack
				if (ackMessage.ack > packetBuff[sendbase].seq) {
					timeOut = calculateTimeOut(estimatedRTT, DevRTT,
												duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count()
												- ackMessage.timestamp + ackMessage.resDelay);
					cout << "timeout = " << timeOut << "ms" << endl;
					if (cwnd >= ssthresh) cwnd = cwnd + 1.0 / cwnd;
					else cwnd++;
					if (redundancy >= 3) cwnd = ssthresh;
					redundancy = 0;
					sendbase = (sendbase + ackMessage.ack - packetBuff[sendbase].seq) % 1001;
					timeKillEvent(timeOutId);
					if (sendbase != nextseqnum) {
						timeOutId = timeSetEvent(timeOut, 1, (LPTIMECALLBACK)resendFileData, DWORD(s), TIME_ONESHOT);
					}
				}
				//ack = base: 3次冗余ack，快速重传base
				else if (ackMessage.ack == packetBuff[sendbase].seq) {
					timeOut = calculateTimeOut(estimatedRTT, DevRTT,
												duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count()
												- ackMessage.timestamp + ackMessage.resDelay);
					redundancy++;
					if (redundancy == 3) {
						packetBuff[sendbase].timestamp = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
						sendto(s, (char *)(&packetBuff[sendbase]), sizeof(packet), 0, (SOCKADDR *)&(serverDataAddr), sizeof(serverDataAddr));
						cout << "quik resend seq: " << packetBuff[sendbase].seq << endl;
						ssthresh = cwnd / 2;
						cwnd += 3;
					}
					else if (redundancy > 3) {
						cwnd++;
					}
				}
				//ack < base：base之前的包都已经被确认，因此不用响应小于base的ack
			}
		}
		file.close();
		while (sendbase != nextseqnum) {
			if (recvfrom(s, (char *)&ackMessage, sizeof(ackMessage), 0, (SOCKADDR *)&rcvDataAddr, &serveraddrLen) != -1) {
				//防止错误的IP/端口发来的信息
				if (rcvDataAddr.sin_addr.s_addr != serverDataAddr.sin_addr.s_addr ||
					rcvDataAddr.sin_port != serverDataAddr.sin_port) {
					continue;
				}

				//防止重传的filePath消息
				if (ackMessage.ack > seq) continue;

				rwnd = ackMessage.rwnd;
				cout << "smallest unAcked seq: " << packetBuff[sendbase].seq << " receive ack: " << ackMessage.ack << endl;
				//ack > base: ack之前的都被确认，因此base = ack
				if (ackMessage.ack > packetBuff[sendbase].seq) {
					timeOut = calculateTimeOut(estimatedRTT, DevRTT,
						duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count()
						- ackMessage.timestamp + ackMessage.resDelay);
					cout << "timeout = " << timeOut << "ms" << endl;
					if (cwnd >= ssthresh) cwnd = cwnd + 1.0 / cwnd;
					else cwnd++;
					if (redundancy >= 3) cwnd = ssthresh;
					redundancy = 0;
					sendbase = (sendbase + ackMessage.ack - packetBuff[sendbase].seq) % 1001;
					timeKillEvent(timeOutId);
					if (sendbase != nextseqnum) {
						timeOutId = timeSetEvent(timeOut, 1, (LPTIMECALLBACK)resendFileData, DWORD(s), TIME_ONESHOT);
					}
				}
				//ack = base: 3次冗余ack，快速重传base
				else if (ackMessage.ack == packetBuff[sendbase].seq) {
					timeOut = calculateTimeOut(estimatedRTT, DevRTT,
						duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count()
						- ackMessage.timestamp + ackMessage.resDelay);
					redundancy++;
					if (redundancy == 3) {
						packetBuff[sendbase].timestamp = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
						sendto(s, (char *)(&packetBuff[sendbase]), sizeof(packet), 0, (SOCKADDR *)&(serverDataAddr), sizeof(serverDataAddr));
						cout << "quik resend seq: " << packetBuff[sendbase].seq << endl;
						ssthresh = cwnd / 2;
						cwnd += 3;
					}
					else if (redundancy > 3) {
						cwnd++;
					}
				}
				//ack < base：base之前的包都已经被确认，因此不用响应小于base的ack
			}
		}

		//发送挥手报文
		sendto(s, "FIN", 3, 0, (SOCKADDR *)&(serverDataAddr), sizeof(serverDataAddr));
		cout << "upload file succeed!";
		return 0;
	}
/********************************************************************************************************/


/******************************************下载文件*******************************************************/
	else if (strncmp(argv[1], "lget", 4) == 0) {
		//向服务器发送请求
		memcpy((char *)&message.req, "lget", 4);
		message.timestamp = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
		sendto(s, (char *)&message, sizeof(message), 0, (SOCKADDR *)&serverListenAddr, serveraddrLen);
		timeOutId = timeSetEvent(timeOut, 1, (LPTIMECALLBACK)resendGetRequire, DWORD(1), TIME_ONESHOT);

		//获得服务器为客户端创建的socket地址，向该socket发送要下载的文件路径
		if (recvfrom(s, (char *)&res, sizeof(res), 0, NULL, NULL) != -1) {
			cout << "connect server succeed!" << endl;
			timeKillEvent(timeOutId);
			memcpy((char *)&serverDataAddr, (char *)&res.addr, sizeof(serverDataAddr));
			serverDataAddr.sin_addr.s_addr = inet_addr(serverIp);
			sendto(s, filePath, strlen(filePath) + 1, 0, (SOCKADDR *)&serverDataAddr, sizeof(serverDataAddr));
			timeOut = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count() - res.timestamp;
			timeOut *= 4;
			timeOutId = timeSetEvent(timeOut, 1, (LPTIMECALLBACK)resendFilePath, DWORD(1), TIME_ONESHOT);
		}
		else {
			cout << "connect server failed!" << endl;
			return 0;
		}
	
		//创建文件
		ofstream file(filePath, ios_base::out | ios_base::binary);
		if (!file) {
			cout << "create file failed: " << endl;
			return 0;
		}

		/*******************开始下载文件*******************/
		unsigned int rcvBase = 0;		//缓冲区起始下标
		unsigned int expectSeq = 0;		//期待收到的报文的seq
		unsigned int lastRcvSeq = 0;	//收到的报文的最大seq
		unsigned int rcvBytes = 0;
		sockaddr_in recvfromAddr;
		packet rcvPacket;
		ackpacket ackMessage;
		
		while (true) {
			if (recvfrom(s, (char *)&rcvPacket, sizeof(packet), 0, (SOCKADDR *)&recvfromAddr, &serveraddrLen) != -1) {
				//防止错误的IP/端口发来的信息
				if (recvfromAddr.sin_addr.s_addr != serverDataAddr.sin_addr.s_addr ||
					recvfromAddr.sin_port != serverDataAddr.sin_port) {
					continue;
				}

				timeKillEvent(timeOutId);

				//判断是不是挥手报文
				if (strncmp((char *)&rcvPacket, "FIN", 3) == 0) {
					file.close();
					cout << "download file succeed!" << endl;
					sendto(s, "FIN", 3, 0, (SOCKADDR *)&serverDataAddr, serveraddrLen);
					return 0;
				}

				//若两分钟未收到数据，则断开连接
				timeOutId = timeSetEvent(120000, 1, (LPTIMECALLBACK)disconnect, DWORD(1), TIME_ONESHOT);
				cout << "-----------------------------------------" << endl;
				cout << "receive seq: " << rcvPacket.seq << endl;
				//比期望序号大的失序报文，立即发送冗余ack
				if (rcvPacket.seq > expectSeq) {
					if (rcvPacket.seq > lastRcvSeq) lastRcvSeq = rcvPacket.seq;
					unsigned int index = (rcvBase + rcvPacket.seq - expectSeq) % 1000;
					memcpy((void *)&(packetBuff[index]), (void *)&rcvPacket, sizeof(packet));
					ifVaildData[index] == true;
					//发送冗余ack
					ackMessage.resDelay = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count() - rcvPacket.timestamp;
					ackMessage.timestamp = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
					ackMessage.ack = expectSeq;
					ackMessage.rwnd = 1000 - (lastRcvSeq + 1 - expectSeq);
					sendto(s, (char *)&ackMessage, sizeof(ackMessage), 0, (SOCKADDR *)&serverDataAddr, serveraddrLen);
				}
				//比期望序号小的报文
				else if (rcvPacket.seq < expectSeq){
					ackMessage.resDelay = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count() - rcvPacket.timestamp;
					ackMessage.timestamp = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
					ackMessage.ack = expectSeq;
					ackMessage.rwnd = 1000 - (lastRcvSeq + 1 - expectSeq );
					sendto(s, (char *)&ackMessage, sizeof(ackMessage), 0, (SOCKADDR *)&serverDataAddr, serveraddrLen);
				}
				else {
					if (rcvPacket.seq > lastRcvSeq) lastRcvSeq = rcvPacket.seq;
					memcpy((void *)&(packetBuff[rcvBase]), (void *)&rcvPacket, sizeof(packet));
					ifVaildData[rcvBase] = true;
					//写入文件
					while (ifVaildData[rcvBase]) {
						file.write(packetBuff[rcvBase].buff, packetBuff[rcvBase].buffDataLen);
						ifVaildData[rcvBase] = false;
						rcvBytes += packetBuff[rcvBase].buffDataLen;
						rcvBase = (rcvBase + 1) % 1000;
						expectSeq++;
					}
					cout << rcvBytes << "bytes have been received so far" << endl;
					//发送ack
					ackMessage.resDelay = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count() - rcvPacket.timestamp;
					ackMessage.timestamp = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
					ackMessage.ack = expectSeq;
					ackMessage.rwnd = 1000 - (lastRcvSeq + 1 - expectSeq );
					sendto(s, (char *)&ackMessage, sizeof(ackMessage), 0, (SOCKADDR *)&serverDataAddr, serveraddrLen);
				}
				cout << "send ack: " << ackMessage.ack << endl;
				cout << "-----------------------------------------" << endl;
			}
		}
	}

/*************************************************************************************************************/
	cout << "Please input: LFTP {lsend | lget} <myserver> <mylargefile>" << endl;
	return 0;
	WSACleanup();
}
