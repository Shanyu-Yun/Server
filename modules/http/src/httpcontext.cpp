#include "http/httpcontext.hpp"

#include <algorithm>

namespace http {
void HttpContext::reset() {
  state_ = kExpectRequestLine;
  contentLength_ = 0;

  request_.reset();
}

bool HttpContext::wantBody() const {
  return contentLength_ > 0;
}

bool HttpContext::parseRequestLine(const char* begin, const char* end) {
  // 找到第一个空格的位置
  const char* space = std::find(begin, end, ' ');
  if (space == end)
    return false;
  if (!request_.setMethod(begin, space))
    return false;

  // 跳过空格，找第二个空格
  begin = space + 1;
  space = std::find(begin, end, ' ');
  if (space == end)
    return false;

  // path里可能含“？”
  const char* question = std::find(begin, space, '?');
  if (question != space) {
    request_.setPath(begin, question);
  }
}
}  // namespace http