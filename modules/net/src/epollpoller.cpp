#include "net/epollpoller.hpp"

#include <unistd.h>

#include <cerrno>
#include <cstdlib>

#include "base/logger.hpp"
#include "base/timestamp.hpp"

namespace net {

namespace {
constexpr int kNew = -1;
constexpr int kAdded = 1;
constexpr int kDeleted = 2;
}  // namespace

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
    if (static_cast<size_t>(numEvents) == events_.size())
      events_.resize(events_.size() * 2);
  } else if (numEvents == 0) {
    LOGINFO("EpollPoller::poll() timeout");
  } else {
    if (errno != EINTR)
      LOGERROR("EpollPoller::poll() error: {:d}", errno);
  }
  return now;
}

void EpollPoller::updateChannel(Channel* channel) {
  const int pollerState = channel->pollerState();
  const int fd = channel->fd();

  if (pollerState == kNew || pollerState == kDeleted) {
    if (pollerState == kNew)
      channels_[fd] = channel;
    channel->setPollerState(kAdded);
    update(EPOLL_CTL_ADD, channel);
  } else {
    if (channel->isNoneEvent()) {
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
  if (pollerState == kAdded)
    update(EPOLL_CTL_DEL, channel);
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
    LOGERROR("EpollPoller::update() epoll_ctl op={:d} fd={:d} err={:d}", operation, fd, errno);
    if (operation != EPOLL_CTL_DEL)
      abort();
  }
}

}  // namespace net
