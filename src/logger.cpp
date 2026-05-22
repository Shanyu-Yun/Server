#include "logger.hpp"

#include <iostream>

#include "timestamp.hpp"

Logger& Logger::instance() {
  static Logger instance;
  return instance;
}

void Logger::setLogLevel(LogLevel level) {
  logLevel_ = level;
}

// 写日志【级别】时间戳 ：msg
void Logger::log(std::string msg) {
  switch (logLevel_) {
    case LogLevel::INFO:
      std::cout << "[INFO]";
      break;
    case LogLevel::DEBUG:
      std::cout << "[DEBUG]";
      break;
    case LogLevel::WARNING:
      std::cout << "[WARNING]";
      break;
    case LogLevel::ERROR:
      std::cout << "[ERROR]";
      break;
    default:
      break;
  }

  Timestamp ts;
  std::cout << ts.now().toString() << ":" << msg << std::endl;
}
