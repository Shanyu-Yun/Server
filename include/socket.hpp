#pragma once
#include "inetaddress.hpp"

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
   * @return 当前 Socket 持有的文件描述符。
   */
  int fd() const;

  /**
   * @brief 绑定本地地址。
   * @param localaddr 要绑定的 IP 和端口。
   */
  void bindAddress(const InetAddress& localaddr);

  /**
   * @brief 开始监听连接请求。
   *
   * 使用系统默认的最大监听队列长度 SOMAXCONN。
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
   *
   * 调用后仍可读取对端数据，但不会再向对端发送数据。
   */
  void shutdownWrite();

  /**
   * @brief 开启或关闭 TCP_NODELAY。
   * @param on true 表示禁用 Nagle 算法，false 表示启用 Nagle 算法。
   * Nagle 算法会将小数据包合并成更大的包以减少网络拥塞，但可能增加延迟。
   */
  void setTcpNoDelay(bool on);

  /**
   * @brief 开启或关闭 SO_REUSEADDR。
   * @param on true 表示允许地址复用，false 表示关闭地址复用。
   */
  void setReuseAddr(bool on);

  /**
   * @brief 开启或关闭 SO_REUSEPORT。
   * @param on true 表示允许端口复用，false 表示关闭端口复用。
   */
  void setReusePort(bool on);

  /**
   * @brief 开启或关闭 SO_KEEPALIVE。
   * @param on true 表示启用 TCP keepalive，false 表示关闭 TCP keepalive。
   */
  void setKeepAlive(bool on);

 private:
  /**
   * @brief 持有并负责关闭的 socket 文件描述符。
   */
  const int sockfd_;
};
