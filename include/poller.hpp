#pragma once
#include <unordered_map>
#include <vector>

#include "channel.hpp"
#include "timestamp.hpp"

class EventLoop;

class Poller {
 public:
  using ChannelList = std::vector<Channel*>;

  Poller(EventLoop* loop);
  virtual ~Poller() = default;

  virtual Timestamp poll(int timeoutMs, ChannelList* activeChannels) = 0;
  virtual void updateChannel(Channel* channel) = 0;
  virtual void removeChannel(Channel* channel) = 0;
  virtual bool hasChannel(Channel* channel) const = 0;
  static Poller* newDefaultPoller(EventLoop* loop);

 protected:
  std::unordered_map<int, Channel*> channels_;

 private:
  EventLoop* ownerLoop_;
};
