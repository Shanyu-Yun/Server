#include "inetaddress.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>

InetAddress::InetAddress(uint16_t port, std::string ip) : port_(port), ip_(std::move(ip)) {
  // 此处等价于 memset(&addr_, 0, sizeof(addr_))，将addr_的所有字节初始化为0
  addr_ = {};
  // AF_INET表示IPv4地址族
  addr_.sin_family = AF_INET;
  // 将端口号转换为网络字节序，并存储在addr_.sin_port中，因为网络字节序是大端序，而主机字节序可能是小端序，所以需要转换
  addr_.sin_port = htons(port_);
  // 将ip地址字符串转换为二进制形式，并存储在addr_.sin_addr中
  inet_pton(AF_INET, ip_.c_str(), &addr_.sin_addr);
}

InetAddress::InetAddress(const sockaddr_in& addr) : addr_(addr) {
  //将网络字节序的IP地址转换为点分十进制字符串，并存储在ip_中
  ip_ = inet_ntoa(addr.sin_addr);
  //将网络字节序的端口号转换为主机字节序，并存储在port_中
  port_ = ntohs(addr.sin_port);
}

std::string InetAddress::toIp() const {
  return ip_;
}

uint16_t InetAddress::toPort() const {
  return port_;
}

std::string InetAddress::toIpPort() const {
  return ip_ + ":" + std::to_string(port_);
}

const sockaddr_in* InetAddress::getSockAddr() const {
  return &addr_;
}

void InetAddress::setSockAddr(const sockaddr_in& addr) {
  addr_ = addr;
  ip_ = inet_ntoa(addr.sin_addr);
  port_ = ntohs(addr.sin_port);
}