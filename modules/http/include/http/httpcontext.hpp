#pragma once
#include "base/timestamp.hpp"
#include "http/httprequest.hpp"

namespace http {

class Buffer;  // 仅用作指针参数，前置声明即可。

/**
 * @brief 每连接的 HTTP 解析状态机。
 *
 * 持有半成品 HttpRequest 与解析进度。它活在 TcpConnection 的 context_ 中，
 * 故一个请求拆成多次 TCP 段到达也不丢进度，从而实现增量解析。
 */
class HttpContext {
 public:
  /**
   * @brief 解析状态。
   */
  enum HttpRequestParseState {
    kExpectRequestLine,  ///< 等待请求行。
    kExpectHeaders,      ///< 等待请求头。
    kExpectBody,         ///< 等待请求体。
    kGotAll,             ///< 已集齐一个完整请求。
    kError,              ///< 解析出错（畸形请求）。
  };

  /**
   * @brief 尽可能多地推进状态机，直到数据不够或集齐一个请求。
   *
   * 返回值只表达对错：畸形返回 false；返回 true 但 gotAll() 为 false 表示
   * “数据还不够，等下次 onMessage”。已消费的字节会从 buf 中 retrieve。
   * @param buf         输入缓冲区。
   * @param receiveTime 本次数据到达时间。
   * @return 解析是否正常（未遇到畸形）。
   */
  bool parse(Buffer* buf, tinynet::Timestamp receiveTime);

  /** @brief 是否已集齐一个完整请求。 */
  bool gotAll() const {
    return state_ == kGotAll;
  }

  /**
   * @brief 复位状态机，供 keep-alive 下解析同一连接上的下一个请求。
   */
  void reset();

  /** @brief 只读访问解析出的请求。 */
  const HttpRequest& request() const {
    return request_;
  }

  /** @brief 可写访问解析中的请求。 */
  HttpRequest& request() {
    return request_;
  }

 private:
  /**
   * @brief 解析请求行，拆出 method/path/query/version。
   * @param begin 行起始。
   * @param end   行结束（不含 CRLF）。
   * @return 是否解析成功。
   */
  bool parseRequestLine(const char* begin, const char* end);

  /**
   * @brief 由 Content-Length 判断本请求是否带 body。
   */
  bool wantBody() const;

  HttpRequestParseState state_ = kExpectRequestLine;  ///< 当前解析状态。
  HttpRequest request_;                               ///< 半成品/已完成的请求。
  long contentLength_ = 0;                            ///< 解析到的 Content-Length。
};

}  // namespace http
