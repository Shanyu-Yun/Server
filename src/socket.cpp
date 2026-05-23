#include "socket.hpp"

#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include "logger.hpp"

Socket::Socket(int sockfd) : sockfd_(sockfd) {}

Socket::~Socket() {
  ::close(sockfd_);
}

int Socket::fd() const {
  return sockfd_;
}

void Socket::bindAddress(const InetAddress& localaddr) {
  if (::bind(sockfd_, reinterpret_cast<const sockaddr*>(localaddr.getSockAddr()),
             sizeof(sockaddr_in)) < 0) {
    LOGERROR("Socket::bindAddress() error");
    abort();
  }
}

void Socket::listen() {
  if (::listen(sockfd_, SOMAXCONN) < 0) {
    LOGERROR("Socket::listen() error");
    abort();
  }
}

int Socket::accept(InetAddress* peeraddr) {
  sockaddr_in addr;
  socklen_t addrlen = sizeof(addr);
  int connfd = ::accept(sockfd_, reinterpret_cast<sockaddr*>(&addr), &addrlen);
  if (connfd >= 0) {
    peeraddr->setSockAddr(addr);
  }
  return connfd;
}

void Socket::shutdownWrite() {
  if (::shutdown(sockfd_, SHUT_WR) < 0) {
    LOGERROR("Socket::shutdownWrite() error");
  }
}

void Socket::setTcpNoDelay(bool on) {
  int optval = on ? 1 : 0;
  if (::setsockopt(sockfd_, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(optval)) < 0) {
    LOGERROR("Socket::setTcpNoDelay() error");
  }
}

void Socket::setReuseAddr(bool on) {
  int optval = on ? 1 : 0;
  if (::setsockopt(sockfd_, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
    LOGERROR("Socket::setReuseAddr() error");
  }
}

void Socket::setReusePort(bool on) {
  int optval = on ? 1 : 0;
  if (::setsockopt(sockfd_, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval)) < 0) {
    LOGERROR("Socket::setReusePort() error");
  }
}

void Socket::setKeepAlive(bool on) {
  int optval = on ? 1 : 0;
  if (::setsockopt(sockfd_, SOL_SOCKET, SO_KEEPALIVE, &optval, sizeof(optval)) < 0) {
    LOGERROR("Socket::setKeepAlive() error");
  }
}