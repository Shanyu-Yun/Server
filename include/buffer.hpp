#pragma once
#include <unistd.h>

#include <cstddef>
#include <string>
#include <vector>

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
   * @brief 标记已经读取指定长度的数据。
   * @param len 要取出的字节数。
   */
  void retrieve(size_t len);

  /**
   * @brief 取出所有可读数据，并重置读写下标。
   */
  void retrieveAll();

  /**
   * @brief 取出指定长度的数据并转换为字符串。
   * @param len 要取出的字节数。
   * @return 包含取出数据的字符串。
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
   * @brief 返回可写区域起始位置。
   * @return 指向 writerIndex_ 的可写指针。
   */
  char* beginWrite();

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

 private:
  /**
   * @brief 可读数据起始下标。
   */
  std::size_t readerIndex_;

  /**
   * @brief 可写数据起始下标。
   */
  std::size_t writerIndex_;

  /**
   * @brief 实际存储字节数据的连续空间。
   */
  std::vector<char> buffer_;

 private:
  /**
   * @brief 整理或扩容缓冲区，为写入留出空间。
   * @param len 需要的可写字节数。
   */
  void makeSpace(size_t len);
};
