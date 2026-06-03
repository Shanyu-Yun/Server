#include "http/httprequest.hpp"

namespace http {
bool HttpRequest::setMethod(const char* start, const char* end) {
  path_.assign(start, end);
}
}  // namespace http
