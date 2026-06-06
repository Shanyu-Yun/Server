#include "httpcontext.hpp"

#include <algorithm>

#include "buffer.hpp"
#include "timestamp.hpp"
#include "httprequest.hpp"

namespace protocol {
void HttpContext::reset() {
  state_ = kExpectRequestLine;
  contentLength_ = 0;

  request_.reset();
}

bool HttpContext::parseRequestLine(const char* begin, const char* end) {
  const char* space = std::find(begin, end, ' ');
  if (space == end || !request_.setMethod(begin, space)) {
    return false;
  }

  const char* nextspace = std::find(space + 1, end, ' ');
  if (nextspace == end) {
    return false;
  }
  const char* question = std::find(space + 1, nextspace, '?');
  if (question != nextspace) {
    request_.setPath(space + 1, question);
    request_.setQuery(question + 1, nextspace);
  } else {
    request_.setPath(space + 1, nextspace);
  }

  std::string version(nextspace + 1, end);
  if (version == "HTTP/1.1") {
    request_.setVersion(Version::kHttp11);
  } else if (version == "HTTP/1.0") {
    request_.setVersion(Version::kHttp10);
  } else {
    request_.setVersion(Version::kUnknown);
    return false;
  }
  return true;
}

bool HttpContext::wantBody() const {
  return contentLength_ > 0;
}

bool HttpContext::parse(transport::Buffer* buf, transport::Timestamp receiveTime) {
  bool ok = true;
  bool hasMore = true;
  while (hasMore) {
    switch (state_) {
      case kExpectRequestLine: {
        const char* crlf = buf->findCRLF();
        if (crlf) {
          ok = parseRequestLine(buf->peek(), crlf);
          if (ok) {
            request_.setReceiveTime(receiveTime);
            buf->consumeUntil(crlf + 2);
            state_ = kExpectHeaders;
          } else {
            state_ = kError;
            hasMore = false;
          }
        } else {
          hasMore = false;  // 半行，等下次
        }
        break;
      }
      case kExpectHeaders: {
        const char* crlf = buf->findCRLF();
        if (!crlf) {
          hasMore = false;  // 半行，等下次
          break;
        }
        if (crlf == buf->peek()) {
          // 空行 → 头部结束，提取 Content-Length 决定是否有 body
          std::string len = request_.getHeader("content-length");
          if (!len.empty()) {
            contentLength_ = std::stol(len);
          }
          if (wantBody()) {
            state_ = kExpectBody;
          } else {
            state_ = kGotAll;
            hasMore = false;
          }
        } else {
          // 普通 header 行，找 ':' 拆 key/value 填进 request_
          const char* colon = std::find(buf->peek(), crlf, ':');
          if (colon != crlf) {
            request_.addHeader(buf->peek(), colon, crlf);
          }
        }
        buf->consumeUntil(crlf + 2);  // 空行和 header 行都要消费
        break;
      }
      case kExpectBody: {
        if (buf->readableBytes() < static_cast<size_t>(contentLength_)) {
          hasMore = false;  // body 没到齐，等下次
        } else {
          request_.appendBody(buf->peek(), contentLength_);
          buf->consume(contentLength_);
          state_ = kGotAll;
          hasMore = false;
        }
        break;
      }
      case kGotAll: {
        hasMore = false;
        break;
      }
      case kError: {
        ok = false;
        hasMore = false;
        break;
      }
      default: {
        hasMore = false;
        break;
      }
    }
  }
  return ok;
}

}  // namespace protocol