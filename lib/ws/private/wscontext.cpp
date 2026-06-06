#include "wscontext.hpp"

#include <cstddef>
#include <cstdint>
#include <utility>

#include "wsframe.hpp"

namespace ws {

bool WsContext::parse(net::Buffer* buf) {
  bool ok = true;
  while (ok) {
    ok = tryParseFrame(buf);
  }
  return true;
}

bool WsContext::tryParseFrame(net::Buffer* buf) {
  // 一帧的头部都不足，说明解析无意义
  if (buf->readableBytes() < 2)
    return false;

  const uint8_t* p = reinterpret_cast<const uint8_t*>(buf->peek());
  // 解析第一个字节
  // 1位为Fin
  bool fin = (p[0] & 0x80) != 0;
  // 4-7位为opcode
  auto opcode = static_cast<Opcode>(p[0] & 0x0F);

  // 解析第二个字节
  // 1位为masked
  bool masked = (p[1] & 0x80) != 0;
  // 2-7为payloadLen
  uint64_t len7 = (p[1] & 0x7F);

  // 算出完整帧的长度
  size_t headerLen = 2;
  if (len7 == 126) {
    headerLen += 2;
  } else if (len7 == 127) {
    headerLen += 8;
  }
  if (masked)
    headerLen += 4;

  if (buf->readableBytes() < headerLen)
    return false;

  uint64_t payloadLen = len7;
  if (len7 == 126) {
    payloadLen = (uint64_t(p[2]) << 8 | p[3]);
  } else if (len7 == 127) {
    payloadLen = 0;
    for (int i = 0; i < 8; ++i) {
      payloadLen = (payloadLen << 8) | p[2 + i];
    }
  }

  // 整帧数据是否到齐
  if (buf->readableBytes() < headerLen + payloadLen)
    return false;

  const uint8_t* maskKey = masked ? (p + headerLen - 4) : nullptr;
  const uint8_t* rawPayload = p + headerLen;
  std::string payload(reinterpret_cast<const char*>(rawPayload), payloadLen);
  if (masked) {
    for (size_t i = 0; i < payloadLen; ++i) {
      payload[i] ^= maskKey[i % 4];
    }
  }

  buf->consume(headerLen + payloadLen);
  onFrame(WsFrame{opcode, fin, std::move(payload)});
  return true;
}

void WsContext::onFrame(WsFrame frame) {
  Opcode op = frame.opcode;
  switch (op) {
    // ── 控制帧（RFC 6455 §5.5）：不受分片约束，可插在数据分片中间 ──────────

    case Opcode::kPing:
      // RFC 强制要求：收到 ping 必须立刻回 pong，payload 原样带回
      // 帧格式：0x8A（FIN=1 opcode=0xA）+ 1字节长度 + payload
      if (sendCb_) {
        std::string pong;
        pong.push_back(char(0x8A));
        pong.push_back(char(frame.payload.size() & 0x7F));  // 控制帧 payload ≤ 125
        pong += frame.payload;
        sendCb_(pong);
      }
      return;

    case Opcode::kPong:
      // 对端回应了我们发出的 ping，连接存活确认，无需动作
      return;

    case Opcode::kClose:
      // 收到对端的关闭请求，先回一个 close 帧（RFC 要求的挥手），再通知上层关连接
      // 帧格式：0x88（FIN=1 opcode=0x8）+ 0x00（无状态码 payload）
      if (sendCb_) {
        std::string close;
        close.push_back(char(0x88));
        close.push_back(char(0x00));
        sendCb_(close);
      }
      if (frameCb_) frameCb_(frame);  // 上层（WsServer）收到后调 conn->shutdown()
      return;

    // ── 数据帧：处理分片重组 ────────────────────────────────────────────────

    case Opcode::kText:
    case Opcode::kBinary:
      if (frame.fin) {
        // FIN=1：单帧完整消息，直接交给用户回调
        if (frameCb_) frameCb_(frame);
      } else {
        // FIN=0：分片消息的首帧，记录类型并开始攒 payload
        fragOpcode_ = op;
        fragBuffer_ = std::move(frame.payload);
        inFragment_ = true;
      }
      return;

    case Opcode::kContinuation:
      if (inFragment_) {
        fragBuffer_ += frame.payload;
        if (fragBuffer_.size() > kMaxMessageSize) {
          // 超过 16MB 重组上限，发 close(1009 Message Too Big) 并丢弃
          // 1009 大端拆开：高字节 0x03，低字节 0xF1
          inFragment_ = false;
          fragBuffer_.clear();
          if (sendCb_) {
            std::string close;
            close.push_back(char(0x88));
            close.push_back(char(0x02));  // payload 长度 = 2（状态码）
            close.push_back(char(0x03));
            close.push_back(char(0xF1));
            sendCb_(close);
          }
          return;
        }
        if (frame.fin) {
          // FIN=1：分片的最后一帧，重组完成，交给用户回调
          WsFrame complete(fragOpcode_, true, std::move(fragBuffer_));
          inFragment_ = false;
          fragOpcode_ = Opcode::kContinuation;
          if (frameCb_) frameCb_(complete);
        }
        // FIN=0：还有后续续帧，继续等待
      }
      break;

    default:
      break;  // 未知 opcode，忽略（协议扩展保留位）
  }
}

}  // namespace ws
