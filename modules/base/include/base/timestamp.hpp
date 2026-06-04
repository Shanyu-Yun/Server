#pragma once

#include <cstdint>
#include <string>

namespace net {

/**
 * @brief 微秒精度的时间戳。
 *
 * Timestamp 保存从 Unix Epoch 到当前时间点的微秒数，并提供获取当前时间
 * 和转换为字符串的接口。
 */
class Timestamp {
 public:
  /**
   * @brief 构造一个值为 0 的时间戳。
   */
  Timestamp();

  /**
   * @brief 根据微秒数构造时间戳。
   * @param microSecondsSinceEpoch 从 Unix Epoch 起经过的微秒数。
   */
  explicit Timestamp(int64_t microSecondsSinceEpoch);

  /**
   * @brief 获取当前时间。
   * @return 当前时间对应的 Timestamp。
   */
  static Timestamp now();

  /**
   * @brief 转换为可读字符串。
   * @return 格式化后的时间字符串。
   */
  std::string toString() const;

  /**
   * @brief 返回内部微秒数值。
   */
  int64_t microSecondsSinceEpoch() const {
    return microSecondsSinceEpoch_;
  }

  /**
   * @brief 时间戳是否有效（>0）。
   */
  bool valid() const {
    return microSecondsSinceEpoch_ > 0;
  }

  /**
   * @brief 返回一个无效时间戳（值为 0）。
   */
  static Timestamp invalid() {
    return Timestamp();
  }

  // 运算符重载
  inline bool operator<(const Timestamp& rhs) const {
    return microSecondsSinceEpoch_ < rhs.microSecondsSinceEpoch_;
  }

  inline bool operator==(const Timestamp& rhs) const {
    return microSecondsSinceEpoch_ == rhs.microSecondsSinceEpoch_;
  }

  inline bool operator!=(const Timestamp& rhs) const {
    return microSecondsSinceEpoch_ != rhs.microSecondsSinceEpoch_;
  }

  inline bool operator>(const Timestamp& rhs) const {
    return microSecondsSinceEpoch_ > rhs.microSecondsSinceEpoch_;
  }

  // 友元函数
  inline Timestamp operator+(int64_t microseconds) {
    return Timestamp(microSecondsSinceEpoch_ + microseconds);
  }

  inline Timestamp operator-(int64_t microseconds) {
    return Timestamp(microSecondsSinceEpoch_ - microseconds);
  }

 private:
  static constexpr int64_t kMicroSecondsPerSecond = 1000 * 1000;  ///< 每秒包含的微秒数。
  int64_t microSecondsSinceEpoch_;                                ///< 从 Unix Epoch 起经过的微秒数。
};

}  // namespace net
