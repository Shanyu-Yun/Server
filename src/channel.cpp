#include "channel.hpp"

#include "eventloop.hpp"

Channel::Channel(EventLoop* loop, int fd)
    : loop_(loop), fd_(fd), events_(0), revents_(0), index_(-1), tied_(false) {}

Channel::~Channel() {}

void Channel::update() {
  loop_->updateChannel(this);
}

void Channel::handleEvent(Timestamp receiveTime) {
  if (tied_) {
    // 尝试使用weak_ptr提升为shared_ptr，确保owner还存在
    std::shared_ptr<void> guard = tie_.lock();
    if (guard) {
      handleEventWithGuard(receiveTime);
    }
  } else {
    handleEventWithGuard(receiveTime);
  }
}

void Channel::remove() {
  loop_->removeChannel(this);
}

void Channel::tie(const std::shared_ptr<void>& owner) {
  tie_ = owner;
  tied_ = true;
}

void Channel::handleEventWithGuard(Timestamp receiveTime) {
  // 处理EPOLLHUP事件，且没有EPOLLIN事件，说明对端关闭了连接
  // EPOLLHUP：对端关闭连接，或者本端关闭写操作；
  // EPOLLIN：可读事件
  if ((revents_ & EPOLLHUP) && !(revents_ & EPOLLIN)) {
    if (closeCallback_) {
      closeCallback_();
    }
  }

  // 处理EPOLLERR事件，说明发生了错误
  // EPOLLERR：发生错误事件
  if (revents_ & EPOLLERR) {
    if (errorCallback_) {
      errorCallback_();
    }
  }

  // 处理可读事件
  // EPOLLIN：可读事件；
  // EPOLLPRI：紧急数据可读事件
  if (revents_ & (EPOLLIN | EPOLLPRI)) {
    if (readCallback_) {
      readCallback_(receiveTime);
    }
  }

  // 处理可写事件
  // EPOLLOUT：可写事件
  if (revents_ & EPOLLOUT) {
    if (writeCallback_) {
      writeCallback_();
    }
  }

  // 为什么如果发生了EPOLLHUP事件和EPOLLIN事件，就不调用closeCallback_？
  // 因为EPOLLHUP事件表示对端关闭了连接，但如果同时有EPOLLIN事件，说明还有数据可读，
  // 可能是对端先关闭了写操作但还没有完全关闭连接，所以先处理可读事件，等数据读完后再处理关闭事件。
}
