#include "logger.hpp"

#include <iostream>

#include "timestamp.hpp"

namespace transport {

Logger& Logger::instance() {
  static Logger instance;
  return instance;
}

void Logger::setLogLevel(LogLevel level) {
  logLevel_ = level;
}

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
  std::cout << Timestamp::now().toString() << ":" << msg << std::endl;
}

}  // namespace transport
