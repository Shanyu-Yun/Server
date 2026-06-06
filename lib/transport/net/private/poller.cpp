#include "poller.hpp"

#include "epollpoller.hpp"

namespace transport {

Poller::Poller(EventLoop* loop) : ownerLoop_(loop) {}

Poller* Poller::newDefaultPoller(EventLoop* loop) {
  return new EpollPoller(loop);
}

}  // namespace transport
