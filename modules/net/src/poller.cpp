#include "net/poller.hpp"

#include "net/epollpoller.hpp"

namespace net {

Poller::Poller(EventLoop* loop) : ownerLoop_(loop) {}

Poller* Poller::newDefaultPoller(EventLoop* loop) {
  return new EpollPoller(loop);
}

}  // namespace net
