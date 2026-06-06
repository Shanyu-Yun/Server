#pragma once
#include <unistd.h>

#include <cstddef>
#include <string>
#include <vector>

namespace net {

/**
 * @brief Buffer 默认初始容量。
 */
inline constexpr std::size_t kInitialSize = 1024;

/**
 * @brief 面向网络 IO 的可增长字节缓冲区。
 *
 * Buffer 使用 readerIndex_ 和 writerIndex_ 划分 prependable、readable、
 * writable 三段空间，用于缓存从 fd 读取的数据以及等待写入 fd 的数据。
 */
class Buffer {
 public:
  /**
   * @brief 创建指定初始容量的 Buffer。
   * @param initialSize 初始缓冲区大小。
   */
  Buffer(std::size_t initialSize = kInitialSize);

  /**
   * @brief 获取当前可读字节数。
   * @return writerIndex_ 与 readerIndex_ 之间的字节数。
   */
  size_t readableBytes() const;

  /**
   * @brief 获取当前可写字节数。
   * @return buffer_ 尾部剩余可写空间大小。
   */
  size_t writableBytes() const;

  /**
   * @brief 获取当前可前置字节数。
   * @return readerIndex_ 前面的可回收空间大小。
   */
  size_t prependableBytes() const;

  /**
   * @brief 返回可读数据起始位置。
   * @return 指向 readerIndex_ 的只读指针。
   */
  const char* peek() const;

  /**
   * @brief 消费指定长度的可读数据（只前进读下标，不返回数据）。
   * @param len 要消费的字节数。
   */
  void consume(size_t len);

  /**
   * @brief 消费所有可读数据，并重置读写下标。
   */
  void consumeAll();

  /**
   * @brief 消费数据直到指定位置（不返回数据）。
   * @param end 消费的终点，需落在 [peek(), beginWrite()) 内。
   */
  inline void consumeUntil(const char* end) {
    consume(end - peek());
  }

  /**
   * @brief 取回指定长度的数据并以字符串返回（取回后同时消费）。
   * @param len 要取回的字节数。
   * @return 包含取回数据的字符串。
   */
  std::string retrieveAsString(size_t len);

  /**
   * @brief 确保至少有指定长度的可写空间。
   * @param len 需要的可写字节数。
   */
  void ensureWritableBytes(size_t len);

  /**
   * @brief 追加一段数据到缓冲区。
   * @param data 待追加数据的起始地址。
   * @param len 待追加数据的字节数。
   */
  void append(const char* data, size_t len);

  /**
   * @brief 追加一个字符串到缓冲区。
   * 
   * @param str 
   */
  void append(const std::string& str) {
    append(str.data(), str.size());
  }

  /**
   * @brief 返回可写区域起始位置。
   * @return 指向 writerIndex_ 的可写指针。
   */
  char* beginWrite();

  /**
   * @brief 返回可写区域起始位置的只读指针。
   * @return 指向 writerIndex_ 的只读指针。
   */
  const char* beginWrite() const;
  /**
   * @brief 将缓冲区中的可读数据写入文件描述符。
   * @param fd 目标文件描述符。
   * @param savedErrno 写入失败时保存 errno。
   * @return write() 的返回值。
   */
  ssize_t writeFd(int fd, int* savedErrno);

  /**
   * @brief 从文件描述符读取数据到缓冲区。
   * @param fd 源文件描述符。
   * @param savedErrno 读取失败时保存 errno。
   * @return readv() 的返回值。
   */
  ssize_t readFd(int fd, int* savedErrno);

  /**
   * @brief 查找 CRLF（"\r\n"）在可读数据中的位置。
   * @return 指向 CRLF 起始位置的指针，或 nullptr 如果未找到。
   */
  const char* findCRLF() const;

 private:
  std::size_t readerIndex_;   ///< 可读数据起始下标。
  std::size_t writerIndex_;   ///< 可写数据起始下标。
  std::vector<char> buffer_;  ///< 实际存储字节数据的连续空间。

 private:
  /**
   * @brief 整理或扩容缓冲区，为写入留出空间。
   * @param len 需要的可写字节数。
   */
  void makeSpace(size_t len);
};

}  // namespace net
