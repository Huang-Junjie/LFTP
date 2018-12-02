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
char* filePath;
//数据包结构体
struct packet {
	unsigned int seq;
	unsigned int buffDataLen;
	bool ifRcv;
	char buff[1024];
};
//ack包结构体,含ack和接收窗口大小
struct ackpacket {
	unsigned int ack;
	unsigned int rwnd;
};
packet rcvBuff[1000];
/**************************************************/


/***********************重传函数********************/
void WINAPI resendGetRequire(UINT wTimerID, UINT msg, DWORD dwUser, DWORD dwl, DWORD dw2) {
	if (timeOut > 60000) {
		cout << "connect server failed!" << endl;
		exit(0);
	}
	cout << "request timeout, resent lget request" << endl;
	sendto(s, "lget", 4, 0, (SOCKADDR *)&serverAddr, serveraddrLen);
	timeOut *= 2;
	timeOutId = timeSetEvent(timeOut, 1, (LPTIMECALLBACK)resendGetRequire, DWORD(1), TIME_ONESHOT);
}

void WINAPI resendSendRequire(UINT wTimerID, UINT msg, DWORD dwUser, DWORD dwl, DWORD dw2) {
	if (timeOut > 60000) {
		cout << "connect server failed!" << endl;
		exit(0);
	}
	cout << "request timeout, resent lsend request" << endl;
	sendto(s, "lsend", 4, 0, (SOCKADDR *)&serverAddr, serveraddrLen);
	timeOut *= 2;
	timeOutId = timeSetEvent(timeOut, 1, (LPTIMECALLBACK)resendSendRequire, DWORD(1), TIME_ONESHOT);
}

void WINAPI resendFilePath(UINT wTimerID, UINT msg, DWORD dwUser, DWORD dwl, DWORD dw2) {
	if (timeOut > 60000) {
		cout << "disconnect from the server!" << endl;
		exit(0);
	}
	cout << "timeout, resent file path" << endl;
	sendto(s, filePath, strlen(filePath) + 1, 0, (SOCKADDR *)&serverAddr, serveraddrLen);
	timeOut *= 2;
	timeOutId = timeSetEvent(timeOut, 1, (LPTIMECALLBACK)resendSendRequire, DWORD(1), TIME_ONESHOT);
}
/**************************************************/


int main(int argc, char* argv[]) {
	//检查参数
	if (argc != 4) {
		cout << "Please input: LFTP {lsend | lget} <myserver> <mylargefile>" << endl;
	}
	filePath = argv[3];

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
		timeOutId = timeSetEvent(timeOut, 1, (LPTIMECALLBACK)resendSendRequire, DWORD(1), TIME_ONESHOT);
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
		timeOutId = timeSetEvent(timeOut, 1, (LPTIMECALLBACK)resendGetRequire, DWORD(1), TIME_ONESHOT);
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
		sendto(s, filePath, strlen(filePath) + 1, 0, (SOCKADDR *)&serverAddr, serveraddrLen);
		timeOutId = timeSetEvent(timeOut, 1, (LPTIMECALLBACK)resendFilePath, DWORD(1), TIME_ONESHOT);

		//创建文件
		ofstream file(filePath, ios_base::in | ios_base::binary);
		if (!file) {
			cout << "create file failed: (" << endl;
			return 0;
		}

		//开始下载文件
		unsigned int rcvBase = 0;		//缓冲区起始下标
		unsigned int expectSeq = 0;		//期待收到的报文的seq
		unsigned int lastRcvSeq = 0;	//收到的报文的最大seq
		sockaddr_in recvfromAddr;
		packet rcvPacket;
		ackpacket ackMessage;
		while (true) {
			if (recvfrom(s, (char *)&rcvPacket, sizeof(packet), 0, (SOCKADDR *)&recvfromAddr, NULL) != -1) {
				timeKillEvent(timeOutId);
				//防止错误的IP/端口发来的信息
				if (recvfromAddr.sin_addr.s_addr != serverAddr.sin_addr.s_addr ||
					recvfromAddr.sin_port != serverAddr.sin_port) {
					continue;
				}

				//比期望序号大的失序报文，立即发送冗余ack
				if (rcvPacket.seq > expectSeq) {
					if (rcvPacket.seq > lastRcvSeq) lastRcvSeq = rcvPacket.seq;
					unsigned int index = (rcvBase + rcvPacket.seq - expectSeq) % 1000;
					memcpy((void *)&(rcvBuff[index]), (void *)&rcvPacket, sizeof(packet));
					//发送冗余ack
					ackMessage.ack = expectSeq;
					ackMessage.rwnd = 1000 - (lastRcvSeq - expectSeq + 1);
					sendto(s, (char *)&ackMessage, sizeof(ackMessage), 0, (SOCKADDR *)&serverAddr, serveraddrLen);
				}
				//比期望序号小的报文
				else if (rcvPacket.seq < expectSeq){
					ackMessage.ack = expectSeq;
					ackMessage.rwnd = 1000 - (lastRcvSeq - expectSeq + 1);
					sendto(s, (char *)&ackMessage, sizeof(ackMessage), 0, (SOCKADDR *)&serverAddr, serveraddrLen);
				}
				else {
					if (rcvPacket.seq > lastRcvSeq) lastRcvSeq = rcvPacket.seq;
					memcpy((void *)&(rcvBuff[rcvBase]), (void *)&rcvPacket, sizeof(packet));
					//写入文件
					while (rcvBuff[rcvBase].ifRcv) {
						file.write(rcvBuff[rcvBase].buff, rcvBuff[rcvBase].buffDataLen);
						rcvBuff[rcvBase].ifRcv = false;
						rcvBase = (rcvBase + 1) % 1000;
						expectSeq++;
					}


					//发送冗余ack
					ackMessage.ack = expectSeq;
					ackMessage.rwnd = 1000 - (lastRcvSeq - expectSeq + 1);
					sendto(s, (char *)&ackMessage, sizeof(ackMessage), 0, (SOCKADDR *)&serverAddr, serveraddrLen);
				}
				
				
				
				
				
				
				
			}



		}






		

		





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
