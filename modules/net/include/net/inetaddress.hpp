#pragma once

#include <netinet/in.h>
#include <sys/types.h>

#include <cstdint>
#include <string>

namespace tinynet {

/**
 * @brief IPv4 地址与端口的轻量封装。
 *
 * InetAddress 在字符串形式的 ip/port 和 sockaddr_in 之间做转换，
 * 供 Socket::bindAddress()、Socket::accept() 以及连接回调使用。
 */
class InetAddress {
 public:
  /**
   * @brief 根据端口和 IPv4 字符串构造地址。
   * @param port 主机字节序端口。
   * @param ip 点分十进制 IPv4 字符串。
   */
  InetAddress(uint16_t port = 0, std::string ip = "127.0.0.1");

  /**
   * @brief 根据 sockaddr_in 构造地址。
   * @param addr 已填充的 IPv4 socket 地址。
   */
  explicit InetAddress(const sockaddr_in& addr);

  /**
   * @brief 获取 IP 字符串。
   * @return 点分十进制 IPv4 地址。
   */
  std::string toIp() const;

  /**
   * @brief 获取端口。
   * @return 主机字节序端口。
   */
  uint16_t toPort() const;

  /**
   * @brief 获取 "ip:port" 格式的地址字符串。
   * @return 地址和端口拼接后的字符串。
   */
  std::string toIpPort() const;

  /**
   * @brief 获取底层 sockaddr_in 地址。
   * @return 指向内部 sockaddr_in 的只读指针。
   */
  const sockaddr_in* getSockAddr() const;

  /**
   * @brief 设置底层 sockaddr_in 地址，并同步字符串形式的 ip/port。
   * @param addr 新的 IPv4 socket 地址。
   */
  void setSockAddr(const sockaddr_in& addr);

 private:
  uint16_t port_;     ///< 主机字节序端口。
  std::string ip_;    ///< 点分十进制 IPv4 字符串。
  sockaddr_in addr_;  ///< 底层 IPv4 socket 地址。
};

}  // namespace tinynet
