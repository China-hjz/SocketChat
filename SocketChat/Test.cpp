//#define _WINSOCK_DEPRECATED_NO_WARNINGS
//#include <iostream>
//#include <winsock2.h>
//#include <ws2tcpip.h>
//#include <string>
//#pragma comment(lib, "ws2_32.lib")
//
//#define PORT 8888  // 固定端口
//#define BUF_SIZE 1024
//
//SOCKET sock;
//bool running = true;
//
//// 接收线程
//DWORD WINAPI recvThread(LPVOID) {
//    char buf[BUF_SIZE];
//    sockaddr_in from;
//    int fromLen = sizeof(from);
//
//    while (running) {
//        int len = recvfrom(sock, buf, BUF_SIZE, 0, (sockaddr*)&from, &fromLen);
//        if (len > 0) {
//            buf[len] = '\0';
//            std::cout << "来自 " << inet_ntoa(from.sin_addr)
//                << ":" << ntohs(from.sin_port)
//                << " > " << buf << std::endl;
//        }
//    }
//    return 0;
//}
//
//int main() {
//    WSADATA ws;
//    WSAStartup(MAKEWORD(2, 2), &ws);
//
//    // 创建并绑定Socket
//    sock = socket(AF_INET, SOCK_DGRAM, 0);
//    sockaddr_in addr = { AF_INET, htons(PORT), INADDR_ANY };
//    bind(sock, (sockaddr*)&addr, sizeof(addr));
//
//    // 启动接收线程
//    CreateThread(0, 0, recvThread, 0, 0, 0);
//
//    // 发送循环
//    std::cout << "输入目标IP和消息（格式：IP 消息）\n";
//    while (true) {
//        std::cout << "> ";
//        std::string ip, msg;
//        std::cin >> ip;
//        if (ip == "exit") break;
//        std::getline(std::cin >> std::ws, msg);
//
//        sockaddr_in to = { AF_INET, htons(PORT) };
//        inet_pton(AF_INET, ip.c_str(), &to.sin_addr);
//        sendto(sock, msg.c_str(), msg.size(), 0, (sockaddr*)&to, sizeof(to));
//    }
//
//    running = false;
//    closesocket(sock);
//    WSACleanup();
//    return 0;
//}