#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <thread>

namespace net {

/**
 * @brief 对 std::thread 的轻量封装，补充线程命名、OS 级 tid 和全局计数。
 *
 * Thread 将线程的创建与启动分离：构造时只保存回调，调用 start() 后
 * 线程才真正运行。析构时若线程仍在运行且未被 join，会自动 detach。
 */
class Thread {
 public:
  /**
   * @brief 线程执行的任务类型。
   */
  using ThreadFunc = std::function<void()>;

  /**
   * @brief 构造一个 Thread，但不立即启动。
   * @param func 线程启动后执行的任务。
   * @param name 线程名称，用于日志和调试，可为空。
   */
  explicit Thread(ThreadFunc func, const std::string& name = std::string());

  /**
   * @brief 析构 Thread。
   *
   * 若线程已启动且未被 join，析构时自动 detach 以避免阻塞。
   */
  ~Thread();

  /**
   * @brief 启动线程，开始执行构造时传入的任务。
   */
  void start();

  /**
   * @brief 等待线程执行完毕。
   *
   * 线程未启动或已被 join 时记录错误日志。
   */
  void join();

  /**
   * @brief 判断线程是否已启动。
   */
  bool started() const {
    return started_;
  }

  /**
   * @brief 判断线程是否已被 join。
   */
  bool joined() const {
    return joined_;
  }

  /**
   * @brief 获取线程的 OS 级线程 ID（gettid 返回值）。
   *
   * 线程启动前返回 0。
   */
  pid_t tid() const {
    return tid_;
  }

  /**
   * @brief 获取线程名称。
   */
  std::string name() const {
    return name_;
  }

  /**
   * @brief 获取已创建的 Thread 对象总数。
   */
  static int numCreated() {
    return numCreated_.load();
  }

 private:
  bool started_;                         ///< 是否已调用 start()。
  bool joined_;                          ///< 是否已调用 join()。
  std::shared_ptr<std::thread> thread_;  ///< 底层 std::thread 对象。
  pid_t tid_;                            ///< 线程的 OS 级线程 ID，由 start() 在新线程内填充。
  ThreadFunc func_;                      ///< 线程执行的任务。
  std::string name_;                     ///< 线程名称。

  inline static std::atomic<int> numCreated_{0};  ///< 已创建的 Thread 对象总数。
};

}  // namespace net
