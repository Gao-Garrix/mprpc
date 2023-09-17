## Muduo Protobuf Remote Procedure Call (MPRPC)

### 1、文件结构

```bash
.
├── autobuild.sh
├── bin     # 可执行文件目录
├── build   # make 构建目录
├── CMakeLists.txt
├── example # RPC caller 以及 callee 示例
├── lib     # libmprpc.a 静态库
├── README.md
├── src     # MPRPC 源码
└── test    # protobuf 使用实例
```

### 2、Protobuf

protobuf(protocol buffer) 是 google 的一种**数据交换**的格式，它独立于平台语言。google 提供了protobuf多种语言的实现: Java、C#、C++、Golang 和 Python，每一种实现都包含了相应语 言的编译器以及库文件。

由于它是一种二进制的格式，比使用 xml(20倍) 、json(10倍)进行数据交换快许多。可以把它用于分布式应用之间的数据通信或者异构环境下的数据交换。作为一种效率和兼容性都很优秀的二进制数据传输格式，可以用于诸如网络传输、配置文件、数据存储等诸多领域。

> 简单来说 protobuf 用于 rpc 的**序列化和反序列化**

安装（libprotoc 3.11.0，最新版本需要使用 bazel 安装，参考官网）：
```bash
unzip protobuf-master.zip # 1. 解压压缩包
cd protobuf-master # 2. 进入解压后的文件夹
sudo apt-get install autoconf automake libtool curl make g++ unzip  # 3. 安装所需工具
./autogen.sh # 4. 自动生成configure配置文件
./configure  # 5. 配置环境
make # 6. 编译源代码(时间比较长)
```

- 使用实例参考：[test/protobuf](/test/protobuf/main.cc)


### 3、Muduo

callee 通过 muduo 实现网络高并发处理

- 安装参考：https://blog.csdn.net/QIANGWEIYUAN/article/details/89023980


### 4、MPRPC

#### MprpcApplication

单例模式，初始化 RPC 信息，主要包括解析 IP 以及 Port

- mprpc_app.h + mprpc_app.cc：懒汉式单例

- mprpc_config.h + mprpc_config.cc：读取配置文件

  

#### [RpcServer] RpcProvider

##### `RpcProvider::NotifyService()`
> 用于发布 rpc 方法的函数接口

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

- 注册 service_name 以及所有的 method
- 维护 service_info.m_service 以及 service_info.m_methodMap (method_name -> pmethodDesc)
- 维护 m_serviceMap (service_name -> service_info)

##### `RpcProvider::Run()`
> 启动 rpc 服务节点，开始提供 rpc 远程网络调用服务

- 读取配置文件 rpcserverip 以及 rpcserverport
- 创建 muduo::net::TcpServer 对象
    - 绑定连接回调和消息读写回调方法, 分离网络代码和业务代码
    - 设置 muduo 库的线程数量, 1个IO线程，4个worker线程
- 将 之前维护的 m_serviceMap 以及 m_methodMap 注册到 zookeeper 上，让 rpc client(caller) 可以从 zk 上发现服务
    > 为什么不直接让 rpc client 读取配置文件呢，因为在分布式场景下，某些 rpc 节点 (微服务) 可能挂掉，但是还来不及更新配置文件，直接交给 zookeeper 让其自动管理节点（心跳机制）

- 启动网络服务: `server.start(); m_eventLoop.loop();`


##### `RpcProvider::OnConnection()`
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

##### `RpcProvider::OnMessage()`

> 1. 已建立连接用户的读写事件回调, 如果远程有一个 rpc 服务的调用请求, 那么 OnMessage 方法就会响应
在框架内部，RpcProvider 和 RpcConsumer 协商好之间通信用的 protobuf 数据类型格式
> 2. 定义 proto 的 message 类型，进行数据头的序列化和反序列化
header_str (+ args_str) = header_size(4个字节) + service_name + method_name + args_size (+ args_str)
> 3. 例如: 16UserServiceLogin14zhang san123456
10 "10" 和 1000 "1000" 字节数统一成 4字节 => uint32
> 4. std::string  insert 和 copy 方法

- OnMessage 回调函数中协商好 RpcProvider 和 RpcConsumer 之间通信用的 protobuf 数据类型

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


#### [RpcClient] MprpcChannel

> 继承 RpcChannel 重写 CallMethod 方法，主要是是 caller 调用的

所有通过stub代理对象调用的rpc方法，都走到这里了，统一做rpc方法调用的数据数据序列化和网络发送
处理如下的和 rpc_provider 商量好的通信格式: header_str + args_str
- header_str = header_size + service_name method_name args_size
- args_str = args_str

具体步骤为:
1. 定义 rpc 的请求 header (首先获取 service_name 以及 method_name)
2. rpc 调用方想调用 service_name 的 method_name 服务，需要查询 zk 上该服务所在的 host 信息
3. 连接 rpc 服务节点，发送 rpc 请求
4. 接收 rpc 请求的响应值
5. 反序列化rpc调用的响应数据


#### MprpcController

> 主要用于 RPC 方法执行过程中状态信息的维护，在 MprpcChannel::CallMethod() 中维护


#### zookeeper
Zookeeper是在分布式环境中应用非常广泛，它的优秀功能很多，比如分布式环境中全局命名服务，服务注册中心，全局分布式锁等等。
- zk的数据是怎么组织的 - znode节点
- zk的watcher机制

**安装 zk**
1. zookeeper-3.4.10$ cd conf
2. zookeeper-3.4.10/conf$ mv zoo_sample.cfg zoo.cfg （修改其中的 dataDir={path}/zookeeper-3.4.10/data）
3. 进入bin目录，启动zkServer， ./zkServer.sh start
4. 可以通过 netstat 查看 zkServer 的端口，在 bin 目录启动 zkClient.sh 链接 zkServer，熟悉 zookeeper 怎么组织节点

**安装 zk C/C++ API**
1. cd src/c
2. sudo ./confifure
3. sudo make (如果出现 [-Werror=format-overflow=] 就将 Makefile 文件的 -Werror 去掉)
4. sudo make install

主要关注zookeeper怎么管理节点，zk-c API怎么创建节点，获取节点，删除节点以及watcher机制的 API编程
- 使用实例参考: [zookeeperutil.h](src/include/zookeeperutil.h) 以及 [zookeeperutil.cc](src/zookeeperutil.cc)


### 5、框架构建

![rpc.png](https://s2.loli.net/2023/09/17/ANKWm5LOyJv4kxU.png)

- [CMakeLists.txt](/CMakeLists.txt): 注意多层目录的 build，体会 CMakeLists 在其中的用法

- [autobuild.sh](/autobuild.sh): 一键构建

### 6、参考
- 施磊——【高级】C++项目-实现分布式网络通信框架-rpc通信原理
- 剖析 muduo 网络库：https://github.com/EricPengShuai/muduo