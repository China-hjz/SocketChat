#include <SocketChat.hpp>
#define PORT 114514  
#define BUF_SIZE 1024

SOCKET sock;
bool running = true;
using namespace std;
namespace fs = filesystem;

DWORD WINAPI recvThread(LPVOID) {
	char buf[BUF_SIZE];
	sockaddr_in from;
	int fromLen = sizeof(from);

	while (running) {
		int len = recvfrom(sock, buf, BUF_SIZE, 0, (sockaddr*)&from, &fromLen);
		if (len > 0) {
			buf[len] = '\0';
			std::cout << "来自 " << inet_ntoa(from.sin_addr)
				<< ":" << ntohs(from.sin_port)
				<< " > " << buf << std::endl;
		}
	}
	return 0;
}


string get_current_timestamp() {
	auto now = chrono::system_clock::now();
	auto in_time_t = chrono::system_clock::to_time_t(now);

	stringstream ss;
	tm t;
	localtime_s(&t, &in_time_t);
	ss << put_time(&t, "%Y-%m-%d_%H-%M-%S");
	return ss.str();
}

void ensure_logs_directory_exists() {
	if (!fs::exists("logs")) {
		fs::create_directory("logs");
	}
}

int main()
{
	ensure_logs_directory_exists();
	std::string filename = "logs/log_" + get_current_timestamp() + ".txt";
	auto logger = spdlog::basic_logger_mt("timestamp_file_logger", filename);
	logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
	logger->set_level(spdlog::level::debug);



	logger->info("Log System Initialization completed");
	

	WSADATA ws;
	WSAStartup(MAKEWORD(2, 2), &ws);

	// 创建并绑定Socket
	sock = socket(AF_INET, SOCK_DGRAM, 0);
	sockaddr_in addr = { AF_INET, htons(PORT), INADDR_ANY };
	bind(sock, (sockaddr*)&addr, sizeof(addr));
	logger->info("Socket Created");
	// 启动接收线程
	CreateThread(0, 0, recvThread, 0, 0, 0);
	logger->info("Recv Start");
	// 发送循环
	std::cout << "输入目标IP和消息（格式：IP 消息）\n";
	while (true) {
		std::cout << "> ";
		std::string ip, msg;
		std::cin >> ip;
		if (ip == "exit") break;
		std::getline(std::cin >> std::ws, msg);

		sockaddr_in to = { AF_INET, htons(PORT) };
		inet_pton(AF_INET, ip.c_str(), &to.sin_addr);
		sendto(sock, msg.c_str(), msg.size(), 0, (sockaddr*)&to, sizeof(to));
	}

	running = false;
	closesocket(sock);
	WSACleanup();
	spdlog::shutdown();
	return 0;
}


