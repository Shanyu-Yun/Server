#include "inetaddress.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>

namespace transport {

InetAddress::InetAddress(uint16_t port, std::string ip) : port_(port), ip_(std::move(ip)) {
  addr_ = {};
  addr_.sin_family = AF_INET;
  addr_.sin_port = htons(port_);
  inet_pton(AF_INET, ip_.c_str(), &addr_.sin_addr);
}

InetAddress::InetAddress(const sockaddr_in& addr) : addr_(addr) {
  ip_ = inet_ntoa(addr.sin_addr);
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

}  // namespace transport
