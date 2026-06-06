#include "wsconnection.hpp"

namespace ws {

WsConnection::WsConnection(net::TcpConnectionPtr conn) : conn_(std::move(conn)) {}

void WsConnection::send(std::string_view text) {
  // TODO: 实现文本帧编码发送
}

void WsConnection::sendBinary(const void* data, size_t len) {
  // TODO: 实现二进制帧编码发送
}

void WsConnection::close(uint16_t code) {
  // TODO: 实现 Close 帧发送
}

void WsConnection::sendFrame(uint8_t opcode, const void* data, size_t len) {
  // TODO: 实现帧编码
}

}  // namespace ws
