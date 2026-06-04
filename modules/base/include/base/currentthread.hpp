#pragma once

namespace net {

/**
 * @brief 当前线程相关的辅助函数。
 */
namespace CurrentThread {

/**
 * @brief 当前线程缓存的线程 ID。
 *
 * 每个线程拥有独立副本，首次调用 tid() 时由 cacheTid() 填充。
 */
extern thread_local int t_cachedTid;

/**
 * @brief 获取并缓存当前线程 ID。
 */
void cacheTid();

/**
 * @brief 返回当前线程 ID。
 * @return 当前线程缓存的 tid。
 */
inline int tid() {
  if (t_cachedTid == 0) [[unlikely]] {
    cacheTid();
  }
  return t_cachedTid;
}

}  // namespace CurrentThread

}  // namespace net
