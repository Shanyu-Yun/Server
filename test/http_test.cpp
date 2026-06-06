#include <cassert>
#include <cstring>
#include <string>

#include "httprequest.hpp"
int main() {
  http::HttpRequest req;
  req.setMethod("GET", "GET" + 3);  // → kGet
  // start/colon/end 必须在同一段内存里
  const char* line = "Host: example.com";
  const char* colon = line + 4;              // 指向 ':'
  const char* end = line + strlen(line);
  req.addHeader(line, colon, end);
  assert(req.getHeader("host") == "example.com");  // 去空白 + 小写
  assert(req.getHeader("HOST") == "example.com");  // 大写查也对
}