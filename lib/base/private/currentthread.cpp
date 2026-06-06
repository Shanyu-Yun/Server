#include "currentthread.hpp"

#include <sys/syscall.h>
#include <unistd.h>

namespace net {

namespace CurrentThread {

thread_local int t_cachedTid = 0;

void cacheTid() {
  t_cachedTid = static_cast<int>(::syscall(SYS_gettid));
}

}  // namespace CurrentThread

}  // namespace net
