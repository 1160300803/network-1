#include <stdio.h>
#include <Windows.h>
#include <process.h>
#include <string>
#include <tchar.h>

#pragma comment(lib,"Ws2_32.lib")

#define MAXSIZE 65507 //发送数据报文的最大长度
#define HTTP_PORT 80 //http 服务器端口

#define CACHE_MAXSIZE 100
#define DATELENGTH 40
//Http 重要头部数据
struct HttpHeader{
	char method[4]; // POST 或者 GET，注意有些为 CONNECT，本实验暂不考虑
	char url[1024];  //  请求的 url
	char host[1024]; //  目标主机
	char cookie[1024 * 10]; //cookie
	HttpHeader(){
		ZeroMemory(this, sizeof(HttpHeader));
	}
};

BOOL InitSocket();
BOOL ParseHttpHead(char *buffer, HttpHeader * httpHeader);
BOOL ConnectToServer(SOCKET *serverSocket, char *host);
unsigned int __stdcall ProxyThread(LPVOID lpParameter);

//代理相关参数
SOCKET ProxyServer;
sockaddr_in ProxyServerAddr;
const int ProxyPort = 10240;
//禁止访问的主机，禁用网站
//char* host[3] = { "www.sougou.com", "jwts.hit.edu.cn", "weibo.com"};
//钓鱼

struct ProxyParam{
	SOCKET clientSocket;
	SOCKET serverSocket;
};

int main(int argc, _TCHAR* argv[])
{

	printf("代理服务器正在启动\n");
	printf("初始化...\n");
	if (!InitSocket()){

		printf("socket 初始化失败\n");
		return -1;
	}
	printf("代理服务器正在运行，监听端口  %d\n", ProxyPort);
	SOCKET acceptSocket = INVALID_SOCKET;
	ProxyParam *lpProxyParam;
	HANDLE hThread;
//	DWORD dwThreadID;
	//代理服务器不断监听
	sockaddr_in verAddr;
	int h = sizeof(SOCKADDR);
	while (true){
		acceptSocket = accept(ProxyServer,(SOCKADDR*)&verAddr, &h);
		lpProxyParam = new ProxyParam;
		if (lpProxyParam == NULL){
			continue;
		}
		if (strcmp("127.0.0.1", inet_ntoa(verAddr.sin_addr))){
			printf("被限制的用户访问！\n");
			continue;
		}

		lpProxyParam->clientSocket = acceptSocket;
		hThread = (HANDLE)_beginthreadex(NULL, 0,&ProxyThread, (LPVOID)lpProxyParam, 0, 0);
		CloseHandle(hThread);
		Sleep(200);
	}
	closesocket(ProxyServer);
	WSACleanup();
	return 0;
}
BOOL InitSocket(){
	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
		printf("加载 winsock 失败，错误代码为: %d\n", WSAGetLastError());
		return FALSE;
	}
	if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2)
	{
		printf("winsock 版本错误\n");
		WSACleanup();
		return FALSE;
	}
	ProxyServer = socket(AF_INET, SOCK_STREAM, 0);
	if (INVALID_SOCKET == ProxyServer)
    {
		printf("创建套接字失败，错误代码为：%d\n", WSAGetLastError());
		return FALSE;
	}
	ProxyServerAddr.sin_family = AF_INET;
	ProxyServerAddr.sin_port = htons(ProxyPort);
	ProxyServerAddr.sin_addr.S_un.S_addr = INADDR_ANY;
	if (bind(ProxyServer, (SOCKADDR*)&ProxyServerAddr, sizeof(SOCKADDR))==-1)
    {
		printf("绑定套接字失败\n");
		return FALSE;
	}
	if (listen(ProxyServer, SOMAXCONN)==-1)
    {
		printf("监听端口%d 失败", ProxyPort);
		return FALSE;
	}
	return TRUE;
}
BOOL ParseHttpHead(char *buffer, HttpHeader * httpHeader){
	char *p;
	char *ptr;
	bool change = false;
	p = strtok_s(buffer,"\r\n", &ptr);//提取第一行
	printf("%s\n", p);
	if (p[0]=='G')
	{
		memcpy(httpHeader->method, "GET", 3);
		memcpy(httpHeader->url, &p[4], strlen(p) - 13);
	}
	else if (p[0]=='P')
	{
		memcpy(httpHeader->method, "POST", 4);
		memcpy(httpHeader->url, &p[5], strlen(p) - 14);
	}
	printf("%s\n", httpHeader->url);
	p = strtok_s(NULL,"\r\n", &ptr);
	while (p){
		switch (p[0]){
		case 'H':
				memcpy(httpHeader->host, &p[6], strlen(p)-6);
                break;
		case 'C':
			if (strlen(p)>8){
				char header[8];
				ZeroMemory(header, sizeof(header));
				memcpy(header, p, 6);
				if (!strcmp(header, "Cookie")){
					memcpy(httpHeader->cookie, &p[8], strlen(p)-8);
				}
			}
			break;
		default:
			break;
		}
		p = strtok_s(NULL,"\r\n", &ptr);
	}
	return change;
}

BOOL ConnectToServer(SOCKET *serverSocket, char *host){
	sockaddr_in serverAddr;
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(HTTP_PORT);
    /*
	返回hostent结构体类型指针
	struct hostent
	{
	char    *h_name;
	char    **h_aliases;
	int     h_addrtype;
	int     h_length;
	char    **h_addr_list;
	h_addr h_addr_list
	};
	*/
	HOSTENT *hostent = gethostbyname(host);//32位ip
	if (!hostent){
		return FALSE;
	}
//	const char *inet_ntop(int af, const void *src, char *dst, socklen_t cnt);//网络字节顺序
	in_addr Inaddr=*((in_addr*)*hostent->h_addr_list);//主机ip
	serverAddr.sin_addr.s_addr = inet_addr(inet_ntoa(Inaddr));
	*serverSocket = socket(AF_INET, SOCK_STREAM, 0);
	if (*serverSocket==INVALID_SOCKET)
		return FALSE;
	if (connect(*serverSocket, (SOCKADDR *)&serverAddr, sizeof(serverAddr))==SOCKET_ERROR)
	{
		closesocket(*serverSocket);
		return FALSE;
	}
	return TRUE;
}
unsigned int __stdcall ProxyThread(LPVOID lpParameter){
	char Buffer[MAXSIZE];
	char *CacheBuffer;
	ZeroMemory(Buffer, MAXSIZE);
//	SOCKADDR_IN clientAddr;
	int length = sizeof(SOCKADDR_IN);
	int recvSize;
	int ret;
	recvSize = recv(((ProxyParam*)lpParameter)->clientSocket, Buffer, MAXSIZE, 0);
    // printf(Buffer);
	if (recvSize <= 0)
		goto error;
	HttpHeader* httpHeader = new HttpHeader();
	CacheBuffer = new char[recvSize + 1];
	ZeroMemory(CacheBuffer, recvSize + 1);
	memcpy(CacheBuffer, Buffer, recvSize);
    //printf(CacheBuffer);
	//printf("*********************************");
	ParseHttpHead(CacheBuffer, httpHeader);
	//delete CacheBuffer;
	// 网址过滤
	/*if (!strcmp(httpHeader->host, "today.hit.edu.cn"))
	{
		printf("网站过滤\n");
		goto error;
	}*/

    if(!strcmp(httpHeader->url,"http://www.hit.edu.cn/"))
    {
        printf("\n**********************\n");
        printf("钓鱼成功！\n");
        memcpy(httpHeader->host,"jwes.hit.edu.cn",22);
    }
	if (!ConnectToServer(&((ProxyParam*)lpParameter)->serverSocket, httpHeader->host))
		goto error;
	printf("代理连接主机%s成功\n", httpHeader->host);
    //将客户端发送的 HTTP 数据报文转发给目标服务器
    ret = send(((ProxyParam *)lpParameter)->serverSocket, Buffer, strlen(Buffer)+1, 0);
    //等待目标服务器返回数据
    recvSize = recv(((ProxyParam*)lpParameter)->serverSocket, Buffer, MAXSIZE, 0);
    //printf(Buffer);
    if (recvSize <= 0)
        goto error;
	//将目标服务器返回的数据直接转发给客户端
	ret = send(((ProxyParam*)lpParameter)->clientSocket, Buffer, sizeof(Buffer), 0);
error:
	printf("关闭套接字\n");
	Sleep(200);
	closesocket(((ProxyParam*)lpParameter)->clientSocket);
	closesocket(((ProxyParam*)lpParameter)->serverSocket);
	delete    lpParameter;
	_endthreadex(0);
	return 0;
}


