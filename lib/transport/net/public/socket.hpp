#pragma once
#include "inetaddress.hpp"

namespace transport {

/**
 * @brief RAII 封装一个 TCP socket 文件描述符。
 *
 * Socket 拥有传入的 fd，并在析构时关闭它。该类提供 bind、listen、
 * accept、半关闭写端以及常用 TCP/socket 选项设置。
 */
class Socket {
 public:
  /**
   * @brief 构造一个拥有指定文件描述符的 Socket。
   * @param sockfd 已创建的 socket 文件描述符。
   */
  explicit Socket(int sockfd);

  /**
   * @brief 析构 Socket，并关闭持有的文件描述符。
   */
  ~Socket();

  /**
   * @brief 获取底层 socket 文件描述符。
   */
  int fd() const;

  /**
   * @brief 绑定本地地址。
   */
  void bindAddress(const InetAddress& localaddr);

  /**
   * @brief 开始监听连接请求。
   */
  void listen();

  /**
   * @brief 接受一个新连接。
   * @param peeraddr 输出参数，用于保存对端地址。
   * @return 成功时返回已连接 socket 的文件描述符，失败时返回 -1。
   */
  int accept(InetAddress* peeraddr);

  /**
   * @brief 半关闭连接的写端。
   */
  void shutdownWrite();

  /**
   * @brief 开启或关闭 TCP_NODELAY。
   * @param on true 表示禁用 Nagle 算法。
   */
  void setTcpNoDelay(bool on);

  /**
   * @brief 开启或关闭 SO_REUSEADDR。
   */
  void setReuseAddr(bool on);

  /**
   * @brief 开启或关闭 SO_REUSEPORT。
   */
  void setReusePort(bool on);

  /**
   * @brief 开启或关闭 SO_KEEPALIVE。
   */
  void setKeepAlive(bool on);

 private:
  const int sockfd_;  ///< 持有并负责关闭的 socket 文件描述符。
};

}  // namespace transport
