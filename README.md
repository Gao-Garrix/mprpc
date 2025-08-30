# Muduo Protobuf Remote Procedure Call (MPRPC)

## 1、项目概述
MPRPC（Muduo Protobuf Remote Procedure Call）是一个基于C++的高性能RPC框架，它结合了Muduo网络库和Protobuf序列化库，实现了高效的远程过程调用。该框架采用了分布式架构设计，通过Zookeeper实现服务注册与发现，为分布式系统提供了一种便捷、高效的RPC解决方案。

### 核心特性
- **高性能网络通信**：基于Muduo网络库，采用非阻塞IO和事件驱动模型，支持高并发处理
- **高效序列化**：使用Protobuf进行数据序列化和反序列化，比JSON/XML等文本格式效率更高
- **服务注册与发现**：集成Zookeeper，实现服务的自动注册、发现和管理
- **解耦设计**：网络层、业务逻辑层、序列化层分离，便于扩展和维护
- **跨平台支持**：基于C++实现，支持多平台部署

## 2、文件结构
```bash
.
├── autobuild.sh # [一键构建脚本]
├── bin     # 可执行文件目录
├── build   # make 构建目录
├── CMakeLists.txt # [cmake构建配置文件]
├── example # RPC caller 以及 callee 示例
├── lib     # libmprpc.a 静态库
├── README.md
├── src     # MPRPC 源码
└── test    # protobuf 使用示例
```

## 3、技术栈及选型原因
### 3.1 核心技术栈
- **C++11**：现代C++标准，提供丰富的语言特性和标准库支持
- **Muduo网络库**：基于Reactor模式的高性能C++网络库，非阻塞IO、事件驱动模型
- **Protobuf**：Google的高效二进制序列化协议，比JSON/XML等文本格式效率更高
- **Zookeeper**：分布式协调服务，提供服务注册与发现功能
- **CMake**：跨平台构建工具，简化项目构建过程

### 3.2 技术选型原因
#### 3.2.1 Muduo网络库
- **高性能**：采用非阻塞IO和事件驱动模型，支持高并发连接
- **设计优雅**：将网络代码与业务逻辑分离，降低耦合度
- **稳定性**：经过大规模生产环境验证，稳定可靠
- **易用性**：提供清晰的接口设计，简化网络编程

#### 3.2.2 Protobuf
- **高效性**：二进制序列化格式，比使用 xml(20倍) 、json(10倍)进行数据交换快许多
- **跨语言支持**：支持Java、C#、C++、Golang、Python多种主流编程语言，便于异构系统集成
- **向前兼容**：支持数据结构的演进，向后兼容
- **代码生成**：自动生成序列化/反序列化代码，减少手动编码错误

#### 3.2.3 Zookeeper
- **服务发现**：提供服务注册与发现机制，实现服务动态发现
- **高可用性**：支持集群部署，提供高可用服务
- **一致性**：保证分布式环境下数据的一致性
- **成熟稳定**：广泛用于大型分布式系统，稳定可靠

## 4、模块设计与实现细节
### 4.1 整体架构
MPRPC框架采用分层架构设计，主要包括以下几层：
1. **网络层**：基于Muduo网络库，处理网络通信
2. **协议层**：定义RPC通信协议，处理数据序列化和反序列化
3. **服务层**：提供服务注册、发现和管理功能
4. **应用层**：提供RPC接口，供业务调用

### 4.2 核心模块
#### 4.2.1 MprpcApplication
- **功能**：应用程序入口，采用单例模式，初始化RPC配置信息
- **实现细节**：
  - 采用懒汉式单例模式，确保全局唯一实例
  - 解析配置文件，获取RPC服务IP和端口
  - 提供全局访问接口，方便其他模块获取配置信息
- **文件结构**：
  - `mprpc_app.h + mprpc_app.cc`：单例模式实现
  - `mprpc_config.h + mprpc_config.cc`：配置文件解析

#### 4.2.2 RpcProvider（服务端）
- **功能**：RPC服务提供者，负责接收RPC请求，调用本地服务并返回结果
- **实现细节**：
  - `NotifyService()`：注册RPC服务方法
    - 维护服务信息结构体`ServiceInfo`，包含服务对象和方法映射
    ```cpp
    // service服务类型信息
    struct ServiceInfo
    {
        google::protobuf::Service *m_service; // 保存服务对象
        std::unordered_map<std::string, const google::protobuf::MethodDescriptor *> m_methodMap; // 保存服务方法
    };
    // 存储注册成功的服务对象和其服务方法的所有信息
    std::unordered_map<std::string, ServiceInfo> m_serviceMap;
    ```
    - 注册service_name及所有method
    - 维护service_info.m_service和service_info.m_methodMap(method_name -> pmethodDesc)
    - 维护m_serviceMap(service_name -> service_info)
  - `Run()`：启动RPC服务节点，开始提供 rpc 远程网络调用服务
    - 读取配置文件获取IP `rpcserverip` 和端口 `rpcserverport`
    - 创建muduo::net::TcpServer对象
        - 绑定连接回调和消息读写回调，分离网络代码和业务代码
        - 设置muduo库线程数量（1个IO线程，4个worker线程）
    - 将`m_serviceMap`和`m_methodMap`注册到`Zookeeper`，实现服务发现
    - 启动网络服务
  - `OnConnection()`：处理连接事件
    - 检测连接状态，断开时关闭连接
    ```cpp
    void RpcProvider::OnConnection(const muduo::net::TcpConnectionPtr &conn)
    {
        if (!conn->connected())
        {
            // 和 rpc client 的连接断开了
            conn->shutdown();
        }
    }
    ```
  - `OnMessage()`：处理RPC调用请求
    - 协商RPC通信协议格式
    - 进行数据头的序列化和反序列化，解决TCP粘包问题
    - 从字符流中解析header_size、header_str和args_size
    - 生成RPC请求和响应参数
    - 绑定回调函数处理RPC响应
    - 具体来说：RpcProvider 需要 service_name, method_name, args，其中 header_str 包括 service_name 以及 method_name，并且进行「数据头」的序列化和反序列化，使用 header_size 规定 header_str 长度，**考虑 TCP 粘包问题，因为 header_str 需要规定一个 arg_size**，因此最终 header_str 包括 header_size + header_str + arg_size

    ```cpp
    // 假设协商之后 RpcProvider 接收到如下数据
    // 16UserServiceLogin14zhangsan 123456
    // header_size + header_str + arg_size
    ```

    1. 从字符流中读取前4个字节的内容 header_size -> header_str -> service_name + method_name + args_size
    2. 获取 rpc 方法参数的字符流数据 args_str
    3. 生成 rpc 方法调用的请求 request 和响应 response 参数，response 是 callee 中 Login 填写的
    4. `google::protobuf::NewCallback` 给下面的 method 方法的调用，绑定一个 Closure 的回调函数 SendRpcResponse

#### 4.2.3 MprpcChannel（客户端）
- **功能**：RPC客户端通道，继承RpcChannel，重写CallMethod方法，主要是 caller 调用的
- **实现细节**：
  - 所有通过stub代理对象调用的RPC方法都经过此方法
  - 统一处理RPC方法调用的数据序列化和网络发送
  - 处理与RpcProvider协商好的通信格式：header_str + args_str
    - header_str = header_size + service_name method_name args_size
    - args_str = args_str
  - 具体步骤：
    1. 定义RPC请求header（获取service_name和method_name）
    2. rpc 调用方想调用 service_name 的 method_name 服务，先查询Zookeeper获取服务所在host信息
    3. 连接RPC服务节点，发送RPC请求
    4. 接收RPC响应值
    5. 反序列化RPC调用响应数据

#### 4.2.4 MprpcController
- **功能**：RPC控制器，维护RPC方法执行过程中的状态信息
- **实现细节**：
  - 在MprpcChannel::CallMethod()中维护
  - 记录RPC调用状态、错误信息等
  - 提供状态查询和设置接口

#### 4.2.5 Zookeeper集成
- **功能**：实现服务注册与发现
- **实现细节**：
  - 使用Zookeeper的C/C++ API
  - 创建服务节点，注册服务信息
  - 提供服务发现接口，获取可用服务列表
  - 实现watcher机制，监听服务状态变化
- **文件结构**：
  - `zookeeperutil.h + zookeeperutil.cc`：Zookeeper操作封装

### 4.3 模块解耦方式
MPRPC框架采用多种设计模式实现模块解耦：
1. **单例模式**：MprpcApplication采用单例模式，确保全局配置一致性
2. **工厂模式**：通过工厂创建RPC服务对象，降低客户端与服务端的耦合
3. **观察者模式**：Zookeeper的watcher机制，实现服务状态变化的自动通知
4. **代理模式**：MprpcChannel作为RPC方法的代理，隐藏网络通信细节
5. **分层架构**：网络层、协议层、服务层、应用层分离，降低模块间依赖
6. **回调机制**：使用回调函数处理异步RPC响应，提高系统灵活性

## 5、RPC调用全过程数据流
### 5.1 服务注册阶段
1. 服务端启动时，通过`RpcProvider::NotifyService()`注册服务
2. 服务端将服务信息（service_name、method_name、host、port等）注册到Zookeeper
3. Zookeeper创建相应服务节点，并保存服务信息

### 5.2 服务发现阶段
1. 客户端启动时，连接Zookeeper
2. 客户端向Zookeeper查询所需服务的可用节点
3. Zookeeper返回可用服务节点列表
4. 客户端缓存服务节点信息，并监听服务节点变化

### 5.3 RPC调用阶段
1. 客户端通过stub代理对象调用RPC方法
2. 调用进入`MprpcChannel::CallMethod()`
3. 构造RPC请求协议头：
   - header_size (4字节)：协议头长度
   - header_str：包含service_name和method_name
   - args_size (4字节)：参数长度
   - args_str：序列化的参数数据
4. 查询Zookeeper获取可用服务节点
5. 建立与服务端的TCP连接
6. 发送RPC请求协议头和参数数据
7. 服务端接收请求，解析协议头和参数
8. 根据service_name和method_name查找对应的服务和方法
9. 调用本地服务方法，传入反序列化后的参数
10. 获取服务方法返回结果
11. 将结果序列化为响应数据
12. 将响应数据发送回客户端
13. 客户端接收响应数据，反序列化得到结果
14. 回调函数处理结果，完成RPC调用

### 5.4 数据流示意图
```
客户端                          网络                          服务端
  |                             |                             |
  |---1.调用RPC方法------------->|                             |
  |                             |                             |
  |<-----2.返回stub------------->|                             |
  |                             |                             |
  |---3.构造请求协议------------>|                             |
  |---4.查询Zookeeper---------->|                             |
  |                             |<-----5.返回服务节点信息-----|
  |                             |                             |
  |---6.建立TCP连接------------->|                             |
  |---7.发送请求协议------------>|                             |
  |                             |---8.解析协议头------------>|
  |                             |---9.查找服务和------------>|
  |                             |     方法                   |
  |                             |<----10.调用本地方法-------|
  |                             |<----11.获取结果-----------|
  |                             |---12.序列化响应---------->|
  |<----13.接收响应--------------|                             |
  |---14.反序列化结果------------>|                             |
  |                             |                             |
```

## 6、Protobuf使用指南
### 6.1 Protobuf简介
protobuf(protocol buffer) 是Google的一种**数据交换**格式，它独立于平台语言。Google提供了protobuf多种语言的实现: Java、C#、C++、Golang和Python，每一种实现都包含了相应语言的编译器以及库文件。

由于它是一种二进制的格式，比使用xml(快20倍)、json(快10倍)进行数据交换快许多。可以把它用于分布式应用之间的数据通信或者异构环境下的数据交换。作为一种效率和兼容性都很优秀的二进制数据传输格式，可以用于诸如网络传输、配置文件、数据存储等诸多领域。

简单来说，protobuf用于RPC的**序列化和反序列化**。

### 6.2 安装Protobuf
安装（libprotoc 3.11.0，最新版本需要使用bazel安装，参考官网）：
```bash
unzip protobuf-master.zip # 1. 解压压缩包
cd protobuf-master # 2. 进入解压后的文件夹
sudo apt-get install autoconf automake libtool curl make g++ unzip  # 3. 安装所需工具
./autogen.sh # 4. 自动生成configure配置文件
./configure  # 5. 配置环境
make # 6. 编译源代码(时间比较长)
make install # 7. 安装到系统
```

### 6.3 Protobuf使用实例
1. 定义.proto文件：
```protobuf
syntax = "proto2";
package fixbug;

message ResultCode {
  int32 errcode = 1;
  string errmsg = 2;
}

message LoginRequest {
  bytes name = 1;
  bytes pwd = 2;
}

message LoginResponse {
  ResultCode result = 1;
  bool success = 2;
}

// 定义服务
service UserServiceRpc {
  rpc Login(LoginRequest) returns(LoginResponse);
}
```

2. 编译.proto文件：
```bash
protoc --cpp_out=. *.proto
```

3. 使用生成的代码：
- 使用实例参考：[test/protobuf](/test/protobuf/main.cc)

## 7、Muduo网络库
### 7.1 Muduo简介
callee通过muduo实现网络高并发处理

### 7.2 Muduo安装
- 安装参考：https://blog.csdn.net/QIANGWEIYUAN/article/details/89023980

### 7.3 Muduo特点
- **基于Reactor模式**：采用事件驱动模型，高效处理高并发连接
- **非阻塞IO**：使用非阻塞IO提高网络吞吐量
- **线程模型**：采用"one loop per thread"模型，每个线程一个事件循环
- **定时器支持**：提供高效的定时器功能
- **缓冲区管理**：提供高效的缓冲区管理机制
- **易于扩展**：清晰的接口设计，便于扩展功能

## 8、Zookeeper集成
### 8.1 Zookeeper简介
Zookeeper是在分布式环境中应用非常广泛，它的优秀功能很多，比如分布式环境中全局命名服务，服务注册中心，全局分布式锁等等。
- zk的数据是怎么组织的 - znode节点
- zk的watcher机制

### 8.2 安装Zookeeper
1. zookeeper-3.4.10$ cd conf
2. zookeeper-3.4.10/conf$ mv zoo_sample.cfg zoo.cfg （修改其中的 dataDir={path}/zookeeper-3.4.10/data）
3. 进入bin目录，启动zkServer， ./zkServer.sh start
4. 可以通过 netstat 查看 zkServer 的端口，在 bin 目录启动 zkClient.sh 链接 zkServer，熟悉 zookeeper 怎么组织节点

### 8.3 安装ZooKeeper C/C++ API
1. cd src/c
2. sudo ./configure
3. sudo make (如果出现 [-Werror=format-overflow=] 就将 Makefile 文件的 -Werror 去掉)
4. sudo make install

主要关注zookeeper怎么管理节点，zk-c API怎么创建节点，获取节点，删除节点以及watcher机制的 API编程
- 使用实例参考: [zookeeperutil.h](src/include/zookeeperutil.h) 以及 [zookeeperutil.cc](src/zookeeperutil.cc)

## 9、框架概览
![rpc.png](https://s2.loli.net/2023/09/17/ANKWm5LOyJv4kxU.png)
![rpc-process.png](https://s2.loli.net/2023/09/17/iJwxthanRUOq9ur.png)

- [CMakeLists.txt](/CMakeLists.txt): 注意多层目录的 build，体会 CMakeLists 在其中的用法
- [autobuild.sh](/autobuild.sh): 一键构建