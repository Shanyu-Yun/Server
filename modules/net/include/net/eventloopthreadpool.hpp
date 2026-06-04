#pragma once
#include <memory>
#include <string>
#include <vector>

#include "net/eventloop.hpp"
#include "net/eventloopthread.hpp"

namespace net {

/**
 * @brief EventLoop 线程池，为多线程 Reactor 提供 IO 线程管理。
 *
 * 线程数为 0 时退化为单线程模式，所有连接在 baseLoop 上处理；
 * 否则创建 numThreads_ 个 EventLoopThread，每个线程持有独立的 EventLoop。
 */
class EventLoopThreadPool {
 public:
  /**
   * @brief 构造线程池，绑定 main loop。
   * @param baseLoop 主事件循环（TcpServer 所在线程的 loop）。
   * @param nameArg  线程池名称前缀，用于线程命名。
   */
  EventLoopThreadPool(EventLoop* baseLoop, const std::string& nameArg);

  /**
   * @brief 析构线程池。各 EventLoopThread 在析构时自动 join。
   */
  ~EventLoopThreadPool();

  /**
   * @brief 设置 IO 线程数量，必须在 start() 前调用。
   * @param numThreads 线程数，0 表示单线程（使用 baseLoop）。
   */
  void setThreadNum(int numThreads) {
    numThreads_ = numThreads;
  }

  /**
   * @brief 启动所有 IO 线程并等待每个线程的 EventLoop 就绪。
   * @param cb 每个 IO 线程启动时调用的初始化回调，可为空。
   */
  void start(const ThreadInitCallback& cb = ThreadInitCallback());

  /**
   * @brief 按轮询（round-robin）策略返回下一个 IO 线程的 EventLoop。
   *
   * 若线程数为 0，始终返回 baseLoop_。
   * @return 指向某个 IO 线程 EventLoop 的指针。
   */
  EventLoop* getNextLoop();

  /**
   * @brief 返回所有 IO 线程的 EventLoop 指针列表。
   *
   * 若线程数为 0，返回仅包含 baseLoop_ 的列表。
   * @return 所有 IO EventLoop 指针的向量。
   */
  std::vector<EventLoop*> getAllLoops();

 private:
  EventLoop* baseLoop_;                                    ///< 主事件循环（TcpServer 所在线程）。
  std::string name_;                                       ///< 线程名称前缀。
  bool started_;                                           ///< 是否已调用 start()。
  int numThreads_;                                         ///< 要创建的 IO 线程数量。
  int next_;                                               ///< 轮询下标，指向下一个要返回的 loop。
  std::vector<std::unique_ptr<EventLoopThread>> threads_;  ///< 管理所有 IO 线程对象的生命周期。
  std::vector<EventLoop*> loops_;                          ///< 所有 IO 线程的 EventLoop 指针，与 threads_ 一一对应。
};

}  // namespace net
