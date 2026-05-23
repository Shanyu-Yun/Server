#include "eventloop.hpp"

#include <sys/eventfd.h>
#include <unistd.h>

#include <cstdlib>

#include "logger.hpp"

// poll timeout in milliseconds
namespace {
constexpr int kPollTimeMs = 10000;
}

EventLoop::EventLoop()
    : looping_(false),
      quit_(false),
      callingPendingFunctors_(false),
      threadId_(CurrentThread::tid()),
      poller_(Poller::newDefaultPoller(this)) {
  // 创建wakeupFd_，并将其注册到poller_中
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
  quit_ = false;

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
  // 如果不是在当前线程调用 quit()，则需要唤醒 loop() 以退出循环
  if (!isInLoopThread()) {
    wakeup();
  }
}

void EventLoop::runInLoop(const Functor& cb) {
  // 如果在当前线程调用 runInLoop()，则直接执行回调；否则将回调加入队列，并唤醒 loop() 以执行回调
  if (isInLoopThread()) {
    cb();
  } else {
    queueInLoop(cb);
  }
}

void EventLoop::queueInLoop(const Functor& cb) {
  {
    // 将回调加入队列
    std::lock_guard<std::mutex> lock(mutex_);
    pendingFunctors_.push_back(cb);
  }

  // 如果不是在当前线程调用 queueInLoop()，或者正在调用 pending functors，
  // 则需要唤醒 loop() 以执行回调
  if (!isInLoopThread() || callingPendingFunctors_) {
    wakeup();
  }
}

void EventLoop::updateChannel(Channel* channel) {
  poller_->updateChannel(channel);
}

void EventLoop::removeChannel(Channel* channel) {
  poller_->removeChannel(channel);
}

bool EventLoop::hasChannel(Channel* channel) const {
  if (!isInLoopThread()) {
    LOGERROR("EventLoop::hasChannel() called from wrong thread");
    abort();
  }
  return poller_->hasChannel(channel);
}

void EventLoop::wakeup() {
  uint64_t one = 1;
  // 这里为什么要写8字节呢，
  // 因为 wakeupFd_ 是用 eventfd() 创建出来的，“唤醒方式”就是：往这个 fd 写入 8 字节整数，让它变成可读，从而唤醒 epoll_wait。
  ssize_t n = ::write(wakeupFd_, &one, sizeof(one));
  if (n != sizeof(one)) {
    // 写入失败，记录日志
    LOGERROR("EventLoop::wakeup() writes {:d} bytes instead of 8", n);
  }
}

void EventLoop::handleRead() {
  uint64_t one = 1;
  ssize_t n = ::read(wakeupFd_, &one, sizeof(one));
  if (n != sizeof(one)) {
    // 读取失败，记录日志
    LOGERROR("EventLoop::handleRead() reads {:d} bytes instead of 8", n);
  }
}

void EventLoop::doPendingFunctors() {
  std::vector<Functor> functors;
  callingPendingFunctors_ = true;
  {
    // 交换 pendingFunctors_ 和 functors，减少锁的持有时间
    // 不使用交换直接遍历pendingFunctors_，那么就需要长时间持有锁
    // pendingFunctors_中的回调可能会很耗时
    std::lock_guard<std::mutex> lock(mutex_);
    functors.swap(pendingFunctors_);
  }

  // 执行回调
  for (const Functor& functor : functors) {
    functor();
  }

  callingPendingFunctors_ = false;
}