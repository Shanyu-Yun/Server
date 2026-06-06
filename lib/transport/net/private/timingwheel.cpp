#include "timingwheel.hpp"

#include "tcpconnection.hpp"

namespace transport {

TimingWheel::TimingWheel(EventLoop* loop, int timeoutSeconds)
    : loop_(loop), timeoutSeconds_(timeoutSeconds), buckets_(timeoutSeconds) {
  loop_->runEvery(1.0, [this] { onTimer(); });
}

void TimingWheel::onConnection(const TcpConnectionPtr& conn) {
  auto entry = std::make_shared<Entry>(conn);
  buckets_.back().insert(entry);
  // 存入内部 map，以 connName 为 key，不再占用 conn->context_
  connEntries_[conn->getName()] = WeakEntryPtr(entry);
}

void TimingWheel::onMessage(const TcpConnectionPtr& conn, Buffer*, Timestamp) {
  auto it = connEntries_.find(conn->getName());
  if (it == connEntries_.end()) return;
  auto entry = it->second.lock();
  if (entry) {
    buckets_.back().insert(entry);
  }
}

void TimingWheel::onClose(const TcpConnectionPtr& conn) {
  connEntries_.erase(conn->getName());
}

TimingWheel::~TimingWheel() {}

void TimingWheel::onTimer() {
  buckets_.push_back(Bucket());
  buckets_.pop_front();
}

TimingWheel::Entry::Entry(const std::weak_ptr<TcpConnection>& weakConn) : weakConn_(weakConn) {}

TimingWheel::Entry::~Entry() {
  auto conn = weakConn_.lock();
  if (conn) {
    conn->forceClose();
  }
}

}  // namespace transport
