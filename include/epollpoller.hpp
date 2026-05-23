#pragma once

#include <sys/epoll.h>

#include <vector>

#include "poller.hpp"
#include "timestamp.hpp"

class EpollPoller : public Poller {
 public:
  EpollPoller(EventLoop* loop);
  ~EpollPoller() override;

  Timestamp poll(int timeoutMs, ChannelList* activeChannels) override;
  void updateChannel(Channel* channel) override;
  void removeChannel(Channel* channel) override;
  bool hasChannel(Channel* channel) const override;

 private:
  static const int kInitEventListSize = 16;

  void fillActiveChannels(int numEvents, ChannelList* activeChannels) const;
  void update(int operation, Channel* channel);

  int epollfd_;
  std::vector<epoll_event> events_;
};
