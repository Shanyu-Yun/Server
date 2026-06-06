#include "httprequest.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>

#include "timestamp.hpp"

namespace http {

void HttpRequest::setPath(const char* start, const char* end) {
  path_.assign(start, end);
}

void HttpRequest::setQuery(const char* start, const char* end) {
  query_.assign(start, end);
}

void HttpRequest::setVersion(Version v) {
  version_ = v;
}

void HttpRequest::appendBody(const char* data, size_t len) {
  body_.append(data, len);
}

void HttpRequest::setReceiveTime(net::Timestamp t) {
  receiveTime_ = t;
}

void HttpRequest::reset() {
  method_ = Method::kInvalid;
  version_ = Version::kUnknown;
  path_.clear();
  query_.clear();
  body_.clear();
  headers_.clear();
  receiveTime_ = net::Timestamp();
}

bool HttpRequest::setMethod(const char* start, const char* end) {
  std::string m(start, end);
  if (m == "GET")
    method_ = Method::kGet;
  else if (m == "POST")
    method_ = Method::kPost;
  else if (m == "HEAD")
    method_ = Method::kHead;
  else if (m == "PUT")
    method_ = Method::kPut;
  else if (m == "DELETE")
    method_ = Method::kDelete;
  else
    return false;
  return true;
}

void HttpRequest::addHeader(const char* start, const char* colon, const char* end) {
  // key:[start,colon]全部转小写
  std::string key(start, colon);
  std::transform(key.begin(), key.end(), key.begin(), ::tolower);

  // 修建多余的空格和转义字符
  const char* vbegin = colon + 1;
  while (vbegin < end && *vbegin == ' ') {
    ++vbegin;
  }
  const char* vend = end;
  while (vend > vbegin && (*(vend - 1) == ' ' || *(vend - 1) == '\r')) {
    --vend;
  }

  headers_[key] = std::string(vbegin, vend);
}

std::string HttpRequest::getHeader(const std::string& field) const {
  std::string lower = field;
  std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
  auto it = headers_.find(lower);
  return it != headers_.end() ? it->second : "";
}

bool HttpRequest::keepAlive() const {
  std::string conn = getHeader("connection");
  if (conn == "close")
    return false;
  if (version_ == Version::kHttp11)
    return true;
  return conn == "keep-alive";
}

}  // namespace http
