#pragma once

#include <cstddef>
#include <memory>
#include <string_view>

#include "tcpconnection.hpp"

namespace protocol {

class WsConnection {
 public:
  explicit WsConnection(transport::TcpConnectionPtr conn);

  // 发文本帧（opcode=0x1，FIN=1，服务端不掩码）
  void send(std::string_view text);

  // 发二进制帧（opcode=0x2，FIN=1）
  void sendBinary(const void* data, size_t len);

  // 发 Close 帧并关闭连接
  void close(uint16_t code = 1000);

  const transport::TcpConnectionPtr& tcp() const { return conn_; }

 private:
  // 拼帧头并通过 TcpConnection 发送
  void sendFrame(uint8_t opcode, const void* data, size_t len);

  transport::TcpConnectionPtr conn_;
};

using WsConnectionPtr     = std::shared_ptr<WsConnection>;
using WeakWsConnectionPtr = std::weak_ptr<WsConnection>;

}  // namespace protocol
