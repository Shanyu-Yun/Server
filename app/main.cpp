#include <iostream>

#include "base64.hpp"
#include "logger.hpp"
#include "sha1.hpp"
#include "eventloop.hpp"
#include "inetaddress.hpp"
#include "tcpserver.hpp"
using namespace net;

int main() {
  std::string msg = "shaSQ";
  std::array<uint8_t, 20> res = sha1(msg);

  for (auto b : res)
    printf("%02x", b);
  std::cout << std::endl;

  auto i = base64Encode(res);
  std::cout << i.size() << std::endl;
}