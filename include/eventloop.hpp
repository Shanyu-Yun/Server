#pragma once

#include <sched.h>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include "channel.hpp"
#include "curthread.hpp"
#include "poller.hpp"
#include "timestamp.hpp"

/**
 * @brief 事件循环，负责在所属线程中等待 IO 事件并分发回调。
 *
 * EventLoop 是 Reactor 模型中的核心对象。每个 EventLoop 通常只属于
 * 一个线程，它通过 Poller 等待活跃 Channel，再调用 Channel 分发对应
 * 的读、写、关闭、错误回调。
 *
 * EventLoop 还维护一个 pending functor 队列，用于支持其他线程向当前
 * loop 所属线程投递任务；跨线程投递后会通过 wakeupFd_ 唤醒正在阻塞
 * 在 poll/epoll_wait 中的事件循环。
 *
 * @note 该类不应该被拷贝，一个线程内通常也只应该创建一个 EventLoop。
 */
class EventLoop {
 public:
  /**
   * @brief 投递到 EventLoop 所属线程执行的任务类型。
   */
  using Functor = std::function<void()>;

  /**
   * @brief 创建一个绑定到当前线程的 EventLoop。
   */
  EventLoop();

  /**
   * @brief 析构 EventLoop，并释放 wakeup fd 等内部资源。
   */
  ~EventLoop();

  /**
   * @brief 启动事件循环。
   *
   * loop() 会持续通过 Poller 等待 IO 事件，依次处理活跃 Channel，
   * 然后执行 pending functor 队列，直到 quit() 被调用。
   */
  void loop();

  /**
   * @brief 请求退出事件循环。
   *
   * 如果从其他线程调用，会唤醒当前 EventLoop 所属线程，使 loop()
   * 能尽快从 poll/epoll_wait 中返回并观察到退出标志。
   */
  void quit();

  /**
   * @brief 在 EventLoop 所属线程中执行任务。
   *
   * 如果调用者就在 EventLoop 所属线程，任务会被立即执行；否则任务会
   * 被加入 pending functor 队列，并唤醒 EventLoop 等待后续执行。
   *
   * @param cb 待执行任务。
   */
  void runInLoop(const Functor& cb);

  /**
   * @brief 将任务加入 pending functor 队列。
   *
   * 该函数可以被其他线程调用。任务不会在调用点立即执行，而是在
   * EventLoop 所属线程的事件循环中统一执行。
   *
   * @param cb 待排队任务。
   */
  void queueInLoop(const Functor& cb);

  /**
   * @brief 更新 Channel 在 Poller 中的注册状态。
   *
   * @param channel 要更新的 Channel。
   */
  void updateChannel(Channel* channel);

  /**
   * @brief 从 Poller 中移除 Channel。
   * 
   * @param channel  要移除的 Channel。
   */
  void removeChannel(Channel* channel);

  /**
   * @brief 判断 Poller 中是否存在 Channel。
   * 
   * @param channel 要检查的 Channel。
   */
  bool hasChannel(Channel* channel) const;

  /**
   * @brief 判断当前调用线程是否为该 EventLoop 所属线程。
   */
  bool isInLoopThread() const {
    return threadId_ == CurrentThread::tid();
  }

 private:
  /**
   * @brief 唤醒可能阻塞在 Poller::poll() 中的事件循环。
   */
  void wakeup();

  /**
   * @brief 处理 wakeupFd_ 上的可读事件，清空唤醒计数。
   */
  void handleRead();

  /**
   * @brief 执行 pending functor 队列中的所有任务。
   */
  void doPendingFunctors();

 private:
  /**
   * @brief loop() 是否正在运行。
   */
  std::atomic<bool> looping_;

  /**
   * @brief 是否请求退出事件循环。
   */
  std::atomic<bool> quit_;

  /**
   * @brief 当前是否正在执行 pending functor 队列。
   */
  std::atomic<bool> callingPendingFunctors_;

  /**
   * @brief EventLoop 所属线程 ID。
   */
  const pid_t threadId_;

  /**
   * @brief 最近一次 Poller::poll() 返回的时间。
   */
  Timestamp pollReturnTime_;

  /**
   * @brief IO 多路复用抽象，默认由 Poller::newDefaultPoller() 创建。
   */
  std::unique_ptr<Poller> poller_;

  /**
   * @brief Poller 返回的活跃 Channel 列表。
   */
  Poller::ChannelList activeChannels_;

  /**
   * @brief 用于跨线程唤醒 EventLoop 的 eventfd。
   */
  int wakeupFd_;

  /**
   * @brief 负责把 wakeupFd_ 注册到 Poller 中的 Channel。
   */
  std::unique_ptr<Channel> wakeupChannel_;

  /**
   * @brief 保护 pendingFunctors_ 的互斥量。
   */
  std::mutex mutex_;

  /**
   * @brief 等待在 EventLoop 所属线程中执行的任务队列。
   */
  std::vector<Functor> pendingFunctors_;
};
