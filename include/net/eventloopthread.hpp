#pragma once
#include <condition_variable>
#include <functional>
#include <mutex>

#include "base/thread.hpp"
#include "net/eventloop.hpp"

namespace tinynet {

/**
 * @brief EventLoop 线程启动时执行的初始化回调。
 *
 * 参数为该线程的 EventLoop 指针，可用于在 loop 启动前注册定时器等资源。
 */
using ThreadInitCallback = std::function<void(EventLoop*)>;

/**
 * @brief 在独立线程中创建并运行一个 EventLoop。
 *
 * startLoop() 同步等待后台线程的 EventLoop 准备就绪后返回其指针，
 * 调用方可以立即向该 loop 提交任务。
 */
class EventLoopThread {
 public:
  /**
   * @brief 构造 EventLoopThread，但不立即启动线程。
   * @param cb   线程启动后、loop 运行前调用的初始化回调，可为空。
   * @param name 线程名称，用于日志和调试，可为空。
   */
  EventLoopThread(const ThreadInitCallback& cb = ThreadInitCallback(), const std::string& name = std::string());

  /**
   * @brief 析构 EventLoopThread。
   *
   * 若线程已启动，通过 loop_->quit() 通知后台线程退出，并等待 join。
   */
  ~EventLoopThread();

  /**
   * @brief 启动后台线程并阻塞等待其 EventLoop 就绪。
   * @return 后台线程中已运行的 EventLoop 指针，生命周期由该对象管理。
   */
  EventLoop* startLoop();

 private:
  /**
   * @brief 后台线程的入口函数，创建 EventLoop 并通知 startLoop 返回。
   */
  void threadFunc();

  EventLoop* loop_;                  ///< 后台线程中创建的 EventLoop，由 threadFunc 填充。
  bool exiting_;                     ///< 析构时是否已请求退出。
  Thread thread_;                    ///< 后台线程的 RAII 封装。
  std::mutex mutex_;                 ///< 保护 loop_ 可见性的互斥锁。
  std::condition_variable condVar_;  ///< startLoop 等待 loop_ 就绪的条件变量。
  ThreadInitCallback callback_;      ///< loop 运行前的初始化回调。
};

}  // namespace tinynet
