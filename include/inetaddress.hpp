#pragma once

#include <netinet/in.h>
#include <sys/types.h>

#include <cstdint>
#include <string>
class InetAddress {
 public:
  InetAddress(uint16_t port = 0, std::string ip = "127.0.0.1");
  explicit InetAddress(const sockaddr_in& addr);
  std::string toIp() const;
  uint16_t toPort() const;
  std::string toIpPort() const;

  const sockaddr_in* getSockAddr() const;

  void setSockAddr(const sockaddr_in& addr);

 private:
  uint16_t port_;
  std::string ip_;
  sockaddr_in addr_;
};