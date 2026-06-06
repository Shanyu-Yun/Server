#include "eventloopthreadpool.hpp"

namespace net {

EventLoopThreadPool::EventLoopThreadPool(EventLoop* baseLoop, const std::string& name)
    : baseLoop_(baseLoop), name_(name), started_(false), numThreads_(0), next_(0) {}

EventLoopThreadPool::~EventLoopThreadPool() {}

void EventLoopThreadPool::start(const ThreadInitCallback& cb) {
  started_ = true;
  for (int i = 0; i < numThreads_; ++i) {
    std::string threadName = name_ + std::to_string(i);
    auto* t = new EventLoopThread(cb, threadName);
    threads_.emplace_back(t);
    loops_.push_back(t->startLoop());
  }
}

EventLoop* EventLoopThreadPool::getNextLoop() {
  EventLoop* loop = baseLoop_;
  if (!loops_.empty()) {
    loop = loops_[next_];
    if (++next_ >= static_cast<int>(loops_.size()))
      next_ = 0;
  }
  return loop;
}

std::vector<EventLoop*> EventLoopThreadPool::getAllLoops() {
  if (loops_.empty())
    return {baseLoop_};
  return loops_;
}

}  // namespace net
