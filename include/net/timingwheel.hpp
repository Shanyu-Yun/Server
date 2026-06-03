#pragma once
#include <deque>
#include <memory>
#include <unordered_set>

#include "base/noncopyable.hpp"
#include "net/callbacks.hpp"

namespace tinynet {
class EventLoop;

/**
 * @brief 基于时间轮的连接空闲超时管理器。
 *
 * 将时间轴切分为 timeoutSeconds 个等长的格（Bucket），每格代表 1 秒。
 * deque 整体构成一个滑动窗口：每秒向队尾追加一个空格，同时弹出队首最老的格。
 * 当某格被弹出时，其持有的所有 Entry 的 shared_ptr 引用计数归零，
 * Entry 析构函数随即调用 TcpConnection::forceClose()，完成超时踢出。
 *
 * 刷新超时：收到消息时将该连接对应的 shared_ptr<Entry> 重新插入队尾，
 * 使引用计数 >= 2，旧格弹出后引用计数降为 1 而不归零，连接得以续命。
 *
 * 用法：将 onConnection / onMessage 分别挂载到 TcpServer 的同名回调即可。
 */
class TimingWheel : noncopyable {
 public:
  /**
   * @brief 构造时间轮。
   * @param loop           所属 EventLoop，用于注册每秒定时器。
   * @param timeoutSeconds 空闲超时秒数，同时决定时间轮的格数。
   */
  TimingWheel(EventLoop* loop, int timeoutSeconds);

  /**
   * @brief 新连接建立时调用，将连接纳入超时管理。
   *
   * 创建一个 Entry 放入当前最新格，并把对应的 weak_ptr<Entry>
   * 存入 conn->context_，供 onMessage 刷新超时时使用。
   * @param conn 新建立的 TCP 连接。
   */
  void onConnection(const TcpConnectionPtr& conn);

  /**
   * @brief 连接收到消息时调用，刷新该连接的超时计时。
   *
   * 从 conn->context_ 取出 weak_ptr<Entry> 并 lock，
   * 将 shared_ptr<Entry> 重新插入最新格，延长连接的存活窗口。
   * @param conn        收到消息的连接。
   * @param buf         输入缓冲区（本函数不消费数据）。
   * @param receiveTime 消息到达时间戳。
   */
  void onMessage(const TcpConnectionPtr& conn, Buffer* buf, Timestamp receiveTime);

  ~TimingWheel();

 private:
  /**
   * @brief 超时关闭的句柄，与单条 TcpConnection 一一对应。
   *
   * 析构时尝试 lock weakConn_：若连接仍存活则调用 forceClose()。
   * 使用 weak_ptr 避免与 TcpConnection 的 shared_ptr 形成循环引用。
   */
  struct Entry {
   public:
    explicit Entry(const std::weak_ptr<TcpConnection>& weakConn);
    ~Entry();

   public:
    std::weak_ptr<TcpConnection> weakConn_;  ///< 对所管理连接的弱引用。
  };

 private:
  /// 每个格持有若干 Entry 的 shared_ptr；unordered_set 保证同格内不重复。
  using Bucket = std::unordered_set<std::shared_ptr<Entry>>;

  /**
   * @brief 每秒触发一次的定时回调，推进时间轮。
   *
   * 向队尾追加空格，并弹出队首最老的格；
   * 被弹出格中引用计数归零的 Entry 会在此处析构并关闭对应连接。
   */
  void onTimer();

  EventLoop* loop_;             ///< 所属事件循环。
  int timeoutSeconds_;          ///< 空闲超时秒数，等于 buckets_ 的长度。
  std::deque<Bucket> buckets_;  ///< 时间轮主体，队首最老、队尾最新。
};
}  // namespace tinynet