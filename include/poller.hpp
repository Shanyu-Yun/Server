#pragma once
#include <unordered_map>
#include <vector>

#include "channel.hpp"
#include "timestamp.hpp"

class EventLoop;

/**
 * @brief IO 多路复用器的抽象基类。
 *
 * Poller 维护 fd 到 Channel 的映射，并定义等待事件、更新 Channel、
 * 移除 Channel 等统一接口。具体实现由 EpollPoller 等子类完成。
 */
class Poller {
 public:
  /**
   * @brief Poller 返回的活跃 Channel 列表类型。
   */
  using ChannelList = std::vector<Channel*>;

  /**
   * @brief 构造一个绑定到指定 EventLoop 的 Poller。
   * @param loop 所属事件循环。
   */
  Poller(EventLoop* loop);

  /**
   * @brief 虚析构函数。
   */
  virtual ~Poller() = default;

  /**
   * @brief 等待 IO 事件。
   * @param timeoutMs 超时时间，单位毫秒。
   * @param activeChannels 输出参数，用于保存活跃 Channel。
   * @return poll 返回时的时间戳。
   */
  virtual Timestamp poll(int timeoutMs, ChannelList* activeChannels) = 0;

  /**
   * @brief 更新 Channel 在 Poller 中的注册状态。
   * @param channel 要更新的 Channel。
   */
  virtual void updateChannel(Channel* channel) = 0;

  /**
   * @brief 从 Poller 中移除 Channel。
   * @param channel 要移除的 Channel。
   */
  virtual void removeChannel(Channel* channel) = 0;

  /**
   * @brief 判断 Poller 中是否存在指定 Channel。
   * @param channel 要检查的 Channel。
   * @return 存在且指针匹配时返回 true。
   */
  virtual bool hasChannel(Channel* channel) const = 0;

  /**
   * @brief 创建默认 Poller 实现。
   * @param loop 所属事件循环。
   * @return 新创建的 Poller 指针，调用方负责管理生命周期。
   */
  static Poller* newDefaultPoller(EventLoop* loop);

 protected:
  /**
   * @brief fd 到 Channel 的映射表。
   */
  std::unordered_map<int, Channel*> channels_;

 private:
  /**
   * @brief 拥有当前 Poller 的 EventLoop。
   */
  EventLoop* ownerLoop_;
};
