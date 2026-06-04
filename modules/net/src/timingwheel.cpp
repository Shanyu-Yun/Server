#include "net/timingwheel.hpp"

#include "net/tcpconnection.hpp"

namespace net {

TimingWheel::TimingWheel(EventLoop* loop, int timeoutSeconds)
    : loop_(loop), timeoutSeconds_(timeoutSeconds), buckets_(timeoutSeconds) {
  // 注册每秒触发一次的定时器，驱动时间轮向前滑动
  loop_->runEvery(1.0, [this] { onTimer(); });
}

void TimingWheel::onConnection(const TcpConnectionPtr& conn) {
  auto entry = std::make_shared<Entry>(conn);
  buckets_.back().insert(entry);
  // 将 weak_ptr<Entry> 挂载到连接的 context_，供 onMessage 取用
  conn->setContext(std::weak_ptr<Entry>(entry));
}

void TimingWheel::onMessage(const TcpConnectionPtr& conn, Buffer* buf, Timestamp receiveTime) {
  auto& weakEntry = std::any_cast<std::weak_ptr<Entry>&>(*conn->getMutableContext());
  auto entry = weakEntry.lock();
  if (entry) {
    // 重新插入最新格：use_count+1，旧格弹出时不归零，超时计时从此刻重新开始
    buckets_.back().insert(entry);
  }
}

TimingWheel::~TimingWheel() {}

void TimingWheel::onTimer() {
  // push_back 新空格后 pop_front：引用计数归零的 Entry 在此处析构并关闭连接
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

}  // namespace net