#include <iostream>
#include <fstream>
#include <thread>
#include <atomic>
#include <string>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <vector>
#include <csignal>
#pragma comment(lib, "ws2_32.lib")

#define PORT 8888
#define BUFFER_SIZE 1024
#define MAX_IP_LENGTH 16


// 协议类型定义（使用强类型枚举避免冲突）
enum class MainPacketType : int {
    ChatMessage = 0,
    FileTransfer = 1
};

enum class FileSubType : int {
    Start = 1,
    DataPacket = 2,
    TransferEnd = 3
};

// 统一协议头（8字节）
struct PacketHeader {
    MainPacketType mainType;  // 主协议类型（2字节）
    FileSubType subType;       // 子协议类型（2字节）
    uint32_t seqNumber;        // 序列号（4字节）
    uint16_t dataSize;         // 数据大小（2字节）
};

SOCKET sock;
std::atomic<bool> running(true);

// 函数声明
void ProcessPacket(const char* buffer, int len, const sockaddr_in& from);
DWORD WINAPI ReceiveThread(LPVOID param);
void SendMessage(const std::string& message, const std::string& targetIP);
void SendFile(const std::string& filePath, const std::string& targetIP);
void InitializeNetwork();
void Cleanup();

// 主处理函数
void ProcessPacket(const char* buffer, int len, const sockaddr_in& from) {
    if (len < sizeof(PacketHeader)) return;

    PacketHeader header;
    memcpy(&header, buffer, sizeof(header));

    char ipStr[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(from.sin_addr), ipStr, INET_ADDRSTRLEN);

    switch (header.mainType) {
    case MainPacketType::ChatMessage: {
        std::string message(buffer + sizeof(header), header.dataSize);
        std::cout << "\n[来自 " << ipStr << "]:" << message << std::endl;
        break;
    }
    case MainPacketType::FileTransfer:
        switch (header.subType) {
        case FileSubType::Start: {
            std::cout << "\n开始接收文件 [" << header.seqNumber << "]" << std::endl;
            std::ofstream outFile("received_file", std::ios::binary);
            outFile.close();
            break;
        }
        case FileSubType::DataPacket: {
            std::fstream outFile("received_file",
                std::ios::binary | std::ios::app | std::ios::out);
            outFile.write(buffer + sizeof(header), header.dataSize);
            std::cout << "\r已接收数据包: " << header.seqNumber << std::flush;
            break;
        }
        case FileSubType::TransferEnd:
            std::cout << "\n文件接收完成，共接收 " << header.seqNumber << " 个数据包" << std::endl;
            break;
        }
        break;
    }
}

// 接收线程
DWORD WINAPI ReceiveThread(LPVOID param) {
    char buffer[BUFFER_SIZE + sizeof(PacketHeader)];
    sockaddr_in fromAddr;
    int addrLen = sizeof(fromAddr);

    std::cout << "[系统] 接收线程已启动\n";
    while (running) {
        int recvLen = recvfrom(sock, buffer, sizeof(buffer), 0,
            reinterpret_cast<sockaddr*>(&fromAddr), &addrLen);

        if (recvLen > 0) {
            ProcessPacket(buffer, recvLen, fromAddr);
        }
    }
    std::cout << "[系统] 接收线程已退出\n";
    return 0;
}

// 发送聊天消息
void SendMessage(const std::string& message, const std::string& targetIP) {
    sockaddr_in destAddr{};
    destAddr.sin_family = AF_INET;
    destAddr.sin_port = htons(PORT);
    InetPtonA(AF_INET, targetIP.c_str(), &destAddr.sin_addr);

    PacketHeader header{};
    header.mainType = MainPacketType::ChatMessage;
    header.dataSize = static_cast<uint16_t>(message.size());

    std::vector<char> packet(sizeof(header) + message.size());
    memcpy(packet.data(), &header, sizeof(header));
    memcpy(packet.data() + sizeof(header), message.data(), message.size());

    sendto(sock, packet.data(), static_cast<int>(packet.size()), 0,
        reinterpret_cast<const sockaddr*>(&destAddr), sizeof(destAddr));
}

// 发送文件
void SendFile(const std::string& filePath, const std::string& targetIP) {
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file) {
        std::cerr << "错误: 无法打开文件 " << filePath << std::endl;
        return;
    }

    sockaddr_in destAddr{};
    destAddr.sin_family = AF_INET;
    destAddr.sin_port = htons(PORT);
    InetPtonA(AF_INET, targetIP.c_str(), &destAddr.sin_addr);

    // 发送开始包
    PacketHeader startHeader{};
    startHeader.mainType = MainPacketType::FileTransfer;
    startHeader.subType = FileSubType::Start;
    startHeader.seqNumber = 0;
    sendto(sock, reinterpret_cast<char*>(&startHeader), sizeof(startHeader), 0,
        reinterpret_cast<const sockaddr*>(&destAddr), sizeof(destAddr));

    // 发送数据包
    file.seekg(0);
    char buffer[BUFFER_SIZE];
    uint32_t seq = 0;
    bool errorOccurred = false;

    while (!file.eof() && !errorOccurred) {
        file.read(buffer, BUFFER_SIZE);
        const auto bytesRead = static_cast<uint16_t>(file.gcount());

        PacketHeader dataHeader{};
        dataHeader.mainType = MainPacketType::FileTransfer;
        dataHeader.subType = FileSubType::DataPacket;
        dataHeader.seqNumber = ++seq;
        dataHeader.dataSize = bytesRead;

        std::vector<char> packet(sizeof(dataHeader) + bytesRead);
        memcpy(packet.data(), &dataHeader, sizeof(dataHeader));
        memcpy(packet.data() + sizeof(dataHeader), buffer, bytesRead);

        if (sendto(sock, packet.data(), static_cast<int>(packet.size()), 0,
            reinterpret_cast<const sockaddr*>(&destAddr), sizeof(destAddr)) == SOCKET_ERROR) {
            std::cerr << "发送错误: " << WSAGetLastError() << std::endl;
            errorOccurred = true;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // 发送结束包
    if (!errorOccurred) {
        PacketHeader endHeader{};
        endHeader.mainType = MainPacketType::FileTransfer;
        endHeader.subType = FileSubType::TransferEnd;
        endHeader.seqNumber = seq;
        sendto(sock, reinterpret_cast<char*>(&endHeader), sizeof(endHeader), 0,
            reinterpret_cast<const sockaddr*>(&destAddr), sizeof(destAddr));
        std::cout << "文件发送完成，共发送 " << seq << " 个数据包" << std::endl;
    }

    file.close();
}

// 修改后的网络初始化函数
void InitializeNetwork() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup失败: " << WSAGetLastError() << std::endl;
        exit(EXIT_FAILURE);
    }

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        std::cerr << "Socket创建失败: " << WSAGetLastError() << std::endl;
        WSACleanup();
        exit(EXIT_FAILURE);
    }

    // 设置地址重用选项（新增代码）
    int optval = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
        (char*)&optval, sizeof(optval)) == SOCKET_ERROR) {
        std::cerr << "设置地址重用失败: " << WSAGetLastError() << std::endl;
    }

    sockaddr_in localAddr{};
    localAddr.sin_family = AF_INET;
    localAddr.sin_port = htons(PORT);
    localAddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock, reinterpret_cast<sockaddr*>(&localAddr), sizeof(localAddr)) == SOCKET_ERROR) {
        std::cerr << "绑定失败: " << WSAGetLastError() << std::endl;
        closesocket(sock);
        WSACleanup();
        exit(EXIT_FAILURE);
    }
}

// 清理资源
void Cleanup() {
    running = false;
    closesocket(sock);
    WSACleanup();
}
void SignalHandler(int signum) {
    std::cout << "\n捕获中断信号，正在清理..." << std::endl;
    Cleanup();
    exit(signum);
}
int main() {
    signal(SIGINT, SignalHandler);
    InitializeNetwork();
    CreateThread(nullptr, 0, ReceiveThread, nullptr, 0, nullptr);

    std::cout << "===== UDP聊天文件传输工具 =====\n"
        << "命令格式:\n"
        << "  sendmsg <IP> <消息内容>  - 发送消息\n"
        << "  sendfile <IP> <文件路径> - 发送文件\n"
        << "  exit                    - 退出程序\n";

    while (true) {
        std::cout << "\n> ";
        std::string command;
        std::getline(std::cin, command);

        if (command.empty()) continue;

        if (command == "exit") {
            break;
        }

        size_t firstSpace = command.find(' ');
        if (firstSpace == std::string::npos) {
            std::cerr << "无效命令格式!" << std::endl;
            continue;
        }

        std::string action = command.substr(0, firstSpace);
        std::string remaining = command.substr(firstSpace + 1);

        size_t secondSpace = remaining.find(' ');
        if (secondSpace == std::string::npos) {
            std::cerr << "无效参数格式!" << std::endl;
            continue;
        }

        std::string targetIP = remaining.substr(0, secondSpace);
        std::string param = remaining.substr(secondSpace + 1);

        if (action == "sendmsg") {
            SendMessage(param, targetIP);
        }
        else if (action == "sendfile") {
            std::thread(SendFile, param, targetIP).detach();
            std::cout << "开始发送文件: " << param << std::endl;
        }
        else {
            std::cerr << "未知命令: " << action << std::endl;
        }
    }

    Cleanup();
    return 0;
}