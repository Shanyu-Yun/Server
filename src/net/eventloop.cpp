#include "net/eventloop.hpp"

#include <sys/eventfd.h>
#include <unistd.h>

#include "base/logger.hpp"

namespace {
constexpr int kPollTimeMs = 10000;
}

EventLoop::EventLoop()
    : looping_(false), quit_(false), callingPendingFunctors_(false),
      threadId_(CurrentThread::tid()),
      poller_(Poller::newDefaultPoller(this)) {
  wakeupFd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (wakeupFd_ < 0) {
    LOGERROR("Failed to create eventfd");
    abort();
  }
  wakeupChannel_ = std::make_unique<Channel>(this, wakeupFd_);
  wakeupChannel_->setReadCallback(std::bind(&EventLoop::handleRead, this));
  wakeupChannel_->enableReading();
}

EventLoop::~EventLoop() {
  if (looping_) {
    LOGERROR("EventLoop::~EventLoop() EventLoop still looping");
    abort();
  }
  wakeupChannel_->disableAll();
  wakeupChannel_->remove();
  ::close(wakeupFd_);
}

void EventLoop::loop() {
  looping_ = true;
  quit_    = false;
  while (!quit_) {
    activeChannels_.clear();
    pollReturnTime_ = poller_->poll(kPollTimeMs, &activeChannels_);
    for (Channel* channel : activeChannels_) {
      channel->handleEvent(pollReturnTime_);
    }
    doPendingFunctors();
  }
  LOGINFO("EventLoop stop looping");
  looping_ = false;
}

void EventLoop::quit() {
  quit_ = true;
  if (!isInLoopThread()) wakeup();
}

void EventLoop::runInLoop(const Functor& cb) {
  if (isInLoopThread()) cb();
  else queueInLoop(cb);
}

void EventLoop::queueInLoop(const Functor& cb) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    pendingFunctors_.push_back(cb);
  }
  if (!isInLoopThread() || callingPendingFunctors_) wakeup();
}

void EventLoop::updateChannel(Channel* channel) { poller_->updateChannel(channel); }
void EventLoop::removeChannel(Channel* channel) { poller_->removeChannel(channel); }

bool EventLoop::hasChannel(Channel* channel) const {
  if (!isInLoopThread()) {
    LOGERROR("EventLoop::hasChannel() called from wrong thread");
    abort();
  }
  return poller_->hasChannel(channel);
}

void EventLoop::wakeup() {
  uint64_t one = 1;
  ssize_t n = ::write(wakeupFd_, &one, sizeof(one));
  if (n != sizeof(one)) LOGERROR("EventLoop::wakeup() writes {:d} bytes instead of 8", n);
}

void EventLoop::handleRead() {
  uint64_t one = 1;
  ssize_t n = ::read(wakeupFd_, &one, sizeof(one));
  if (n != sizeof(one)) LOGERROR("EventLoop::handleRead() reads {:d} bytes instead of 8", n);
}

void EventLoop::doPendingFunctors() {
  std::vector<Functor> functors;
  callingPendingFunctors_ = true;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    functors.swap(pendingFunctors_);
  }
  for (const Functor& functor : functors) functor();
  callingPendingFunctors_ = false;
}
