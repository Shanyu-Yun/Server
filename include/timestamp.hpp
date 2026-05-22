#pragma once

#include <cstdint>
#include <string>

class Timestamp {
 public:
  Timestamp();
  explicit Timestamp(int64_t microSecondsSinceEpoch);
  static Timestamp now();
  std::string toString() const;

 private:
  static constexpr int64_t kMicroSecondsPerSecond = 1000 * 1000;

  int64_t microSecondsSinceEpoch_;
};
