#pragma once
#include <functional>
#include <memory>

#include "base/buffer.hpp"
#include "base/timestamp.hpp"

namespace tinynet {

class TcpConnection;

/**
 * @brief 指向 TcpConnection 的共享指针，用于在回调中共享连接的所有权。
 */
using TcpConnectionPtr = std::shared_ptr<TcpConnection>;

/**
 * @brief 连接建立或断开时触发的回调。
 */
using ConnectionCallback = std::function<void(const TcpConnectionPtr&)>;

/**
 * @brief 连接关闭时触发的回调。
 */
using CloseCallback = std::function<void(const TcpConnectionPtr&)>;

/**
 * @brief 输出缓冲区数据全部写入 socket 后触发的回调。
 */
using WriteCompleteCallback = std::function<void(const TcpConnectionPtr&)>;

/**
 * @brief 输出缓冲区首次超过高水位阈值时触发的回调。
 *
 * 第二个参数为当前输出缓冲区的字节数。
 */
using HighWaterMarkCallback = std::function<void(const TcpConnectionPtr&, size_t)>;

/**
 * @brief socket 可读且成功读取数据后触发的回调。
 *
 * 参数依次为连接指针、输入缓冲区指针、Poller 返回事件时的时间戳。
 */
using MessageCallback = std::function<void(const TcpConnectionPtr&, Buffer*, Timestamp)>;

/**
 * @brief 定时器回调。
 *
 * 无参数，用户可以通过捕获外部变量来获取上下文信息。
 */
using TimerCallback = std::function<void()>;

}  // namespace tinynet
