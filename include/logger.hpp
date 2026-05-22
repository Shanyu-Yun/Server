#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <utility>

#include "noncopyable.hpp"

enum class LogLevel : int8_t { INFO = 0, WARNING = 1, ERROR = 2, DEBUG = 3 };

//一个单例日志类
class Logger : noncopyable {
 public:
  static Logger& instance();
  void setLogLevel(LogLevel level);
  void log(std::string msg);

 private:
  LogLevel logLevel_;
  Logger() {}
};

template <typename... Args>
void LOGINFO(std::format_string<Args...> fmt, Args&&... args) {
  Logger::instance().setLogLevel(LogLevel::INFO);
  Logger::instance().log(std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
void LOGWARNING(std::format_string<Args...> fmt, Args&&... args) {
  Logger::instance().setLogLevel(LogLevel::WARNING);
  Logger::instance().log(std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
void LOGERROR(std::format_string<Args...> fmt, Args&&... args) {
  Logger::instance().setLogLevel(LogLevel::ERROR);
  Logger::instance().log(std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
void LOGDEBUG(std::format_string<Args...> fmt, Args&&... args) {
  Logger::instance().setLogLevel(LogLevel::DEBUG);
  Logger::instance().log(std::format(fmt, std::forward<Args>(args)...));
}
