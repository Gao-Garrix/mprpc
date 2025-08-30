# MPRPC服务提供方模块（callee）实现详解

## 1. 总体功能描述

MPRPC服务提供方模块（callee）是整个RPC框架的核心组件之一，负责接收来自客户端的RPC调用请求，解析请求参数，调用本地服务方法，并将处理结果返回给客户端。该模块基于Muduo网络库实现高性能的网络通信，结合Zookeeper实现服务注册与发现功能，采用Protobuf进行数据序列化和反序列化。

主要功能包括：
1. **服务注册**：将提供的服务信息注册到Zookeeper，供客户端发现
2. **网络通信**：基于Muduo网络库，处理客户端连接和RPC请求
3. **请求处理**：解析RPC请求，调用相应的服务方法
4. **响应生成**：将服务方法的结果序列化后返回给客户端
5. **错误处理**：处理各种异常情况，返回错误信息

## 2. 模块架构

服务提供方模块主要由以下几个核心组件构成：
- **MprpcApplication**：应用程序入口，负责初始化RPC框架
- **RpcProvider**：服务提供者核心类，处理RPC请求和响应
- **ServiceInfo**：服务信息结构体，存储服务对象和方法映射
- **Zookeeper集成**：实现服务注册与发现

## 3. 核心组件实现

### 3.1 MprpcApplication类

#### 3.1.1 类定义
```cpp
class MprpcApplication
{
public:
    static void Init(int argc, char **argv);
    static MprpcApplication &GetInstance();
    MprpcConfig &GetConfig();
    
private:
    MprpcApplication();
    ~MprpcApplication();
    
    static MprpcApplication m_app;
    MprpcConfig m_config;
};
```

#### 3.1.2 功能描述
MprpcApplication是应用程序的入口，采用单例模式，负责初始化RPC框架，包括：
- 解析命令行参数
- 加载配置文件
- 初始化RPC环境

#### 3.1.3 初始化实现
```cpp
void MprpcApplication::Init(int argc, char **argv)
{
    if (argc < 2)
    {
        std::cout << "config file is missing!" << std::endl;
        exit(EXIT_FAILURE);
    }
    
    // 配置文件路径
    std::string configfile = argv[1];
    m_config.LoadConfigFile(configfile.c_str());
    
    // 加载配置项
    std::string ip = m_config.Load("rpcserverip");
    uint16_t port = atoi(m_config.Load("rpcserverport").c_str());
    std::string zookeeperip = m_config.Load("zookeeperip");
    std::string zookeeperport = m_config.Load("zookeeperport");
    
    // 初始化Zookeeper连接
    ZkClient zk;
    if (!zk.Start())
    {
        std::cout << "zookeeper start error!" << std::endl;
        exit(EXIT_FAILURE);
    }
    
    // 创建Zookeeper节点
    std::string path = "/";
    zk.Create(path, "", 0);
    
    // 启动RPC服务
    RpcProvider provider;
    provider.Run(ip, port);
}
```

#### 3.1.4 单例实现
```cpp
MprpcApplication &MprpcApplication::GetInstance()
{
    static MprpcApplication app;
    return app;
}
```

采用懒汉式单例模式，确保全局只有一个MprpcApplication实例。

### 3.2 RpcProvider类

#### 3.2.1 类定义
```cpp
class RpcProvider
{
public:
    void Run(char *ip, uint16_t port);
    void NotifyService(google::protobuf::Service *service);
    
private:
    // service服务类型信息
    struct ServiceInfo
    {
        google::protobuf::Service *m_service; // 保存服务对象
        std::unordered_map<std::string, const google::protobuf::MethodDescriptor *> m_methodMap; // 保存服务方法
    };
    
    // 存储注册成功的服务对象和其服务方法的所有信息
    std::unordered_map<std::string, ServiceInfo> m_serviceMap;
    
    // 网络相关
    muduo::net::EventLoop m_eventLoop;
    muduo::net::TcpServer m_server;
    
    // Zookeeper相关
    ZkClient m_zkClient;
    
    // 回调函数
    void OnConnection(const muduo::net::TcpConnectionPtr &conn);
    void OnMessage(const muduo::net::TcpConnectionPtr &conn, muduo::net::Buffer *buffer, muduo::Timestamp time);
    void SendRpcResponse(const muduo::net::TcpConnectionPtr &conn, google::protobuf::Message *response);
};
```

#### 3.2.2 Run方法 - 启动RPC服务
```cpp
void RpcProvider::Run(char *ip, uint16_t port)
{
    // 读取配置文件 rpcserverip 以及 rpcserverport
    std::string ip_str = ip;
    uint16_t port_int = port;
    
    muduo::net::InetAddress addr(ip_str, port_int);
    
    // 创建 TcpServer 对象
    m_server = muduo::net::TcpServer(&m_eventLoop, addr, "RpcProvider");
    
    // 绑定连接回调和消息读写回调方法, 分离网络代码和业务代码
    m_server.setConnectionCallback(std::bind(&RpcProvider::OnConnection, this, std::placeholders::_1));
    m_server.setMessageCallback(std::bind(&RpcProvider::OnMessage, this, std::placeholders::_1, 
                                         std::placeholders::_2, std::placeholders::_3));
    m_server.setThreadNum(4); // 设置 muduo 库的线程数量, 1个IO线程，3个worker线程
    
    // 将之前维护的 m_serviceMap 以及 m_methodMap 注册到 zookeeper 上
    // 让 rpc client(caller) 可以从 zk 上发现服务
    for (auto &sp : m_serviceMap)
    {
        std::string service_name = sp.first;
        std::string service_path = "/" + service_name;
        m_zkClient.Create(service_path, "", 0);
        
        for (auto &mp : sp.second.m_methodMap)
        {
            std::string method_name = mp.first;
            std::string method_path = service_path + "/" + method_name;
            char node_path[128] = {0};
            sprintf(node_path, "%s/%s:%d", method_path.c_str(), ip_str.c_str(), port_int);
            m_zkClient.Create(method_path, node_path, ZOO_EPHEMERAL | ZOO_SEQUENCE);
        }
    }
    
    // 启动网络服务
    m_server.start();
    m_eventLoop.loop();
}
```

功能描述：
1. 创建TCP服务器，绑定IP和端口
2. 设置连接回调和消息读写回调函数
3. 设置工作线程数（1个IO线程，3个worker线程）
4. 将服务信息注册到Zookeeper
5. 启动网络服务，进入事件循环

#### 3.2.3 NotifyService方法 - 注册服务
```cpp
void RpcProvider::NotifyService(google::protobuf::Service *service)
{
    ServiceInfo service_info;
    service_info.m_service = service;
    
    // 获取服务对象的描述信息
    const google::protobuf::ServiceDescriptor *pserviceDesc = service->GetDescriptor();
    std::string service_name = pserviceDesc->name();
    
    // 获取服务方法数量
    int method_count = pserviceDesc->method_count();
    for (int i = 0; i < method_count; ++i)
    {
        // 获取服务方法描述
        const google::protobuf::MethodDescriptor *pmethodDesc = pserviceDesc->method(i);
        std::string method_name = pmethodDesc->name();
        service_info.m_methodMap.insert({method_name, pmethodDesc});
    }
    
    // 注册 service_name 以及所有的 method
    // 维护 service_info.m_service 以及 service_info.m_methodMap (method_name -> pmethodDesc)
    // 维护 m_serviceMap (service_name -> service_info)
    m_serviceMap.insert({service_name, service_info});
}
```

功能描述：
1. 接收服务对象，获取服务描述信息
2. 遍历服务中的所有方法，建立方法名到方法描述的映射
3. 将服务信息存储到m_serviceMap中，供后续处理RPC请求时使用

#### 3.2.4 OnConnection方法 - 处理连接事件
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

功能描述：
1. 检测连接状态
2. 如果连接断开，关闭连接

#### 3.2.5 OnMessage方法 - 处理RPC请求
```cpp
void RpcProvider::OnMessage(const muduo::net::TcpConnectionPtr &conn, muduo::net::Buffer *buffer, muduo::Timestamp time)
{
    // 1. 从buffer中读取前4个字节的数据，即header_size
    uint32_t header_size = 0;
    buffer->readBytes(&header_size, 4);
    // 2. 根据header_size读取header_str
    std::string rpc_header_str = buffer->readBytes(header_size);
    // 3. 从rpc_header_str中解析出service_name和method_name
    std::string service_name;
    std::string method_name;
    uint32_t args_size = 0;
    
    // 解析协议头
    size_t pos = rpc_header_str.find(":");
    if (pos == std::string::npos)
    {
        std::cout << "rpc header format error!" << std::endl;
        return;
    }
    service_name = rpc_header_str.substr(0, pos);
    method_name = rpc_header_str.substr(pos + 1);
    
    // 4. 读取arg_size
    buffer->readBytes(&args_size, 4);
    // 5. 根据arg_size读取args_str
    std::string args_str = buffer->readBytes(args_size);
    
    // 6. 获取service对象和method对象
    auto iter = m_serviceMap.find(service_name);
    if (iter == m_serviceMap.end())
    {
        std::cout << service_name << " is not exist!" << std::endl;
        return;
    }
    
    auto service_info = iter->second;
    auto method_iter = service_info.m_methodMap.find(method_name);
    if (method_iter == service_info.m_methodMap.end())
    {
        std::cout << service_name << ":" << method_name << " is not exist!" << std::endl;
        return;
    }
    
    google::protobuf::Service *service = service_info.m_service;
    const google::protobuf::MethodDescriptor *method = method_iter->second;
    
    // 7. 创建请求对象和响应对象
    google::protobuf::Message *request = service->GetRequestPrototype(method).New();
    google::protobuf::Message *response = service->GetResponsePrototype(method).New();
    
    // 8. 将args_str反序列化到request对象中
    request->ParseFromString(args_str);
    
    // 9. 调用rpc方法，执行业务逻辑
    // 绑定一个Closure的回调函数 SendRpcResponse
    google::protobuf::Closure *done = google::protobuf::NewCallback<RpcProvider, 
        const muduo::net::TcpConnectionPtr&, google::protobuf::Message*>(
        this, &RpcProvider::SendRpcResponse, conn, response);
    
    // 在service上调用method，传入request和response，以及回调函数done
    service->CallMethod(method, nullptr, request, response, done);
}
```

功能描述：
1. 从网络缓冲区中读取RPC请求协议头和参数数据
2. 解析协议头，获取服务名和方法名
3. 根据服务名和方法名查找对应的服务对象和方法描述
4. 创建请求对象和响应对象
5. 将参数数据反序列化到请求对象中
6. 调用服务方法，传入请求对象、响应对象和回调函数
7. 使用回调函数处理RPC响应

#### 3.2.6 SendRpcResponse方法 - 发送RPC响应
```cpp
void RpcProvider::SendRpcResponse(const muduo::net::TcpConnectionPtr &conn, google::protobuf::Message *response)
{
    std::string response_str;
    response->SerializeToString(&response_str);
    
    // 序列化后，将response_str通过网络发送给调用方
    conn->send(response_str);
    
    // 关闭连接
    conn->shutdown();
}
```

功能描述：
1. 将响应对象序列化为字符串
2. 将序列化后的响应数据通过网络发送给客户端
3. 关闭连接

## 4. 服务注册与发现

### 4.1 服务注册实现
```cpp
// 在Run方法中注册服务到Zookeeper
for (auto &sp : m_serviceMap)
{
    std::string service_name = sp.first;
    std::string service_path = "/" + service_name;
    m_zkClient.Create(service_path, "", 0);
    
    for (auto &mp : sp.second.m_methodMap)
    {
        std::string method_name = mp.first;
        std::string method_path = service_path + "/" + method_name;
        char node_path[128] = {0};
        sprintf(node_path, "%s/%s:%d", method_path.c_str(), ip_str.c_str(), port_int);
        m_zkClient.Create(method_path, node_path, ZOO_EPHEMERAL | ZOO_SEQUENCE);
    }
}
```

功能描述：
1. 为每个服务创建Zookeeper节点
2. 为每个服务方法创建Zookeeper节点，节点中包含服务提供者的IP和端口
3. 使用ZOO_EPHEMERAL标志确保节点与服务进程生命周期关联
4. 使用ZOO_SEQUENCE标志为节点添加序列号，确保节点名唯一

### 4.2 服务发现机制
服务提供方不需要直接实现服务发现功能，而是通过注册服务到Zookeeper，让客户端（caller）能够发现服务。服务发现机制主要由客户端实现，服务提供方只需要确保服务信息正确注册到Zookeeper即可。

## 5. 错误处理机制

### 5.1 服务不存在处理
```cpp
auto iter = m_serviceMap.find(service_name);
if (iter == m_serviceMap.end())
{
    std::cout << service_name << " is not exist!" << std::endl;
    return;
}
```

当请求的服务不存在时，记录错误日志并返回。

### 5.2 方法不存在处理
```cpp
auto method_iter = service_info.m_methodMap.find(method_name);
if (method_iter == service_info.m_methodMap.end())
{
    std::cout << service_name << ":" << method_name << " is not exist!" << std::endl;
    return;
}
```

当请求的方法不存在时，记录错误日志并返回。

### 5.3 协议格式错误处理
```cpp
size_t pos = rpc_header_str.find(":");
if (pos == std::string::npos)
{
    std::cout << "rpc header format error!" << std::endl;
    return;
}
```

当RPC请求协议格式错误时，记录错误日志并返回。

## 6. 使用示例

### 6.1 定义服务接口
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

### 6.2 实现服务接口
```cpp
// UserServiceRpcRpcServiceImpl.h
#pragma once
#include "UserServiceRpc.pb.h"
#include "mprpcapplication.h"

class UserServiceRpcRpcServiceImpl : public fixbug::UserServiceRpcRpc
{
public:
    bool Login(const fixbug::LoginRequest *request, fixbug::LoginResponse *response);
};

// UserServiceRpcRpcServiceImpl.cc
#include "UserServiceRpcRpcServiceImpl.h"
#include "logger.h"

bool UserServiceRpcRpcServiceImpl::Login(const fixbug::LoginRequest *request, fixbug::LoginResponse *response)
{
    std::string name = request->name();
    std::string pwd = request->pwd();
    
    LOG_INFO("login request: name=%s, pwd=%s", name.c_str(), pwd.c_str());
    
    // 业务逻辑处理
    if (name == "zhang san" && pwd == "123456")
    {
        response->mutable_result()->set_errcode(0);
        response->mutable_result()->set_errmsg("login success!");
        response->set_success(true);
    }
    else
    {
        response->mutable_result()->set_errcode(-1);
        response->mutable_result()->set_errmsg("login failed: user name or password error!");
        response->set_success(false);
    }
    
    LOG_INFO("login response: success=%d, errcode=%d, errmsg=%s", 
             response->success(), response->result().errcode(), response->result().errmsg().c_str());
    
    return true;
}
```

### 6.3 启动RPC服务
```cpp
#include "UserServiceRpcRpcServiceImpl.h"
#include "mprpcapplication.h"
#include "rpcprovider.h"
#include "logger.h"

int main(int argc, char **argv)
{
    // 调用框架的初始化函数
    MprpcApplication::Init(argc, argv);
    
    // provider是一个rpc网络服务对象。把UserServiceRpc对象发布到rpc节点上
    RpcProvider provider;
    provider.NotifyService(new UserServiceRpcRpcServiceImpl());
    
    // 启动一个rpc服务发布节点    Run以后，进程进入阻塞状态，等待远程的rpc调用请求
    provider.Run("127.0.0.1", 8000);
    
    return 0;
}
```

## 7. 总结

MPRPC服务提供方模块（callee）通过以下方式实现了高效的RPC服务提供：

1. **模块化设计**：将网络通信、服务注册、请求处理等功能分离，便于维护和扩展
2. **高性能网络**：基于Muduo网络库，采用非阻塞IO和事件驱动模型，支持高并发处理
3. **服务注册**：将服务信息注册到Zookeeper，实现服务的自动发现
4. **协议解析**：自定义RPC协议，解决TCP粘包问题，支持参数序列化和反序列化
5. **异步处理**：使用回调函数处理RPC响应，提高系统吞吐量
6. **错误处理**：完善的错误处理机制，确保系统稳定性

通过以上设计，MPRPC服务提供方模块能够高效地处理来自客户端的RPC请求，为分布式系统提供可靠的服务调用能力。
