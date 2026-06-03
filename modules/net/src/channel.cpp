#include "net/channel.hpp"

#include "net/eventloop.hpp"

namespace tinynet {

Channel::Channel(EventLoop* loop, int fd)
    : loop_(loop), fd_(fd), events_(0), revents_(0), pollerState_(-1), tied_(false) {}

Channel::~Channel() {}

void Channel::update() {
  loop_->updateChannel(this);
}
void Channel::remove() {
  loop_->removeChannel(this);
}

void Channel::tie(const std::shared_ptr<void>& owner) {
  tie_ = owner;
  tied_ = true;
}

void Channel::handleEvent(Timestamp receiveTime) {
  if (tied_) {
    std::shared_ptr<void> guard = tie_.lock();
    if (guard)
      handleEventWithGuard(receiveTime);
  } else {
    handleEventWithGuard(receiveTime);
  }
}

void Channel::handleEventWithGuard(Timestamp receiveTime) {
  if ((revents_ & EPOLLHUP) && !(revents_ & EPOLLIN)) {
    if (closeCallback_)
      closeCallback_();
  }
  if (revents_ & EPOLLERR) {
    if (errorCallback_)
      errorCallback_();
  }
  if (revents_ & (EPOLLIN | EPOLLPRI)) {
    if (readCallback_)
      readCallback_(receiveTime);
  }
  if (revents_ & EPOLLOUT) {
    if (writeCallback_)
      writeCallback_();
  }
}

}  // namespace tinynet
