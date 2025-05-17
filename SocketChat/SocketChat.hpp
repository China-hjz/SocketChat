#pragma once
#define _WINSOCK_DEPRECATED_NO_WARNINGS
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
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <string>
#include <fstream>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
using namespace std;
using namespace std::filesystem;
