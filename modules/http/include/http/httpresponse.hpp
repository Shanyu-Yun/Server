#pragma once
#include <map>
#include <string>

#include "base/buffer.hpp"

namespace net {
class Buffer;  // 仅用作指针参数，前置声明即可。

}
namespace http {

/**
 * @brief 常用 HTTP 状态码（务实子集，不追求 RFC 完整）。
 */
enum HttpStatusCode : int {
  k200Ok = 200,
  k400BadRequest = 400,
  k404NotFound = 404,
  k413PayloadTooLarge = 413,
  k500InternalServerError = 500,
};

/**
 * @brief HTTP 响应构造器（值对象）。
 *
 * 用户在回调中填写状态码/头/body，最终由 appendToBuffer 序列化成字节。
 * Content-Length 与 Connection 头由框架自动补全，用户不应手填。
 */
class HttpResponse {
 public:
  /**
   * @brief 构造响应。
   * @param closeConnection 响应后是否关闭连接（一般传 !req.keepAlive()）。
   */
  explicit HttpResponse(bool closeConnection);

  /** @brief 设置状态码。 */
  void setStatusCode(HttpStatusCode code);

  /** @brief 设置状态短语；不设置时按状态码取默认（如 "OK"/"Not Found"）。 */
  void setStatusMessage(std::string msg);

  /** @brief 设置响应后是否关闭连接。 */
  void setCloseConnection(bool on);

  /** @brief 响应后是否关闭连接。 */
  bool closeConnection() const;

  /** @brief 添加一个响应头。 */
  void addHeader(const std::string& key, const std::string& value);

  /** @brief 设置 Content-Type，等价于 addHeader("Content-Type", type)。 */
  void setContentType(const std::string& type);

  /** @brief 设置响应体。 */
  void setBody(std::string body);

  /**
   * @brief 序列化到输出缓冲区。
   *
   * 依次写入状态行、各响应头、自动补充的 Content-Length 与 Connection、
   * 空行，最后是 body。
   */
  void appendToBuffer(net::Buffer* out) const;

  /**
   * @brief 设置streaming的状态
   * 
   * @param on 
   */
  void setStreaming(bool on);

  /**
   * @brief 获得流式响应状态
   * 
   * @return true 
   * @return false 
   */
  inline bool getStreaming() {
    return streaming_;
  }

 private:
  HttpStatusCode statusCode_ = k200Ok;          ///< 状态码。
  std::string statusMessage_;                   ///< 状态短语。
  std::map<std::string, std::string> headers_;  ///< 响应头。
  std::string body_;                            ///< 响应体。
  bool closeConnection_;                        ///< 响应后是否关闭连接。
  bool streaming_;                              ///< 响应体是否为流式响应
};

}  // namespace http
