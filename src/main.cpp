#include <iostream>

#include "logger.hpp"

// #include "timestamp.hpp"

int main() {
  // Timestamp ts;
  // std::cout << ts.now().toString() << std::endl;
  LOGINFO("hello {:02d}", 11);
  return 0;
}
