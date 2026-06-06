#include "timestamp.hpp"

#include <chrono>
#include <ctime>
#include <format>

namespace net {

Timestamp::Timestamp() : microSecondsSinceEpoch_(0) {}

Timestamp::Timestamp(int64_t microSecondsSinceEpoch)
    : microSecondsSinceEpoch_(microSecondsSinceEpoch) {}

Timestamp Timestamp::now() {
  auto now = std::chrono::system_clock::now();
  auto us = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch());
  return Timestamp(us.count());
}

std::string Timestamp::toString() const {
  time_t seconds = static_cast<time_t>(microSecondsSinceEpoch_ / kMicroSecondsPerSecond);
  tm tm_time;
  localtime_r(&seconds, &tm_time);
  return std::format("{:04d}/{:02d}/{:02d} {:02d}:{:02d}:{:02d}", tm_time.tm_year + 1900,
                     tm_time.tm_mon + 1, tm_time.tm_mday, tm_time.tm_hour, tm_time.tm_min,
                     tm_time.tm_sec);
}

}  // namespace net
