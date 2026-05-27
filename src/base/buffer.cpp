#include "base/buffer.hpp"

#include <sys/uio.h>
#include <unistd.h>

#include <algorithm>

Buffer::Buffer(std::size_t initialSize)
    : readerIndex_(0), writerIndex_(0), buffer_(initialSize) {}

size_t Buffer::readableBytes()  const { return writerIndex_ - readerIndex_; }
size_t Buffer::writableBytes()  const { return buffer_.size() - writerIndex_; }
size_t Buffer::prependableBytes() const { return readerIndex_; }

const char* Buffer::peek() const { return buffer_.data() + readerIndex_; }

void Buffer::retrieve(size_t len) {
  if (len < readableBytes()) {
    readerIndex_ += len;
  } else {
    retrieveAll();
  }
}

void Buffer::retrieveAll() {
  readerIndex_ = 0;
  writerIndex_ = 0;
}

std::string Buffer::retrieveAsString(size_t len) {
  std::string result(peek(), len);
  retrieve(len);
  return result;
}

void Buffer::ensureWritableBytes(size_t len) {
  if (writableBytes() < len) makeSpace(len);
}

void Buffer::append(const char* data, size_t len) {
  ensureWritableBytes(len);
  std::copy(data, data + len, buffer_.data() + writerIndex_);
  writerIndex_ += len;
}

char* Buffer::beginWrite() { return buffer_.data() + writerIndex_; }

ssize_t Buffer::writeFd(int fd, int* savedErrno) {
  ssize_t n = ::write(fd, peek(), readableBytes());
  if (n >= 0) retrieve(n);
  else *savedErrno = errno;
  return n;
}

ssize_t Buffer::readFd(int fd, int* savedErrno) {
  char extraBuffer[65536];
  iovec vec[2];
  const size_t writable = writableBytes();
  vec[0].iov_base = beginWrite();
  vec[0].iov_len  = writable;
  vec[1].iov_base = extraBuffer;
  vec[1].iov_len  = sizeof(extraBuffer);
  ssize_t n = ::readv(fd, vec, 2);
  if (n > 0) {
    if (static_cast<size_t>(n) <= writable) {
      writerIndex_ += n;
    } else {
      writerIndex_ = buffer_.size();
      append(extraBuffer, n - writable);
    }
  } else if (n < 0) {
    *savedErrno = errno;
  }
  return n;
}

void Buffer::makeSpace(size_t len) {
  if (writableBytes() + prependableBytes() < len) {
    buffer_.resize(writerIndex_ + len);
  } else {
    const size_t readable = readableBytes();
    std::copy(buffer_.data() + readerIndex_, buffer_.data() + writerIndex_, buffer_.data());
    readerIndex_ = 0;
    writerIndex_ = readable;
  }
}
