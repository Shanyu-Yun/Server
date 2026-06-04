#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <thread>
#include <vector>

#include "net/eventloop.hpp"

// ──────────────────── 主函数 ────────────────────
int main() {
  net::EventLoop loop;
  loop.runAfter(1.0, []() { std::cout << "Timer 1 fired after 1 second\n"; });
  loop.runAfter(2.0, []() { std::cout << "Timer 2 fired after 2 seconds\n"; });
  loop.runEvery(3.0, []() { std::cout << "Timer 3 fired every 3 seconds\n"; });
  loop.loop();
}
