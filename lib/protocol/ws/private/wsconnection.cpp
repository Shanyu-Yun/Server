#include "wsconnection.hpp"

#include <cstring>

#include "buffer.hpp"
#include "wsframe.hpp"

namespace protocol {

WsConnection::WsConnection(transport::TcpConnectionPtr conn) : conn_(std::move(conn)) {}

void WsConnection::send(std::string_view text) {
  Opcode textOpcode = Opcode::kText;
  sendFrame(static_cast<uint8_t>(textOpcode), text.data(), text.size());
}

void WsConnection::sendBinary(const void* data, size_t len) {
  Opcode binaryOpcode = Opcode::kBinary;
  sendFrame(static_cast<uint8_t>(binaryOpcode), data, len);
}

void WsConnection::close(uint16_t code) {
  Opcode closeOpcode = Opcode::kClose;
  uint8_t payload[2];
  payload[0] = static_cast<uint8_t>(code >> 8);
  payload[1] = static_cast<uint8_t>(code & 0xFF);
  sendFrame(static_cast<uint8_t>(closeOpcode), payload, sizeof(payload));
  conn_->shutdown();
}

void WsConnection::sendFrame(uint8_t opcode, const void* data, size_t len) {
  uint8_t header[10];  // 最长的帧头是 10 字节（payload len=127）
  transport::Buffer frame;
  std::memset(header, 0, sizeof(header));
  header[0] = 0x80 | opcode;  // FIN=1，设置 opcode
  if (len <= 125) {
    header[1] = static_cast<uint8_t>(len);
    frame.append(static_cast<const char*>(static_cast<const void*>(header)), 2);
  } else if (len <= 65535) {
    header[1] = 126;
    uint16_t len16 = htons(static_cast<uint16_t>(len));
    std::memcpy(header + 2, &len16, sizeof(len16));
    frame.append(static_cast<const char*>(static_cast<const void*>(header)), 4);
  } else {
    header[1] = 127;
    uint64_t len64 = htobe64(len);
    std::memcpy(header + 2, &len64, sizeof(len64));
    frame.append(static_cast<const char*>(static_cast<const void*>(header)), 10);
  }
  frame.append(static_cast<const char*>(data), len);
  conn_->send(frame.peek(), frame.readableBytes());
}
}  // namespace protocol