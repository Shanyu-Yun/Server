#include "timestamp.hpp"

#include <chrono>
#include <ctime>
#include <format>

Timestamp::Timestamp() {
  microSecondsSinceEpoch_ = 0;
}

Timestamp::Timestamp(int64_t microSecondsSinceEpoch) {
  microSecondsSinceEpoch_ = microSecondsSinceEpoch;
}

Timestamp Timestamp::now() {
  auto now = std::chrono::system_clock::now();
  std::chrono::microseconds us =
      std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch());
  int64_t microSecondsSinceEpoch = us.count();
  return Timestamp(microSecondsSinceEpoch);
}

std::string Timestamp::toString() const {
  time_t seconds = static_cast<time_t>(microSecondsSinceEpoch_ / kMicroSecondsPerSecond);

  tm tm_time;
  localtime_r(&seconds, &tm_time);

  int year = tm_time.tm_year + 1900;
  int month = tm_time.tm_mon + 1;
  int day = tm_time.tm_mday;
  int hour = tm_time.tm_hour;
  int minute = tm_time.tm_min;
  int second = tm_time.tm_sec;

  std::string buf = std::format("{:04d}/{:02d}/{:02d} {:02d}:{:02d}:{:02d}", year, month, day, hour,
                                minute, second);
  return buf;
}
