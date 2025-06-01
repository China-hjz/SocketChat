#include <iostream>
#include <fstream>
#include <thread>
#include <atomic>
#include <string>
#include <vector>
#include <map>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shlobj.h>
#include <cctype>
#include <algorithm>
#include <filesystem>
#include <ctime>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "shell32.lib")

#define PORT 8888
#define BUFFER_SIZE 1024
#define MAX_FILE_BUFFER (BUFFER_SIZE - sizeof(PacketHeader))
#define CONTACTS_FILE "contacts.dat"
#define CHATLOG_DIR "chatlogs"
#define TEMP_DIR "temp"

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
    char fileName[260]; // 用于文件传输时携带文件名
    uint64_t fileSize;  // 文件总大小
};
#pragma pack(pop)

SOCKET sock;
std::atomic<bool> running(true);
std::string myNickname;
std::map<std::string, std::string> contacts; // IP -> Nickname
std::string currentChatIP;

// 文件传输状态
struct FileTransfer {
    std::string tempFilePath;
    std::string finalFilePath;
    std::ofstream fileStream;
    uint64_t receivedBytes = 0;
    uint64_t totalBytes = 0;
    std::string senderIP;
    std::string fileName;
};
FileTransfer incomingFile;

// 文件操作辅助函数
void CreateDirectoryIfNotExists(const char* path) {
    if (!CreateDirectoryA(path, NULL)) {
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

// 生成唯一文件名
std::string GenerateTempFileName(const std::string& originalName) {
    std::time_t now = std::time(nullptr);
    std::string timestamp = std::to_string(now);
    return std::string(TEMP_DIR) + "\\" + timestamp + "_" + originalName;
}

// 复制文件
bool CopyFileToLocation(const std::string& source, const std::string& destination) {
    std::ifstream src(source, std::ios::binary);
    if (!src) {
        std::cerr << "无法打开源文件: " << source << std::endl;
        return false;
    }

    std::ofstream dst(destination, std::ios::binary);
    if (!dst) {
        std::cerr << "无法创建目标文件: " << destination << std::endl;
        return false;
    }

    dst << src.rdbuf();
    return src && dst;
}

// 打开文件夹
void OpenContainingFolder(const std::string& filePath) {
    std::string command = "explorer /select,\"" + filePath + "\"";
    system(command.c_str());
}

// 选择保存位置
std::string SelectSaveLocation(const std::string& defaultFileName) {
    wchar_t path[MAX_PATH] = { 0 };

    // 使用通用对话框选择文件夹
    BROWSEINFOW bi = { 0 };
    bi.lpszTitle = L"选择文件保存位置";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (pidl != nullptr) {
        if (SHGetPathFromIDListW(pidl, path)) {
            // 构建完整文件路径
            std::wstring folderPath(path);
            std::wstring wDefaultFileName(defaultFileName.begin(), defaultFileName.end());
            std::wstring wFullPath = folderPath + L"\\" + wDefaultFileName;

            // 清理内存
            CoTaskMemFree(pidl);

            // 转换回 std::string
            int len = WideCharToMultiByte(CP_ACP, 0, wFullPath.c_str(), -1, nullptr, 0, nullptr, nullptr);
            std::string fullPath(len, 0);
            WideCharToMultiByte(CP_ACP, 0, wFullPath.c_str(), -1, &fullPath[0], len, nullptr, nullptr);
            // 去掉末尾的 '\0'
            if (!fullPath.empty() && fullPath.back() == '\0') fullPath.pop_back();
            return fullPath;
        }
        CoTaskMemFree(pidl);
    }

    // 默认保存到当前目录下的Downloads文件夹
    return "Downloads\\" + defaultFileName;
}

// 网络功能
void InitializeNetwork() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup失败: " << WSAGetLastError() << std::endl;
        exit(1);
    }

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        std::cerr << "创建socket失败: " << WSAGetLastError() << std::endl;
        WSACleanup();
        exit(1);
    }

    int reuse = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char*)&reuse, sizeof(reuse)) == SOCKET_ERROR) {
        std::cerr << "设置SO_REUSEADDR失败: " << WSAGetLastError() << std::endl;
    }

    sockaddr_in localAddr;
    memset(&localAddr, 0, sizeof(localAddr));
    localAddr.sin_family = AF_INET;
    localAddr.sin_port = htons(PORT);
    localAddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock, (sockaddr*)&localAddr, sizeof(localAddr)) == SOCKET_ERROR) {
        std::cerr << "绑定端口失败: " << WSAGetLastError() << std::endl;
        closesocket(sock);
        WSACleanup();
        exit(1);
    }
}

DWORD WINAPI ReceiveThread(LPVOID) {
    char buffer[BUFFER_SIZE];
    sockaddr_in fromAddr;
    int addrLen = sizeof(fromAddr);

    while (running) {
        int len = recvfrom(sock, buffer, sizeof(buffer), 0, (sockaddr*)&fromAddr, &addrLen);
        if (len <= 0) continue;

        PacketHeader header;
        memcpy(&header, buffer, sizeof(header));

        char ipStr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &fromAddr.sin_addr, ipStr, INET_ADDRSTRLEN);
        std::string senderIP(ipStr);

        if (header.mainType == MainPacketType::ChatMessage) {
            std::string message(buffer + sizeof(header), header.dataSize);
            std::string logEntry = "[" + std::string(header.senderName) + "]: " + message;

            std::cout << "\n" << logEntry << std::endl;
            SaveChatLog(senderIP, logEntry);
        }
        else if (header.mainType == MainPacketType::FileTransfer) {
            switch (header.subType) {
            case FileSubType::Start: {
                // 文件传输开始
                incomingFile.senderIP = senderIP;
                incomingFile.fileName = header.fileName;
                incomingFile.totalBytes = header.fileSize;
                incomingFile.receivedBytes = 0;

                // 生成临时文件名
                CreateDirectoryIfNotExists(TEMP_DIR);
                incomingFile.tempFilePath = GenerateTempFileName(header.fileName);

                // 打开临时文件
                incomingFile.fileStream.open(incomingFile.tempFilePath, std::ios::binary);
                if (!incomingFile.fileStream) {
                    std::cerr << "无法创建临时文件: " << incomingFile.tempFilePath << std::endl;
                    incomingFile.fileName.clear();
                }
                else {
                    std::cout << "\n开始接收文件: " << header.fileName
                        << " (" << header.fileSize << " 字节) 来自 "
                        << (contacts.count(senderIP) ? contacts[senderIP] : senderIP) << std::endl;
                    std::cout << "临时文件: " << incomingFile.tempFilePath << std::endl;
                }
                break;
            }
            case FileSubType::DataPacket: {
                // 文件数据包
                if (!incomingFile.fileName.empty() && senderIP == incomingFile.senderIP) {
                    incomingFile.fileStream.write(buffer + sizeof(header), header.dataSize);
                    incomingFile.receivedBytes += header.dataSize;

                    // 显示进度
                    int progress = static_cast<int>((static_cast<double>(incomingFile.receivedBytes) / incomingFile.totalBytes) * 100);
                    std::cout << "\r接收进度: " << progress << "% ("
                        << incomingFile.receivedBytes << "/" << incomingFile.totalBytes << ")";
                }
                break;
            }
            case FileSubType::TransferEnd: {
                // 文件传输结束
                if (!incomingFile.fileName.empty() && senderIP == incomingFile.senderIP) {
                    incomingFile.fileStream.close();
                    std::cout << "\n文件接收完成，临时保存位置: " << incomingFile.tempFilePath << std::endl;

                    // 提示用户选择保存位置
                    std::cout << "请选择文件保存位置...\n";
                    incomingFile.finalFilePath = SelectSaveLocation(incomingFile.fileName);

                    // 确保目录存在
                    std::string directory = incomingFile.finalFilePath.substr(0, incomingFile.finalFilePath.find_last_of("\\/"));
                    CreateDirectoryIfNotExists(directory.c_str());

                    // 将文件从临时位置复制到最终位置
                    if (CopyFileToLocation(incomingFile.tempFilePath, incomingFile.finalFilePath)) {
                        std::cout << "文件已保存到: " << incomingFile.finalFilePath << std::endl;

                        // 删除临时文件
                        std::remove(incomingFile.tempFilePath.c_str());

                        // 打开文件所在文件夹
                        OpenContainingFolder(incomingFile.finalFilePath);
                    }
                    else {
                        std::cerr << "文件保存失败! 临时文件保留在: " << incomingFile.tempFilePath << std::endl;
                    }

                    incomingFile.fileName.clear();
                }
                break;
            }
            }
        }
    }
    return 0;
}

void SendMessage(const std::string& message, const std::string& targetIP) {
    sockaddr_in destAddr;
    memset(&destAddr, 0, sizeof(destAddr));
    destAddr.sin_family = AF_INET;
    destAddr.sin_port = htons(PORT);
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

// 发送文件
void SendFile(const std::string& filePath, const std::string& targetIP) {
    // 打开文件
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file) {
        std::cerr << "无法打开文件: " << filePath << std::endl;
        return;
    }

    // 获取文件信息
    uint64_t fileSize = static_cast<uint64_t>(file.tellg());
    file.seekg(0, std::ios::beg);

    // 提取文件名
    std::string fileName = filePath.substr(filePath.find_last_of("\\/") + 1);
    if (fileName.empty()) fileName = "file.bin";

    // 发送开始包
    PacketHeader startHeader{};
    startHeader.mainType = MainPacketType::FileTransfer;
    startHeader.subType = FileSubType::Start;
    startHeader.fileSize = fileSize;
    strncpy_s(startHeader.senderName, myNickname.c_str(), sizeof(startHeader.senderName));
    strncpy_s(startHeader.fileName, fileName.c_str(), sizeof(startHeader.fileName));

    sockaddr_in destAddr;
    memset(&destAddr, 0, sizeof(destAddr));
    destAddr.sin_family = AF_INET;
    destAddr.sin_port = htons(PORT);
    InetPtonA(AF_INET, targetIP.c_str(), &destAddr.sin_addr);

    sendto(sock, (char*)&startHeader, sizeof(startHeader), 0,
        (sockaddr*)&destAddr, sizeof(destAddr));

    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // 给接收方准备时间

    // 发送数据包
    char buffer[MAX_FILE_BUFFER];
    uint32_t seqNumber = 0;
    uint64_t sentBytes = 0;

    while (!file.eof()) {
        file.read(buffer, sizeof(buffer));
        std::streamsize bytesRead = file.gcount();

        if (bytesRead > 0) {
            PacketHeader dataHeader{};
            dataHeader.mainType = MainPacketType::FileTransfer;
            dataHeader.subType = FileSubType::DataPacket;
            dataHeader.seqNumber = seqNumber++;
            dataHeader.dataSize = static_cast<uint16_t>(bytesRead);
            strncpy_s(dataHeader.senderName, myNickname.c_str(), sizeof(dataHeader.senderName));

            std::vector<char> packet(sizeof(dataHeader) + bytesRead);
            memcpy(packet.data(), &dataHeader, sizeof(dataHeader));
            memcpy(packet.data() + sizeof(dataHeader), buffer, bytesRead);

            sendto(sock, packet.data(), static_cast<int>(packet.size()), 0,
                (sockaddr*)&destAddr, sizeof(destAddr));

            sentBytes += bytesRead;
            int progress = static_cast<int>((static_cast<double>(sentBytes) / fileSize) * 100);
            std::cout << "\r发送进度: " << progress << "% (" << sentBytes << "/" << fileSize << ")";
        }
    }

    // 发送结束包
    PacketHeader endHeader{};
    endHeader.mainType = MainPacketType::FileTransfer;
    endHeader.subType = FileSubType::TransferEnd;
    strncpy_s(endHeader.senderName, myNickname.c_str(), sizeof(endHeader.senderName));
    strncpy_s(endHeader.fileName, fileName.c_str(), sizeof(endHeader.fileName));

    sendto(sock, (char*)&endHeader, sizeof(endHeader), 0,
        (sockaddr*)&destAddr, sizeof(destAddr));

    std::cout << "\n文件发送完成: " << fileName << std::endl;
    file.close();
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
    std::cout << "\n输入消息（输入 /exit 返回主菜单, /send 路径 发送文件）:\n> ";
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

        // 处理退出命令
        if (message == "/exit") break;

        // 处理文件发送命令
        if (message.rfind("/send ", 0) == 0) {
            std::string filePath = message.substr(6);
            SendFile(filePath, currentChatIP);
            ShowChatWindow();
            continue;
        }

        // 发送普通消息
        if (!message.empty()) {
            SendMessage(message, currentChatIP);
            SaveChatLog(currentChatIP, "[" + myNickname + "]: " + message);
            ShowChatWindow();
        }
    }
}

int main() {
    // 创建必要的目录
    CreateDirectoryIfNotExists(CHATLOG_DIR);
    CreateDirectoryIfNotExists(TEMP_DIR);
    CreateDirectoryIfNotExists("Downloads");

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
                system("pause");
                break;
            }
            std::cout << "选择联系人编号: ";
            int select;
            std::cin >> select;
            std::cin.ignore();
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