#include "poller.hpp"

#include "epollpoller.hpp"

Poller::Poller(EventLoop* loop) : ownerLoop_(loop) {}

// 如果有其他的poller实现，可以在这里添加条件编译来选择不同的poller实现
Poller* Poller::newDefaultPoller(EventLoop* loop) {
  // 因为这个工厂函数是一个静态函数，所以不属于任何一个对象
  // 因此无法访问非静态成员变量ownerLoop_，只能通过参数传入EventLoop对象的指针来创建Poller实例。
  // 这也是为什么可以调用EpollerPoller构造函数的原因（如果是父类的成员函数就不行）
  return new EpollPoller(loop);
}