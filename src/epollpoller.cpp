#include "epollpoller.hpp"

#include <unistd.h>

#include <cerrno>
#include <cstdlib>

#include "logger.hpp"
#include "timestamp.hpp"

namespace {
constexpr int kNew = -1;     // Channel 未注册
constexpr int kAdded = 1;    // Channel 已在 epoll
constexpr int kDeleted = 2;  // Channel 曾在 epoll，已 DEL，但 channels_ 中仍存在
}  // namespace

// EPOLL_CLOEXEC：在调用exec()时自动关闭epollfd_，防止子进程继承该文件描述符
// exec()：在当前进程中执行一个新的程序，替换当前进程的映像、数据和堆栈等资源
EpollPoller::EpollPoller(EventLoop* loop)
    : Poller(loop), epollfd_(::epoll_create1(EPOLL_CLOEXEC)), events_(kInitEventListSize) {
  if (epollfd_ < 0) {
    LOGERROR("Failed to create epoll file descriptor");
    abort();
  }
}

EpollPoller::~EpollPoller() {
  ::close(epollfd_);
}

Timestamp EpollPoller::poll(int timeoutMs, ChannelList* activeChannels) {
  int numEvents =
      ::epoll_wait(epollfd_, events_.data(), static_cast<int>(events_.size()), timeoutMs);
  Timestamp now(Timestamp::now());
  if (numEvents > 0) {
    fillActiveChannels(numEvents, activeChannels);
    // 如果返回的事件数等于当前 events_ 的大小
    // 说明可能有更多事件未被捕获，需要扩大 events_ 的容量
    if (static_cast<size_t>(numEvents) == events_.size()) {
      events_.resize(events_.size() * 2);
    }
  } else if (numEvents == 0) {
    LOGINFO("EpollPoller::poll() timeout");
  } else {
    if (errno != EINTR) {
      LOGERROR("EpollPoller::poll() error: {:d}", errno);
    }
  }
  return now;
}

void EpollPoller::updateChannel(Channel* channel) {
  const int pollerState = channel->pollerState();
  const int fd = channel->fd();

  if (pollerState == kNew || pollerState == kDeleted) {
    if (pollerState == kNew) {
      channels_[fd] = channel;
    }
    channel->setPollerState(kAdded);
    update(EPOLL_CTL_ADD, channel);
  } else {
    if (channel->isNoneEvent()) {
      // 零时将channel从epoll中删除，但不从channels_中删除
      // 后续如果又有事件发生时再添加回epoll
      update(EPOLL_CTL_DEL, channel);
      channel->setPollerState(kDeleted);
    } else {
      update(EPOLL_CTL_MOD, channel);
    }
  }
}

void EpollPoller::removeChannel(Channel* channel) {
  const int fd = channel->fd();
  const int pollerState = channel->pollerState();
  channels_.erase(fd);

  if (pollerState == kAdded) {
    update(EPOLL_CTL_DEL, channel);
  }
  channel->setPollerState(kNew);
}

bool EpollPoller::hasChannel(Channel* channel) const {
  auto it = channels_.find(channel->fd());
  return it != channels_.end() && it->second == channel;
}

void EpollPoller::fillActiveChannels(int numEvents, ChannelList* activeChannels) const {
  for (int i = 0; i < numEvents; ++i) {
    Channel* channel = static_cast<Channel*>(events_[i].data.ptr);
    channel->setRevents(events_[i].events);
    activeChannels->push_back(channel);
  }
}

void EpollPoller::update(int operation, Channel* channel) {
  epoll_event event{};
  event.events = channel->events();
  event.data.ptr = channel;

  const int fd = channel->fd();
  if (::epoll_ctl(epollfd_, operation, fd, &event) < 0) {
    LOGERROR("EpollPoller::update() epoll_ctl operation {:d} fd {:d} error: {:d}", operation, fd,
             errno);
    if (operation != EPOLL_CTL_DEL) {
      abort();
    }
  }
}
