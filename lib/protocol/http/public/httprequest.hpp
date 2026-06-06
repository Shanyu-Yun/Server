#pragma once
#include <cstddef>
#include <map>
#include <string>

#include "timestamp.hpp"

namespace protocol {

/**
 * @brief HTTP 请求方法。
 */
enum class Method { kInvalid, kGet, kPost, kHead, kPut, kDelete };

/**
 * @brief HTTP 协议版本。
 */
enum class Version { kUnknown, kHttp10, kHttp11 };

/**
 * @brief 一次 HTTP 请求的解析结果（值对象）。
 *
 * 由 HttpContext 在解析过程中逐字段写入，解析完成后交给用户回调只读访问。
 * 生命周期跨越多次 onMessage（body 可能分段到达），故 method/path/header/body
 * 全部以 std::string 拷贝持有，不持有指向 Buffer 的视图。
 *
 * HTTP/1.1 请求报文格式：
 *
 *   POST /search?q=hello HTTP/1.1\r\n        <- 请求行：method  path  query  version
 *   Host: example.com\r\n                    <- 请求头（若干行，key 不区分大小写）
 *   Content-Type: application/json\r\n
 *   Content-Length: 18\r\n
 *   Connection: keep-alive\r\n
 *   \r\n                                     <- 空行：头部结束标志
 *   {"key": "value"}                         <- 请求体（长度由 Content-Length 决定）
 */
class HttpRequest {
 public:
  // —— 解析期写入（由 HttpContext 调用）——

  /**
   * @brief 由请求行的方法字段设置请求方法。
   * @param start 方法字符串起始位置。
   * @param end   方法字符串结束位置（不含）。
   * @return 能识别返回 true；否则保持 kInvalid 并返回 false。
   */
  bool setMethod(const char* start, const char* end);

  /**
   * @brief 设置请求路径（'?' 之前的部分）。
   */
  void setPath(const char* start, const char* end);

  /**
   * @brief 设置查询串（'?' 之后的原始内容，不含 '?'）。
   */
  void setQuery(const char* start, const char* end);

  /**
   * @brief 设置协议版本。
   */
  void setVersion(Version v);

  /**
   * @brief 追加一个请求头。
   *
   * key 统一小写化后存入，value 去除首尾空白，以支持大小写不敏感查找。
   * @param start 行起始（key 起点）。
   * @param colon 冒号位置。
   * @param end   行结束（不含 CRLF）。
   * @note 这里的几个指针位置大概是
   *【start】Content-Type:[colon] text/html \r\n[end]
   */
  void addHeader(const char* start, const char* colon, const char* end);

  /**
   * @brief 向 body 追加数据（body 可能分多次到达）。
   */
  void appendBody(const char* data, size_t len);

  /**
   * @brief 记录请求到达时间。
   */
  void setReceiveTime(transport::Timestamp t);

  // —— 用户回调期读取 ——

  /** @brief 请求方法。 */
  Method method() const {
    return method_;
  }

  /** @brief 协议版本。 */
  Version version() const {
    return version_;
  }

  /** @brief 请求路径。 */
  const std::string& path() const {
    return path_;
  }

  /** @brief 查询串。 */
  const std::string& query() const {
    return query_;
  }

  /**
   * @brief 按字段名取请求头的值（字段名按小写匹配）。
   * @return 命中返回对应值；未命中返回空串。
   */
  std::string getHeader(const std::string& field) const;

  /** @brief 全部请求头（key 已小写）。 */
  const std::map<std::string, std::string>& headers() const {
    return headers_;
  }

  /** @brief 请求体。 */
  const std::string& body() const {
    return body_;
  }

  /**
   * @brief 是否保持长连接。
   *
   * 规则：Connection 头含 "close" → false；否则 HTTP/1.1 → true；
   * 否则（HTTP/1.0 且无 keep-alive）→ false。
   */
  bool keepAlive() const;

  /**
   * @brief 复位为初始状态，供 keep-alive 下复用解析下一个请求。
   */
  void reset();

 private:
  Method method_ = Method::kInvalid;            ///< 请求方法。
  Version version_ = Version::kUnknown;         ///< 协议版本。
  std::string path_;                            ///< 请求路径。
  std::string query_;                           ///< 查询串。
  std::string body_;                            ///< 请求体。
  std::map<std::string, std::string> headers_;  ///< 请求头，key 已小写。
  transport::Timestamp receiveTime_;                  ///< 请求到达时间。
};

}  // namespace protocol
