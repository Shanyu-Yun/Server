# TCP 服务器执行流程

> 本文档描述当前 demo 服务器的核心执行路径，重点说明 `EventLoop`、`Acceptor`、
> `Channel`、`TcpConnection`、`Buffer` 之间如何协作完成 TCP 连接建立、读写和关闭。

---

## 1. 当前参与模块

| 模块 | 职责 |
|---|---|
| `main.cpp` | 创建 `EventLoop` 和 `Acceptor`，保存连接对象，注册业务回调 |
| `EventLoop` | 等待 IO 事件并分发给活跃的 `Channel` |
| `Poller / EpollPoller` | 封装 `epoll_wait`、`epoll_ctl` |
| `Channel` | 保存 fd 关心的事件，并把事件分发到对应回调 |
| `Acceptor` | 监听端口，接受新连接，把已连接 fd 交给上层 |
| `TcpConnection` | 管理单条 TCP 连接的 socket、Channel、输入输出缓冲区和回调 |
| `Buffer` | 保存从 socket 读取的数据，以及暂时写不完的数据 |

---

## 2. 服务器启动流程

```mermaid
flowchart TD
    A["main()"] --> B["创建 EventLoop loop"]
    B --> C["创建 InetAddress listenAddr"]
    C --> D["创建 Acceptor"]
    D --> E["Acceptor 创建监听 Socket"]
    E --> F["Socket 设置 SO_REUSEADDR / SO_REUSEPORT"]
    F --> G["Socket bind listenAddr"]
    G --> H["Acceptor 创建监听 Channel"]
    H --> I["设置 Acceptor::handleRead 为监听 fd 读回调"]
    I --> J["main 设置 NewConnectionCallback"]
    J --> K["acceptor.listen()"]
    K --> L["监听 Socket listen()"]
    L --> M["监听 Channel enableReading()"]
    M --> N["loop.loop()"]
    N --> O["EventLoop 进入 epoll_wait 循环"]
```

启动阶段的关键点：

- `Acceptor` 只负责监听 fd。
- `acceptor.listen()` 会把监听 fd 的读事件注册到 `EventLoop`。
- 真正的事件等待发生在 `EventLoop::loop()` 中。

---

## 3. 新连接建立流程

```mermaid
sequenceDiagram
    participant Client as TCP Client
    participant EventLoop
    participant Poller as EpollPoller
    participant ListenChannel as Acceptor Channel
    participant Acceptor
    participant Main as main.cpp
    participant Conn as TcpConnection
    participant ConnChannel as Connection Channel

    Client->>Acceptor: 发起 TCP 连接
    EventLoop->>Poller: epoll_wait()
    Poller-->>EventLoop: 监听 fd 可读
    EventLoop->>ListenChannel: handleEvent()
    ListenChannel->>Acceptor: handleRead()
    Acceptor->>Acceptor: Socket::accept4()
    Acceptor->>Main: newConnectionCallback(connfd, peerAddr)
    Main->>Conn: 创建 TcpConnection
    Main->>Conn: 设置 MessageCallback / CloseCallback
    Main->>Main: 保存到 connections
    Main->>Conn: connectEstablished()
    Conn->>ConnChannel: tie(shared_from_this())
    Conn->>ConnChannel: enableReading()
    Conn->>Main: connectionCallback()
    Main->>Conn: send(welcome message)
    Conn-->>Client: 发送欢迎消息
```

新连接阶段的关键点：

- `Socket::accept()` 内部使用 `accept4(..., SOCK_NONBLOCK | SOCK_CLOEXEC)` 创建非阻塞连接 fd。
- `TcpConnection` 必须由 `std::shared_ptr` 管理，因为 `Channel::tie()` 和异步回调会依赖 `shared_from_this()`。
- `connections` 保存 `TcpConnectionPtr`，否则连接对象会在回调结束后析构。

---

## 4. 消息读取与回包流程

```mermaid
flowchart TD
    A["客户端发送数据"] --> B["连接 fd 变为可读"]
    B --> C["EventLoop::loop() 得到活跃 Channel"]
    C --> D["Channel::handleEvent()"]
    D --> E["TcpConnection::handleRead(receiveTime)"]
    E --> F["inputBuffer_.readFd(fd, &savedErrno)"]
    F --> G{"读取结果 n"}
    G -->|"n > 0"| H["messageCallback_(conn, &inputBuffer_, receiveTime)"]
    H --> I["业务层从 Buffer 取出数据"]
    I --> J["conn->send(response)"]
    J --> K{"当前是否在 loop 线程"}
    K -->|"是"| L["sendInLoop(response)"]
    K -->|"否"| M["runInLoop() 投递到 loop 线程"]
    M --> L
    L --> N{"socket 能否一次写完"}
    N -->|"能"| O["直接 write 完成"]
    N -->|"不能"| P["剩余数据写入 outputBuffer_"]
    P --> Q["Channel enableWriting()"]
    Q --> R["等待后续 EPOLLOUT"]
    G -->|"n == 0"| S["handleClose()"]
    G -->|"n < 0"| T["handleError()"]
```

读写阶段的关键点：

- `TcpConnection::handleRead()` 只负责把数据读进 `inputBuffer_`，然后交给用户注册的消息回调。
- `send()` 对外可以跨线程调用；如果不在 loop 线程，会通过 `runInLoop()` 切回连接所属线程。
- 如果一次 `write()` 没写完，剩余数据进入 `outputBuffer_`，并开启写事件监听。

---

## 5. 输出缓冲区写完流程

```mermaid
flowchart TD
    A["outputBuffer_ 中有待发送数据"] --> B["Channel 已关注 EPOLLOUT"]
    B --> C["socket 变为可写"]
    C --> D["EventLoop 分发写事件"]
    D --> E["TcpConnection::handleWrite()"]
    E --> F["outputBuffer_.writeFd(fd, &savedErrno)"]
    F --> G{"outputBuffer_ 是否为空"}
    G -->|"是"| H["Channel disableWriting()"]
    H --> I["触发 writeCompleteCallback_"]
    I --> J{"连接状态是否 kDisconnecting"}
    J -->|"是"| K["shutdownInLoop()"]
    J -->|"否"| L["继续保持连接"]
    G -->|"否"| M["等待下一次可写事件"]
```

写缓冲阶段的关键点：

- 写事件只在有待发送数据时开启，避免一直触发 `EPOLLOUT`。
- 当输出缓冲区清空后，会关闭写事件监听。
- 如果用户已经调用 `shutdown()`，会在数据写完后再半关闭写端。

---

## 6. 连接关闭流程

```mermaid
sequenceDiagram
    participant Client as TCP Client
    participant EventLoop
    participant Channel
    participant Conn as TcpConnection
    participant Main as main.cpp

    Client->>Conn: 关闭连接或发送 FIN
    EventLoop->>Channel: 分发读/关闭事件
    Channel->>Conn: handleRead() 或 handleClose()
    Conn->>Conn: inputBuffer_.readFd()
    Conn->>Conn: n == 0 时 handleClose()
    Conn->>Channel: disableAll()
    Conn->>Channel: remove()
    Conn->>Main: connectionCallback()
    Conn->>Main: closeCallback()
    Main->>Main: connections.erase(conn->getName())
    Main-->>Conn: shared_ptr 引用计数减少
    Conn->>Conn: 析构并关闭 socket
```

关闭阶段的关键点：

- `handleClose()` 会先把 `Channel` 从 `Poller` 中移除。
- `closeCallback_` 的主要职责是让上层从连接表中移除 `TcpConnectionPtr`。
- 当没有其他 `shared_ptr` 持有连接对象时，`TcpConnection` 析构，内部 `Socket` 析构并关闭 fd。

---

## 7. 总体协作关系

```mermaid
flowchart LR
    Client["TCP Client"]
    Main["main.cpp<br/>连接表和业务回调"]
    Loop["EventLoop<br/>事件循环"]
    Poller["EpollPoller<br/>epoll_wait / epoll_ctl"]
    Acceptor["Acceptor<br/>监听和 accept"]
    ListenChannel["Channel<br/>监听 fd"]
    Conn["TcpConnection<br/>单连接管理"]
    ConnChannel["Channel<br/>连接 fd"]
    Socket["Socket<br/>fd RAII"]
    Buffer["Buffer<br/>输入/输出缓冲区"]

    Loop <--> Poller
    Loop --> ListenChannel
    ListenChannel --> Acceptor
    Acceptor --> Main
    Main --> Conn
    Conn --> ConnChannel
    Conn --> Socket
    Conn --> Buffer
    ConnChannel --> Loop
    Client <--> Acceptor
    Client <--> Conn
```

整体可以理解为：

- `EventLoop + Poller` 负责发现事件。
- `Channel` 负责把事件路由到对象方法。
- `Acceptor` 负责把监听 fd 上的新连接变成连接 fd。
- `TcpConnection` 负责连接 fd 的读、写、关闭和生命周期。
- `main.cpp` 当前承担了简化版 `TcpServer` 的职责：保存连接、注册回调、发送业务消息。
