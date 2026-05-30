#pragma once
#include <unordered_map>
#include <vector>

#include "base/timestamp.hpp"
#include "net/channel.hpp"

namespace tinynet {

class EventLoop;

/**
 * @brief IO 多路复用器的抽象基类。
 *
 * Poller 定义了 poll / updateChannel / removeChannel 三个核心接口，
 * 具体实现由 EpollPoller 等子类提供。每个 Poller 只属于一个 EventLoop。
 */
class Poller {
 public:
  /** @brief fd 到 Channel 指针的映射类型。 */
  using ChannelList = std::vector<Channel*>;

  /**
   * @brief 构造 Poller，绑定所属 EventLoop。
   * @param loop 所属的事件循环。
   */
  Poller(EventLoop* loop);

  /** @brief 虚析构，供子类安全销毁。 */
  virtual ~Poller() = default;

  /**
   * @brief 等待 IO 事件，将活跃 Channel 填入 activeChannels。
   * @param timeoutMs      超时时间（毫秒），-1 表示无限等待。
   * @param activeChannels 输出参数，本次有事件的 Channel 列表。
   * @return 本次 poll 调用返回时的时间戳。
   */
  virtual Timestamp poll(int timeoutMs, ChannelList* activeChannels) = 0;

  /**
   * @brief 将 Channel 的事件关注变化同步到底层 IO 多路复用器。
   * @param channel 需要更新的 Channel。
   */
  virtual void updateChannel(Channel* channel) = 0;

  /**
   * @brief 从 Poller 中注销并移除 Channel。
   * @param channel 需要移除的 Channel。
   */
  virtual void removeChannel(Channel* channel) = 0;

  /**
   * @brief 检查 Channel 是否已在 Poller 中注册。
   * @param channel 要查询的 Channel。
   * @return 已注册返回 true，否则返回 false。
   */
  virtual bool hasChannel(Channel* channel) const = 0;

  /**
   * @brief 创建默认的 Poller 实现（当前为 EpollPoller）。
   * @param loop 所属的事件循环。
   * @return 堆分配的 Poller 指针，调用方负责释放。
   */
  static Poller* newDefaultPoller(EventLoop* loop);

 protected:
  /** @brief fd 到已注册 Channel 的映射，供子类维护。 */
  std::unordered_map<int, Channel*> channels_;

 private:
  /** @brief 所属的事件循环。 */
  EventLoop* ownerLoop_;
};

}  // namespace tinynet
