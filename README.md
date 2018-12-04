#  windows LFTP



使用方式：

1.  生成客户端和服务端应用程序：
   - g++ LFTPServer.cpp -lws2_32 -lwinmm -std=c++11 -o LFTPServer.exe；
   - g++ LFTP.cpp -lws2_32 -lwinmm -std=c++11 -o LFTP.exe
2. 在服务器运行服务端程序；
3. 在客户端应用程序目录下，使用命令行输入命令：
   - ``LFTP {lsend | lget} <myserver> <mylargefile>``
4. 或者为客户端应用程序LFTP.exe添加环境变量，使用命令行输入命令：
   - ``LFTP {lsend | lget} <myserver> <mylargefile>``

