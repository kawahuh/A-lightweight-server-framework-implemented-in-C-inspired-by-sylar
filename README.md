此项目是跟着b站的sylar佬做的，实现了协程库和网络编程部分

sylar的b站视频：https://www.bilibili.com/video/av53602631/?from=www.sylar.top

源项目地址：https://github.com/sylar-yin/sylar

```
thread  <-  mutex
  ||                 timer
  \/                  ||
Scheduler  -> fiber   \/
		  -------> IOManager  
		  			   ||  fd_manager   address       servlet
		  	            ||     ||            ||			||
		  	            \/     \/            \/			\/
              hook ->      socket      -> TCPServer -> HttpServer
                            ||
                            \/
                    http   stream <- byteArray
                     ||     ||
                     \/     \/
     http_parser ->  HttpSession
                       	    ||
                       	    \/
                       HttpConnection
```

fiber 保存了上下文，绑定了入口函数来执行任务

scheduler 用的是先来先服务，内部维护一个任务队列和调度线程池，线程池从任务队列中按顺序取任务执行。添加新任务后，通知线程池有新任务了，重新开始调度

# Mutex

基于c++11的互斥锁，增加了`信号量，读写锁，自旋锁，原子锁`，解决了多线程运行下数据竞争和竞态的问题

# Thread

基于c++11的线程库进一步封装，线程绑定 Thread::run 函数，在 Thread::run 函数中安全的运行 m_cb ，也就是真正需要执行的函数

在后续的开发中，线程持续运行的是 **Scheduler::run** 函数，在 run 函数中循环获取和调度任务

在 run 函数中，设置了线程的 id 和 name (均为自定义内容，方便查看调试信息)

```
智能指针
信号量
线程空间
pthread_create等系统thread函数
```

# Fiber

作为协程调度的最小执行单位，用于执行指定的函数和任务，可实现在任务中途切换到其他任务执行，提高任务可控性

通过保存上下文来进行协程间切换

带参构造函数绑定了需要执行的函数 **cb** ，分配栈空间，调用 **getcontext** 来获取上下文，设置上下文中的栈空间，绑定上下文的入口函数 **MainFunc** 方便后续被 **scheduler** 调用执行

绑定的函数 **MainFunc** 运行了**协程需要执行的函数**，执行函数完毕后修改状态为结束态，函数置空。使用 auto  类型获取到协程的指针，本体fiber调用智能指针的 **reset** 减少引用计数，之后 auto 的raw_ptr 调用 **swapOut / back**切换回原上下文或调度器继续执行

主要方法是 **reset(function cb), call, back, swapIn, swapOut, YieldToReady和YieldToHold** 。在 **reset** 中，重新绑定了函数 **cb** ，相当于再执行了一遍构造函数。**call** 和 **swapIn** 的区别在于一个是保存到当前线程空间下的 fiber 的上下文，一个是保存到 Scheduler 中的主协程的上下文。**back** 和 **swapOut** 则是切换回去（两个分别设计的猜测是use_caller为true或false的不同情况，如果为true的话就需要单独保存原程序了）。**YieldToReady** 和 **YieldToHold** 调用了 **swapIn 和 swapOut** ，实现切换

特点：**Fiber** 保存了程序的上下文，执行函数，特点在于对函数的执行可控性更强。

核心: swapIn/Out, call/back, reset(重新设置任务), GetThis(用于保存主协程的任务进度，不至于调用其他任务后无法返回主进程)

```
基于ucontext的上下文切换
```

# Scheduler

协程调度的核心

在构造函数中设置线程数量和是否将当前任务也加入调度

在 Scheduler::start 中创建线程池并启动调度，每个线程绑定 Scheduler::run 函数

```
schedule -> scheduleNoLock -> struct FiberAndThread() ->add to-> list<FiberAndThread> m_fibers

Scheduler::run() 里的 while () 循环检查vector(可以理解为一个队列) m_fibers
```

将当前任务通过 Fiber::GetThis 保存起来，然后创建 idle_fiber 和 cb_fiber，一个用于空闲状态下等待，一个用于执行任务。结构体 FiberAndThread ft 定义在头文件中，使用模板函数来绑定任务进行执行，封装为 Scheduler::schedule，**外界只需要通过 Scheduler::schedule 调度函数或者 fiber 即可**。

在 run 中，会循环检测是否有新的任务添加了进来。若有任务，判断是 fiber 还是 func，进入对应的分支进行调度。若无任务，则去执行 idle_fiber；退出调度的选项同样在此分支中

核心函数 Scheduler::run

```
模板类
```

# Timer

用于定时或周期性的执行任务，任务保存在每个定时器中

timer 由 TimerManager 使用 set 统一管理，listExpiredCb 用于收集需要执行的任务

# IOManager

继承了 Scheduler 和 TimerManager

增加了基于epoll通过文件描述符来进行调度对应事件的功能，通过协程去对文件进行处理和调用函数。每个事件有对应的 **FdContext** ，在 **FdContext** 中保存了对应的读写事件。类似的，这里也是通过调用模板类函数 **schedule** 来对函数进行调度。

在构造函数中初始化 epoll 和管道，管道用于唤醒 epoll (epoll_wait)

重写的 idle 函数中，获取了 epoll_wait 新有消息的 fd 并列出了定时器中超时需要执行的函数。对 fd 需要执行的事件(读 or 写)和超时函数进行调度，完成后 swapOut 返回 Scheduler::run 中继续检查任务

在 tickle 函数中，通过向在构造函数中初始化的管道发送数据来唤醒对应的协程进行工作。重写的 **idle** 函数在 while 循环中接收 **epoll_wait** 返回的事件和定时器计时需要处理的函数

1. 在构造函数中初始化了 epoll 和 管道，执行 run 函数
2. 由于 **IOManager** 重写了 idle 函数，所以会循环检测事件和定时器到时触发的函数
3. **addEvent** 等相关操作文件的函数都是通过修改 epoll_event 结构体来操作的

# Fd_Manager

针对 fd 进行一些设置，包括是否阻塞，是否是 socket，是否关闭，接收事件和超时时间

FdManager中，get 用于新建 or 获取，del 用于删除

# Hook

封装了系统的函数，若设置为非阻塞的情况下(在 Scheduler::run 函数的开头调用了 setHookEnable)，可以防止某个函数或进程占用 CPU 资源 (如 sleep)

核心函数 do_io

首先会判断是否需要执行 hook 后的函数。若未启用 hook ，没有上下文，不是 socket 或者阻塞直接调用原函数。执行原函数时，若执行被打断或者未就绪，则添加定时器和事件，等待就绪或超时后再执行。这是两种触发方式，若**定时器触发**，则会取消事件（取消时会触发对fd的操作事件）；若**事件监听触发**则会直接完成事件，之后取消定时器。

特别的：sleep 是通过设置定时器来实现的，connect_with_timeout似乎也用了定时器

# Config

不同类型的数据转 string (double -> "double")

ConfigVarBase 

包括了 list, vector, set 类型向 string 类型的转化，使用了第三方库 YAML

反序列化：

1. 将字符串 v 转化为 YAML 节点对象
2. 创建空的 vector 准备字符串流
3. 遍历 YAML 节点，每次清空字符串流，当前节点写入流，使用`LexicalCast<std::string, T>`将字符串转换回类型 T，将转换结果加入 vector 

# Socket

划定了套接字类型和协议类型，封装了不同的套接字创建方法(TCP, UDP和IPv4, IPv6, Unix协议的组合)

定义了 bind, connect, listen, close, send, recv方法，获取配置的方法，序列化处理。SSLSocket的不同在于添加了SSL库来保证安全

```
创建socket对象
初始化底层套接字
绑定地址
监听连接
接受连接
发起连接
数据传输
关闭连接
```

里面的大部分函数都是封装了其他函数并在操作之前检查了一遍是否合法

可以发送和接收数据，通过 msghdr 结构体

序列化是把本地的一些数据+自定义的字符串通过字符串流收集起来

# Address

针对 IPv4, IPv6, Unix 三种网络协议类似创建了地址类

使用工厂模式进行创建对象( Address -> IPv4Address, IPv6Address, UnixAddress)

对网络地址进行解析

# TCP_Server

绑定 sock

定义了 tcp_server 的所有配置项

定义了配置向字符串以及字符串向配置的转化

在先前实现了 hook 的基础上，对 socks 做异步连接，同时处理多个 sock 的 accept

# Stream

读写指定内容，基于 buffer 或 ByteArray

是一个接口类，在 Socket_Stream 中具体实现

# ByteArray

针对不同的数据类型和大小端存储进行读写操作，以 node 的链表方式存储数据，创建了大端序小端序之间的转换，针对各种存储类型的读写，向16进制字符串和普通字符串的转化

使用了 zigzag 的编码和解码

# Socket_Stream

进行操作前记得判断是否可用(isConnected, !, isClose...)

读写调用的是 Socket 中封装的方法，核心存储的结构是 iovec，<sys/ioctl.h>中的结构

在确认连接的情况下，基于 socket 中的 recv 和 send 方法来读写

# Http_parser

HttpRequestParser 和 HttpResponseParser 主要封装了这个库的一些功能，如 URI , 请求方法，路径

使用了第三方库 http11_parser  [mongrel2/src/http11 at master · mongrel2/mongrel2](https://github.com/mongrel2/mongrel2/tree/master/src/http11)

解析数据(请求体/响应体)

# Http

HttpRequest 和 HttpResponse

请求和响应的方法，版本，路径，原始查询字符串，内容...

set, get, has, del

主要是通过 m_headers, m_params, m_cookies 三个表和 模板函数 checkGetAs 的查询来实现的

Http 协议相关的枚举，类型和接口设计，大小写不敏感

数据序列化

# Http_Session

接收请求，发送响应

recvRequest

解析读取到的请求体并保存到对象 HttpRequest 中

```
创建解析器
获取请求数组长度
分配数据内存
获取数据内存指针
循环读取内存，存放在 data 中
将 data 中的数据复制到 body
初始化 HttpRequest 的连接
```

sendResponse

发送响应

# Servlet

用来处理 http 请求和 url 路径匹配

根据 url 的路径匹配并分发请求到相应的 servlet

# Http_Server

继承了 Tcp_server，重写了 handleClient

内部成员有 m_dispatch

处理客户端时，在 while(true) 创建一个 HttpSession 会话，recvRequest 获取到请求体，调用 m_dispatch 的 handle 处理请求，然后用会话的 sendResponse 发送请求完成一次会话

# Http_Connection

创建连接池处理多个连接

单个连接最终收束到 DeRequest上

recvResponse

和 recvRequest 差不多，但在解析响应内容的时候多加了解析分块传输编码的分支

都可以处理连接，但是连接池可以对单个 connection 进行复用
