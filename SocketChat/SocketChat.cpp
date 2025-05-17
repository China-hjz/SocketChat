#include <iostream>
#include <Winsock2.h>
#include <Windows.h>
#pragma comment(lib, "wsock32.lib")
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#define ipv4 AF_INET
#define ipv6 AF_INET6
#define tcp SOCK_STREAM
#pragma comment(lib, "ws2_32")
#define udp SOCK_DGRAM

using namespace std;
class net
{
	SOCKET sock;
	sockaddr_in6 addr;
public:
	net() {}
	net(char* ip, int hton, int ipt, int type) { init(ip, hton, ipt, type); }
	int init(char* ip, int hton, int ipt, int type)
	{
		WSADATA wsaData;
		if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
		{
			printf("Failed to load Winsock.\\n");
			return (-1);
		}
		sock = socket(ipt, type, IPPROTO_TCP);
		inet_pton(ipt, ip, &addr.sin6_addr);
		addr.sin6_family = ipt;
		addr.sin6_scope_id = 10;
		addr.sin6_port = htons(hton);
		return 0;
	}
	int connects()
	{
		int len = sizeof(sockaddr_in);
		if (connect(sock, (sockaddr*)&addr, len) == SOCKET_ERROR)
		{
			cout << "connect  error:" << WSAGetLastError() << endl;
			return -1;
		}
		return 0;
	}
	void sends(const char* buf)
	{
		send(sock, buf, strlen(buf), 0);
	}
	string recvs()
	{
		char ch[1000];
		recv(sock, ch, 100, 0);
		string str;
		str = ch;
		return str;
	}
};
int main()
{
	char a[100] = "127.0.0.1";
	net s(a, 993, ipv6, tcp);
	s.connects();
	cout << s.recvs() << endl;
	return 0;
}