#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "buffer.hpp"
#include "wsframe.hpp"

namespace ws {

class WsContext {
 public:
  using FrameCallback = std::function<void(const WsFrame&)>;
  using SendCallback  = std::function<void(const std::string&)>;

  void setFrameCallback(FrameCallback cb) { frameCb_ = std::move(cb); }

  // WsServer 注入：WsContext 需要自动回 pong/close 时通过此回调发送原始帧字节
  void setSendCallback(SendCallback cb) { sendCb_ = std::move(cb); }

  // 喂入 Buffer，循环解帧直到不够一帧为止
  // 返回 false 表示协议错误，调用方应关闭连接
  bool parse(net::Buffer* buf);

 private:
  // 尝试从 buf 解出一帧；成功返回 true 并消费对应字节，否则一字节不动
  bool tryParseFrame(net::Buffer* buf);

  // 处理一个已解析完的帧（控制帧内部消化，数据帧走分片重组）
  void onFrame(WsFrame frame);

  static constexpr size_t kMaxMessageSize = 16 * 1024 * 1024;  // 16 MB 重组上限

  FrameCallback frameCb_;
  SendCallback  sendCb_;

  // 分片重组缓冲
  std::string fragBuffer_;
  Opcode fragOpcode_ = Opcode::kContinuation;
  bool inFragment_ = false;
};

}  // namespace ws
