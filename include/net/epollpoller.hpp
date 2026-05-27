#pragma once

#include <sys/epoll.h>

#include <vector>

#include "net/poller.hpp"

/**
 * @brief 基于 Linux epoll 的 Poller 实现。
 *
 * 使用 epoll_create1 创建 epollfd，通过 epoll_ctl 维护 Channel 注册，
 * 调用 epoll_wait 等待就绪事件，并将结果转换为活跃 Channel 列表。
 */
class EpollPoller : public Poller {
 public:
  /**
   * @brief 构造 EpollPoller，创建 epollfd。
   * @param loop 所属的事件循环。
   */
  EpollPoller(EventLoop* loop);

  /** @brief 析构 EpollPoller，关闭 epollfd。 */
  ~EpollPoller() override;

  /**
   * @brief 调用 epoll_wait 等待就绪事件。
   * @param timeoutMs      超时毫秒数，-1 表示无限等待。
   * @param activeChannels 输出参数，本次有事件的 Channel 列表。
   * @return epoll_wait 返回时的时间戳。
   */
  Timestamp poll(int timeoutMs, ChannelList* activeChannels) override;

  /**
   * @brief 通过 epoll_ctl 将 Channel 的事件变化同步到 epollfd。
   * @param channel 需要更新的 Channel。
   */
  void updateChannel(Channel* channel) override;

  /**
   * @brief 通过 epoll_ctl DEL 将 Channel 从 epollfd 中移除。
   * @param channel 需要移除的 Channel。
   */
  void removeChannel(Channel* channel) override;

  /**
   * @brief 检查 Channel 是否已在 channels_ 映射中。
   * @param channel 要查询的 Channel。
   * @return 已注册返回 true，否则返回 false。
   */
  bool hasChannel(Channel* channel) const override;

 private:
  /** @brief epoll_event 列表的初始容量。 */
  static const int kInitEventListSize = 16;

  /**
   * @brief 将 epoll_wait 返回的就绪事件填入 activeChannels。
   * @param numEvents      epoll_wait 返回的就绪事件数量。
   * @param activeChannels 输出参数。
   */
  void fillActiveChannels(int numEvents, ChannelList* activeChannels) const;

  /**
   * @brief 封装 epoll_ctl，执行 ADD / MOD / DEL 操作。
   * @param operation EPOLL_CTL_ADD / EPOLL_CTL_MOD / EPOLL_CTL_DEL。
   * @param channel   目标 Channel。
   */
  void update(int operation, Channel* channel);

  /** @brief epoll 实例的文件描述符。 */
  int epollfd_;
  /** @brief epoll_wait 结果缓冲区，按需自动扩容。 */
  std::vector<epoll_event> events_;
};
