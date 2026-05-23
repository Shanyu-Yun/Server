#pragma once

#include <sys/epoll.h>

#include <vector>

#include "poller.hpp"
#include "timestamp.hpp"

/**
 * @brief 基于 epoll 的 Poller 实现。
 *
 * EpollPoller 负责把 Channel 的关注事件同步到 epoll，并在 poll()
 * 返回后填充活跃 Channel 列表。
 */
class EpollPoller : public Poller {
 public:
  /**
   * @brief 创建 epoll 实例并绑定所属 EventLoop。
   * @param loop 所属事件循环。
   */
  EpollPoller(EventLoop* loop);

  /**
   * @brief 析构 EpollPoller，并关闭 epoll fd。
   */
  ~EpollPoller() override;

  /**
   * @brief 等待 IO 事件。
   * @param timeoutMs epoll_wait 超时时间，单位毫秒。
   * @param activeChannels 输出参数，用于保存活跃 Channel。
   * @return poll 返回时的时间戳。
   */
  Timestamp poll(int timeoutMs, ChannelList* activeChannels) override;

  /**
   * @brief 更新 Channel 在 epoll 中的注册状态。
   * @param channel 要更新的 Channel。
   */
  void updateChannel(Channel* channel) override;

  /**
   * @brief 从 epoll 和内部 Channel 表中移除 Channel。
   * @param channel 要移除的 Channel。
   */
  void removeChannel(Channel* channel) override;

  /**
   * @brief 判断指定 Channel 是否归当前 Poller 管理。
   * @param channel 要检查的 Channel。
   * @return 存在且指针匹配时返回 true。
   */
  bool hasChannel(Channel* channel) const override;

 private:
  /**
   * @brief epoll 事件数组初始大小。
   */
  static const int kInitEventListSize = 16;

  /**
   * @brief 将 epoll_wait 返回的事件转换为活跃 Channel 列表。
   * @param numEvents epoll_wait 返回的事件数量。
   * @param activeChannels 输出参数，用于保存活跃 Channel。
   */
  void fillActiveChannels(int numEvents, ChannelList* activeChannels) const;

  /**
   * @brief 调用 epoll_ctl 执行 ADD/MOD/DEL 操作。
   * @param operation epoll_ctl 操作类型。
   * @param channel 要操作的 Channel。
   */
  void update(int operation, Channel* channel);

  /**
   * @brief epoll 文件描述符。
   */
  int epollfd_;

  /**
   * @brief epoll_wait 使用的事件数组。
   */
  std::vector<epoll_event> events_;
};
