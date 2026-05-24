#pragma once
#include <functional>
#include <memory>

#include "buffer.hpp"
#include "timestamp.hpp"

class TcpConnection;
using TcpConnectionPtr = std::shared_ptr<TcpConnection>;

/****************** 连接回调函数类型定义 ******************/
using ConnectionCallback = std::function<void(const TcpConnectionPtr&)>;
using CloseCallback = std::function<void(const TcpConnectionPtr&)>;
using WriteCompleteCallback = std::function<void(const TcpConnectionPtr&)>;
using HighWaterMarkCallback = std::function<void(const TcpConnectionPtr&, size_t)>;
using MessageCallback = std::function<void(const TcpConnectionPtr&, Buffer*, Timestamp)>;