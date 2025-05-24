#include <iostream>
#include <fstream>
#include <thread>
#include <atomic>
#include <string>
#include <vector>
#include <map>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

#define PORT 8888
#define BUFFER_SIZE 1024
#define CONTACTS_FILE "contacts.dat"
#define CHATLOG_DIR "chatlogs"

// 协议类型定义
enum class MainPacketType : int { ChatMessage = 0, FileTransfer = 1 };
enum class FileSubType : int { Start = 1, DataPacket = 2, TransferEnd = 3 };

#pragma pack(push, 1)
struct PacketHeader {
    MainPacketType mainType;
    FileSubType subType;
    uint32_t seqNumber;
    uint16_t dataSize;
    char senderName[32];
};
#pragma pack(pop)

SOCKET sock;
std::atomic<bool> running(true);
std::string myNickname;
std::map<std::string, std::string> contacts; // IP -> Nickname
std::string currentChatIP;

// 文件操作辅助函数
void CreateDirectoryIfNotExists(const char* path) {
    if (!CreateDirectoryA(path, NULL) ){
        if (GetLastError() != ERROR_ALREADY_EXISTS)
            std::cerr << "无法创建目录: " << path << std::endl;
    }
}

void SaveChatLog(const std::string& ip, const std::string& message) {
    CreateDirectoryIfNotExists(CHATLOG_DIR);
    std::ofstream logFile(CHATLOG_DIR + ("\\" + ip + ".log"), std::ios::app);
    if (logFile) {
        logFile << message << std::endl;
    }
}

void LoadContacts() {
    std::ifstream file(CONTACTS_FILE);
    std::string ip, name;
    while (file >> ip >> name) {
        contacts[ip] = name;
    }
}

void SaveContacts() {
    std::ofstream file(CONTACTS_FILE);
    for (const auto& [ip, name] : contacts) {
        file << ip << " " << name << std::endl;
    }
}

// 网络功能
void InitializeNetwork() {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char*)&reuse, sizeof(reuse));

    sockaddr_in localAddr{ AF_INET, htons(PORT), INADDR_ANY };
    bind(sock, (sockaddr*)&localAddr, sizeof(localAddr));
}

DWORD WINAPI ReceiveThread(LPVOID) {
    char buffer[BUFFER_SIZE + sizeof(PacketHeader)];
    sockaddr_in fromAddr;
    int addrLen = sizeof(fromAddr);

    while (running) {
        int len = recvfrom(sock, buffer, sizeof(buffer), 0, (sockaddr*)&fromAddr, &addrLen);
        if (len <= 0) continue;

        PacketHeader header;
        memcpy(&header, buffer, sizeof(header));

        char ipStr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &fromAddr.sin_addr, ipStr, INET_ADDRSTRLEN);

        std::string message(buffer + sizeof(header), header.dataSize);
        std::string logEntry = "[" + std::string(header.senderName) + "]: " + message;

        if (header.mainType == MainPacketType::ChatMessage) {
            std::cout << "\n" << logEntry << std::endl;
            SaveChatLog(ipStr, logEntry);
        }
    }
    return 0;
}

void SendMessage(const std::string& message, const std::string& targetIP) {
    sockaddr_in destAddr{ AF_INET, htons(PORT) };
    InetPtonA(AF_INET, targetIP.c_str(), &destAddr.sin_addr);

    PacketHeader header{};
    header.mainType = MainPacketType::ChatMessage;
    header.dataSize = static_cast<uint16_t>(message.size());
    strncpy_s(header.senderName, myNickname.c_str(), sizeof(header.senderName));

    std::vector<char> packet(sizeof(header) + message.size());
    memcpy(packet.data(), &header, sizeof(header));
    memcpy(packet.data() + sizeof(header), message.c_str(), message.size());

    sendto(sock, packet.data(), static_cast<int>(packet.size()), 0,
        (sockaddr*)&destAddr, sizeof(destAddr));
}

// 用户界面
void ShowMainMenu() {
    system("cls");
    std::cout << "===== 聊天助手 (" << myNickname << ") =====" << std::endl;
    std::cout << "1. 选择联系人\n2. 添加新联系人\n3. 设置昵称\n4. 退出\n选择: ";
}

void ShowChatWindow() {
    system("cls");
    std::cout << "===== 与 " << contacts[currentChatIP] << " 的对话 =====" << std::endl;

    // 显示聊天记录
    std::ifstream logFile(CHATLOG_DIR + ("\\" + currentChatIP + ".log"));
    std::string line;
    while (std::getline(logFile, line)) {
        std::cout << line << std::endl;
    }
    std::cout << "\n输入消息（输入 /exit 返回主菜单）:\n> ";
}

void AddContact() {
    std::string ip, name;
    std::cout << "输入IP地址: ";
    std::cin >> ip;
    std::cout << "输入联系人名称: ";
    std::cin >> name;
    contacts[ip] = name;
    SaveContacts();
}

void SetNickname() {
    std::cout << "输入你的昵称: ";
    std::cin >> myNickname;
}

void ChatLoop() {
    ShowChatWindow();
    std::string message;
    while (true) {
        std::getline(std::cin, message);
        if (message == "/exit") break;
        if (!message.empty()) {
            SendMessage(message, currentChatIP);
            SaveChatLog(currentChatIP, "[" + myNickname + "]: " + message);
        }
        ShowChatWindow();
    }
}

int main() {
    LoadContacts();
    InitializeNetwork();
    CreateThread(nullptr, 0, ReceiveThread, nullptr, 0, nullptr);

    if (myNickname.empty()) SetNickname();

    while (true) {
        ShowMainMenu();
        int choice;
        std::cin >> choice;
        std::cin.ignore();

        switch (choice) {
        case 1: {
            int index = 1;
            std::vector<std::string> ipList;
            std::cout << "\n联系人列表:\n";
            for (const auto& [ip, name] : contacts) {
                std::cout << index++ << ". " << name << " (" << ip << ")\n";
                ipList.push_back(ip);
            }
            if (ipList.empty()) {
                std::cout << "暂无联系人，请先添加！\n";
                break;
            }
            std::cout << "选择联系人编号: ";
            int select;
            std::cin >> select;
            if (select > 0 && select <= ipList.size()) {
                currentChatIP = ipList[select - 1];
                ChatLoop();
            }
            break;
        }
        case 2:
            AddContact();
            break;
        case 3:
            SetNickname();
            break;
        case 4:
            running = false;
            closesocket(sock);
            WSACleanup();
            return 0;
        }
    }
}