#pragma once

namespace transport {

/**
 * @brief 禁止派生类拷贝和赋值的基类。
 *
 * 继承 noncopyable 的类可以正常构造和析构，但不能拷贝构造或拷贝赋值。
 */
class noncopyable {
 public:
  /**
   * @brief 禁用拷贝构造。
   */
  noncopyable(const noncopyable&) = delete;

  /**
   * @brief 禁用拷贝赋值。
   */
  void operator=(const noncopyable&) = delete;

 protected:
  /**
   * @brief 允许派生类默认构造。
   */
  noncopyable() = default;

  /**
   * @brief 允许派生类析构。
   */
  ~noncopyable() = default;
};

}  // namespace transport
