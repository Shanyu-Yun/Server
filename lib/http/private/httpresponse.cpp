#include "httpresponse.hpp"

#include "buffer.hpp"

namespace http {

HttpResponse::HttpResponse(bool closeConnection) : closeConnection_(closeConnection) {}

void HttpResponse::setStatusCode(HttpStatusCode code) {
  statusCode_ = code;
}

void HttpResponse::setStatusMessage(std::string msg) {
  statusMessage_ = msg;
}

void HttpResponse::setCloseConnection(bool on) {
  closeConnection_ = on;
}

bool HttpResponse::closeConnection() const {
  return closeConnection_;
}

void HttpResponse::addHeader(const std::string& key, const std::string& value) {
  headers_[key] = value;
}

void HttpResponse::setContentType(const std::string& type) {
  addHeader("Content-Type", type);
}

void HttpResponse::setBody(std::string body) {
  body_ = std::move(body);
}

void HttpResponse::setStreaming(bool on) {
  streaming_ = on;
}

void HttpResponse::appendToBuffer(net::Buffer* out) const {
  // 状态行
  out->append("HTTP/1.1 ");
  out->append(std::to_string(statusCode_));
  out->append(" ");
  if (!statusMessage_.empty()) {
    out->append(statusMessage_);
  } else {
    // 默认状态短语（务实子集，不追求 RFC 完整）
    switch (statusCode_) {
      case HttpStatusCode::k200Ok:
        out->append("OK");
        break;
      case HttpStatusCode::k400BadRequest:
        out->append("Bad Request");
        break;
      case HttpStatusCode::k404NotFound:
        out->append("Not Found");
        break;
      case HttpStatusCode::k413PayloadTooLarge:
        out->append("Payload Too Large");
        break;
      case HttpStatusCode::k500InternalServerError:
        out->append("Internal Server Error");
        break;
      default:
        out->append("Unknown");
        break;
    }
  }
  out->append("\r\n");

  // 各响应头
  for (const auto& header : headers_) {
    out->append(header.first);
    out->append(": ");
    out->append(header.second);
    out->append("\r\n");
  }

  // 自动补充 Content-Length
  // 如果streaming是true代表开启流式输出，不需要content-length
  if (!streaming_) {
    out->append("Content-Length: " + std::to_string(body_.size()) + "\r\n");
  }
  out->append("Connection: " + std::string(closeConnection_ ? "close" : "keep-alive") + "\r\n");
  out->append("\r\n");  // 空行：头部结束标志
  if (!streaming_) {
    out->append(body_);
  }
}
}  // namespace http