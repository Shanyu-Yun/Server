#include "base/thread.hpp"

#include "base/currentthread.hpp"
#include "base/logger.hpp"

Thread::Thread(ThreadFunc func, const std::string& name)
    : started_(false),
      joined_(false),
      thread_(nullptr),
      tid_(0),
      func_(std::move(func)),
      name_(name) {
  numCreated_.fetch_add(1);
}

Thread::~Thread() {
  if (started_ && !joined_) {
    thread_->detach();
  }
}

void Thread::start() {
  started_ = true;
  thread_ = std::make_shared<std::thread>([this]() {
    tid_ = CurrentThread::tid();
    func_();
  });
}

void Thread::join() {
  if (started_ && !joined_) {
    thread_->join();
    joined_ = true;
  } else {
    LOGERROR("Thread::join() called on thread that is not started or already joined");
  }
}
