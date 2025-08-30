# MPRPC日志模块实现详解

## 1. 概述
MPRPC的日志模块是一个基于C++11实现的异步日志系统，采用单例模式和线程安全队列设计，支持多线程安全地记录日志，并由专门的日志线程负责将日志写入文件。该系统具有高性能、线程安全、自动化等特点，为分布式系统提供了可靠的日志记录能力。

## 2. 整体架构
日志模块主要由以下几个部分组成：
- **Logger类**：日志记录的核心类，采用单例模式
- **LockQueue模板类**：线程安全的队列，用于日志消息的缓冲
- **日志宏**：简化日志记录的宏定义
- **日志文件管理**：按日期自动分割日志文件

## 3. 核心组件实现

### 3.1 Logger类

#### 3.1.1 类定义
```cpp
class Logger
{
public:
    // 获取日志的单例
    static Logger &GetInstance();
    // 设置日志级别
    void SetLogLevel(LogLevel level);
    // 写日志
    void Log(std::string msg);
    
private:
    int m_loglevel;                  // 记录日志级别
    LockQueue<std::string> m_lckQue; // 日志缓冲队列
    Logger();
    Logger(const Logger &) = delete;
    Logger(Logger &&) = delete;
};
```

#### 3.1.2 单例实现
```cpp
Logger &Logger::GetInstance()
{
    static Logger logger;
    return logger;
}
```

采用懒汉式单例模式，通过`static`局部变量实现，确保全局只有一个Logger实例。

#### 3.1.3 构造函数与线程管理
```cpp
Logger::Logger()
{
    // 启动专门的写日志线程
    std::thread writeLogTask([&](){
        for (;;)
        {
            // 获取当前的日期，然后取日志信息，写入相应的日志文件当中 a+
            time_t now = time(nullptr);
            tm *nowtm = localtime(&now);
            char file_name[128];
            sprintf(file_name, "%d-%02d-%02d_log.txt", nowtm->tm_year+1900, nowtm->tm_mon+1, nowtm->tm_mday);
            FILE *pf = fopen(file_name, "a+");
            if (pf == nullptr)
            {
                std::cout << "logger file : " << file_name << " open error!" << std::endl;
                exit(EXIT_FAILURE);
            }
            std::string msg = m_lckQue.Pop();
            char time_buf[128] = {0};
            sprintf(time_buf, "%d-%02d-%02d %d:%02d:%02d [%s] ", 
                   nowtm->tm_year+1900, nowtm->tm_mon+1, nowtm->tm_mday,
                   nowtm->tm_hour, nowtm->tm_min, nowtm->tm_sec,
                   (m_loglevel == INFO ? "info" : "error"));
            msg.insert(0, time_buf);
            msg.append("\n");
            fputs(msg.c_str(), pf);
            fclose(pf);
        }
    });
    // 设置分离线程，守护线程
    writeLogTask.detach();
}
```

构造函数中创建了一个专门的写日志线程，该线程会无限循环运行，负责从队列中取出日志消息并写入文件。线程被设置为分离状态(detach)，成为守护线程，在程序结束时会被自动回收。

#### 3.1.4 日志级别设置
```cpp
void Logger::SetLogLevel(LogLevel level)
{
    m_loglevel = level;
}
```

设置日志记录的级别，支持INFO和ERROR两种级别。

#### 3.1.5 日志记录
```cpp
void Logger::Log(std::string msg)
{
    m_lckQue.Push(msg);
}
```

将日志消息推入线程安全队列中，由专门的日志线程负责后续处理。

### 3.2 LockQueue模板类

#### 3.2.1 类定义
```cpp
template <typename T>
class LockQueue
{
public:
    // 多个 worker 线程都会写日志 queue
    void Push(const T &data);
    // 一个线程读日志queue，写日志文件
    T Pop();
    
private:
    std::queue<T> m_queue;
    std::mutex m_mutex;
    std::condition_variable m_condvariable;
};
```

LockQueue是一个线程安全的队列模板类，使用生产者-消费者模式实现多线程安全的数据交换。

#### 3.2.2 Push方法 - 多线程安全添加消息
```cpp
void Push(const T &data)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_queue.push(data);
    m_condvariable.notify_one();
}
```

工作原理：
1. **获取锁**：使用`std::lock_guard`自动获取互斥锁，确保同一时间只有一个线程能访问队列
2. **添加数据**：将日志消息添加到队列尾部
3. **通知等待线程**：调用`notify_one()`唤醒可能正在等待的日志线程

关键点：
- `std::lock_guard`在构造时自动获取锁，在析构时自动释放锁，即使发生异常也能保证锁被释放
- 使用`notify_one()`而不是`notify_all()`是因为只有一个专门的日志线程在等待，这样可以提高效率

#### 3.2.3 Pop方法 - 单线程取出消息
```cpp
T Pop()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    while (m_queue.empty())
    {
        // 日志队列为空，线程进入wait状态
        m_condvariable.wait(lock);
    }
    T data = m_queue.front();
    m_queue.pop();
    return data;
}
```

工作原理：
1. **获取锁**：使用`std::unique_lock`获取互斥锁
   - 与`std::lock_guard`不同，`std::unique_lock`可以在需要时手动释放和重新获取锁
2. **检查队列**：检查队列是否为空
   - 如果队列为空，调用`wait()`进入等待状态
   - `wait()`会自动释放锁，并在被唤醒时重新获取锁
3. **取出数据**：从队列头部取出数据并删除
4. **返回数据**：返回取出的日志消息

关键点：
- 使用`while`循环检查队列是否为空，而不是`if`语句，这是为了处理"虚假唤醒"的情况
- 当线程被`notify_one()`唤醒时，它会重新获取锁，然后再次检查队列是否为空
- 这种设计确保即使被唤醒时队列仍然为空，线程会再次进入等待状态，而不是继续执行

### 3.3 日志宏定义

```cpp
/* 定义宏 LOG_INFO("xxx %d %s", 20, "xxxx") */
#define LOG_INFO(logmsgformat, ...)                     \
do                                                  \
{                                                   \
    Logger &logger = Logger::GetInstance();         \
    logger.SetLogLevel(INFO);                       \
    char c[1024] = {0};                             \
    snprintf(c, 1024, logmsgformat, ##__VA_ARGS__); \
    logger.Log(c);                                  \
} while (0)

#define LOG_ERR(logmsgformat, ...)                      \
do                                                  \
{                                                   \
    Logger &logger = Logger::GetInstance();         \
    logger.SetLogLevel(ERROR);                      \
    char c[1024] = {0};                             \
    snprintf(c, 1024, logmsgformat, ##__VA_ARGS__); \
    logger.Log(c);                                  \
} while (0)
```

定义了两个日志宏：
- `LOG_INFO`：记录普通信息级别的日志
- `LOG_ERR`：记录错误信息级别的日志

两个宏都使用可变参数宏特性，支持格式化输出，使用方式类似于printf函数。

## 4. 日志级别

日志系统支持两种日志级别：

```cpp
/* 定义日志级别 */
enum LogLevel
{
    INFO,  // 普通信息
    ERROR, // 错误信息
};
```

- **INFO**：用于记录普通信息，如服务启动、RPC调用等正常操作
- **ERROR**：用于记录错误信息，如连接失败、服务异常等错误情况

## 5. 多线程安全队列实现原理

### 5.1 生产者-消费者模式

在MPRPC日志系统中，LockQueue实现了生产者-消费者模式：
- **生产者**：多个业务线程调用`Push()`方法添加日志消息
- **消费者**：单个专门的日志线程调用`Pop()`方法取出消息并写入文件

### 5.2 线程同步机制

条件变量(`std::condition_variable`)是实现这种同步机制的关键：

- **等待条件**：`m_condvariable.wait(lock)`会在条件不满足时阻塞线程
- **通知条件**：`m_condvariable.notify_one()`会唤醒一个等待的线程
- **自动加锁/解锁**：条件变量会自动处理锁的获取和释放，简化了多线程编程

### 5.3 完整的日志记录流程

1. 业务线程调用`LOG_INFO("message")`或`LOG_ERR("message")`
2. 宏展开后调用`Logger::GetInstance().Log("message")`
3. `Log()`方法调用`m_lckQue.Push("message")`
4. `Push()`方法获取锁，添加消息到队列，然后通知等待的日志线程
5. 日志线程被唤醒后，调用`Pop()`获取消息
6. 日志线程将消息添加时间戳和级别信息，然后写入文件
7. 如果队列不为空，日志线程继续处理下一条消息；如果队列为空，则再次进入等待状态

## 6. 日志文件管理

日志系统采用按日期自动分割的方式管理日志文件：

```cpp
time_t now = time(nullptr);
tm *nowtm = localtime(&now);
char file_name[128];
sprintf(file_name, "%d-%02d-%02d_log.txt", nowtm->tm_year+1900, nowtm->tm_mon+1, nowtm->tm_mday);
FILE *pf = fopen(file_name, "a+");
```

日志文件命名格式为"YYYY-MM-DD_log.txt"，每天生成一个新的日志文件，便于日志的归档和管理。

## 7. 日志格式

日志系统输出的日志格式包含以下信息：

```cpp
sprintf(time_buf, "%d-%02d-%02d %d:%02d:%02d [%s] ", 
       nowtm->tm_year+1900, nowtm->tm_mon+1, nowtm->tm_mday,
       nowtm->tm_hour, nowtm->tm_min, nowtm->tm_sec,
       (m_loglevel == INFO ? "info" : "error"));
msg.insert(0, time_buf);
msg.append("\n");
```

日志格式为：`YYYY-MM-DD HH:MM:SS [level] message\n`

例如，调用`LOG_INFO("User login: %s, age: %d", "Alice", 25)`会生成类似这样的日志：
```
2023-08-30 14:30:45 [info] User login: Alice, age: 25
```

## 8. 设计特点与优势

1. **高性能**：采用异步写入方式，日志操作不会阻塞主线程
2. **线程安全**：通过互斥锁和条件变量确保多线程环境下的安全
3. **可扩展性**：使用模板实现的LockQueue可以用于其他需要线程安全队列的场景
4. **自动化**：日志文件按日期自动分割，无需手动管理
5. **易用性**：通过宏简化日志记录，支持格式化输出

## 9. 应用场景

在MPRPC框架中，这个日志系统主要用于记录：
- RPC服务启动和关闭信息
- 服务注册与发现过程
- RPC调用请求和响应
- 网络连接状态变化
- 错误和异常信息

这种设计使得开发者可以方便地追踪和调试分布式系统中的问题，同时不会因为频繁的IO操作影响系统性能。
