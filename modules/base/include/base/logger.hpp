#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <utility>

#include "base/noncopyable.hpp"

namespace net {

/**
 * @brief 日志级别。
 */
enum class LogLevel : int8_t { INFO = 0, WARNING = 1, ERROR = 2, DEBUG = 3 };

/**
 * @brief 简单的单例日志类。
 *
 * Logger 负责保存当前日志级别，并将格式化后的日志消息输出到标准输出
 * 或标准错误。拷贝被禁用，调用方通过 instance() 获取全局实例。
 */
class Logger : noncopyable {
 public:
  /**
   * @brief 获取全局 Logger 实例。
   * @return Logger 单例引用。
   */
  static Logger& instance();

  /**
   * @brief 设置当前日志级别。
   * @param level 要使用的日志级别。
   */
  void setLogLevel(LogLevel level);

  /**
   * @brief 输出一条日志消息。
   * @param msg 已格式化的日志正文。
   */
  void log(std::string msg);

 private:
  LogLevel logLevel_;  ///< 当前日志级别。

  /**
   * @brief 构造 Logger 单例。
   */
  Logger() {}
};

/**
 * @brief 输出 INFO 级别日志。
 */
template <typename... Args>
void LOGINFO(std::format_string<Args...> fmt, Args&&... args) {
  Logger::instance().setLogLevel(LogLevel::INFO);
  Logger::instance().log(std::format(fmt, std::forward<Args>(args)...));
}

/**
 * @brief 输出 WARNING 级别日志。
 */
template <typename... Args>
void LOGWARNING(std::format_string<Args...> fmt, Args&&... args) {
  Logger::instance().setLogLevel(LogLevel::WARNING);
  Logger::instance().log(std::format(fmt, std::forward<Args>(args)...));
}

/**
 * @brief 输出 ERROR 级别日志。
 */
template <typename... Args>
void LOGERROR(std::format_string<Args...> fmt, Args&&... args) {
  Logger::instance().setLogLevel(LogLevel::ERROR);
  Logger::instance().log(std::format(fmt, std::forward<Args>(args)...));
}

/**
 * @brief 输出 DEBUG 级别日志。
 */
template <typename... Args>
void LOGDEBUG(std::format_string<Args...> fmt, Args&&... args) {
  Logger::instance().setLogLevel(LogLevel::DEBUG);
  Logger::instance().log(std::format(fmt, std::forward<Args>(args)...));
}

}  // namespace net
