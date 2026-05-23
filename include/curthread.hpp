#pragma once

namespace CurrentThread {

extern thread_local int t_cachedTid;

void cacheTid();

inline int tid() {
  if (t_cachedTid == 0) [[unlikely]] {
    cacheTid();
  }
  return t_cachedTid;
}

}  // namespace CurrentThread