# MPRPC服务调用方模块（caller）实现详解

## 1. 总体功能描述

MPRPC服务调用方模块（caller）是整个RPC框架的核心组件之一，负责向服务提供方发起RPC调用请求，传递参数，接收并处理服务方的响应结果。该模块基于Muduo网络库实现网络通信，结合Zookeeper实现服务发现功能，采用Protobuf进行数据序列化和反序列化。

主要功能包括：
1. **服务发现**：从Zookeeper获取可用的服务提供方信息
2. **网络通信**：基于Muduo网络库，与服务提供方建立连接
3. **请求构建**：构建RPC请求，包含服务名、方法名和参数
4. **请求发送**：将RPC请求发送给服务提供方
5. **响应处理**：接收并解析服务提供方的响应，返回结果

## 2. 模块架构

服务调用方模块主要由以下几个核心组件构成：
- **MprpcChannel**：RPC调用通道，继承自RpcChannel，重写CallMethod方法
- **MprpcController**：RPC控制器，维护RPC调用过程中的状态信息
- **Zookeeper集成**：实现服务发现功能

## 3. 核心组件实现

### 3.1 MprpcChannel类

#### 3.1.1 类定义
```cpp
class MprpcChannel : public google::protobuf::RpcChannel
{
public:
    MprpcChannel();
    virtual ~MprpcChannel();
    
    // 重写CallMethod方法，所有通过stub代理对象调用的rpc方法，都走到这里了
    // 统一做rpc方法调用的数据序列化和网络发送
    virtual void CallMethod(const google::protobuf::MethodDescriptor *method,
                           google::protobuf::RpcController *controller,
                           const google::protobuf::Message *request,
                           google::protobuf::Message *response,
                           google::protobuf::Closure *done);
    
    // 设置Zookeeper连接信息
    void SetZookeeperInfo(const std::string &zookeeperip, const std::string &zookeeperport);
    
private:
    // Zookeeper相关
    ZkClient m_zkClient;
    
    // 网络相关
    muduo::net::EventLoop m_eventLoop;
    muduo::net::TcpConnectionPtr m_conn;
    
    // RPC响应缓冲区
    muduo::net::Buffer m_buffer;
    
    // 状态变量
    bool m_connected;
};
```

#### 3.1.2 CallMethod方法 - 核心RPC调用实现
```cpp
void MprpcChannel::CallMethod(const google::protobuf::MethodDescriptor *method,
                             google::protobuf::RpcController *controller,
                             const google::protobuf::Message *request,
                             google::protobuf::Message *response,
                             google::protobuf::Closure *done)
{
    // 1. 定义rpc的请求header (首先获取service_name以及method_name)
    std::string service_name = method->service()->name();
    std::string method_name = method->name();
    
    // 2. rpc调用方想调用service_name的method_name服务，需要查询zk上该服务所在的host信息
    std::string path = "/" + service_name + "/" + method_name;
    std::string node_path = m_zkClient.GetNodeData(path);
    if (node_path.empty())
    {
        controller->SetFailed("rpc service not found!");
        return;
    }
    
    // 解析node_path，获取host和port
    size_t pos = node_path.find(":");
    if (pos == std::string::npos)
    {
        controller->SetFailed("rpc node path format error!");
        return;
    }
    
    std::string host = node_path.substr(0, pos);
    uint16_t port = atoi(node_path.substr(pos + 1).c_str());
    
    // 3. 连接rpc服务节点
    muduo::net::InetAddress addr(host, port);
    m_conn = muduo::net::TcpConnectionPtr(
        new muduo::net::TcpConnection(&m_eventLoop, addr, "RpcClient"));
    
    // 设置连接回调
    m_conn->setConnectionCallback(
        [this](const muduo::net::TcpConnectionPtr &conn) {
            if (conn->connected())
            {
                m_connected = true;
                // 连接建立成功，发送RPC请求
                this->SendRpcRequest();
            }
            else
            {
                m_connected = false;
            }
        });
    
    // 设置消息回调
    m_conn->setMessageCallback(
        [this](const muduo::net::TcpConnectionPtr &conn, muduo::net::Buffer *buffer, muduo::Timestamp time) {
            // 接收rpc请求的响应值
            std::string response_str = buffer->retrieveAllAsString();
            response->ParseFromString(response_str);
            
            // 如果有回调函数，执行回调
            if (done)
            {
                done->Run();
            }
        });
    
    // 设置关闭回调
    m_conn->setCloseCallback(
        [this](const muduo::net::TcpConnectionPtr &conn) {
            m_eventLoop.quit();
        });
    
    // 启动连接
    m_conn->connect();
    m_eventLoop.loop();
}
```

功能描述：
1. 获取服务名和方法名
2. 从Zookeeper查询服务提供方信息
3. 解析服务提供方的IP和端口
4. 建立与服务提供方的TCP连接
5. 设置连接、消息和关闭回调函数
6. 发送RPC请求
7. 接收并解析RPC响应
8. 执行回调函数（如果有）

#### 3.1.3 SendRpcRequest方法 - 发送RPC请求
```cpp
void MprpcChannel::SendRpcRequest()
{
    // 1. 定义rpc的请求header
    std::string service_name = method_->service()->name();
    std::string method_name = method_->name();
    
    // 2. 序列化请求参数
    std::string args_str;
    request_->SerializeToString(&args_str);
    
    // 3. 构造rpc请求协议
    std::string rpc_header_str = service_name + ":" + method_name;
    uint32_t header_size = rpc_header_str.size();
    uint32_t args_size = args_str.size();
    
    // 4. 将协议头和参数数据发送给服务提供方
    m_buffer.append((char*)&header_size, 4);
    m_buffer.append(rpc_header_str);
    m_buffer.append((char*)&args_size, 4);
    m_buffer.append(args_str);
    
    // 5. 发送数据
    m_conn->send(&m_buffer);
}
```

功能描述：
1. 构造RPC请求协议头
2. 序列化请求参数
3. 将协议头和参数数据组合成完整的RPC请求
4. 通过TCP连接发送RPC请求

#### 3.1.4 SetZookeeperInfo方法 - 设置Zookeeper连接信息
```cpp
void MprpcChannel::SetZookeeperInfo(const std::string &zookeeperip, const std::string &zookeeperport)
{
    m_zkClient.Start();
    std::string path = "/";
    m_zkClient.Create(path, "", 0);
}
```

功能描述：
1. 初始化Zookeeper连接
2. 创建根节点（如果不存在）

### 3.2 MprpcController类

#### 3.2.1 类定义
```cpp
class MprpcController : public google::protobuf::RpcController
{
public:
    MprpcController();
    virtual ~MprpcController();
    
    // 重写RpcController的虚函数
    virtual void Reset();
    virtual bool Failed() const;
    virtual std::string ErrorText() const;
    virtual void SetFailed(const std::string &reason);
    virtual void StartCancel();
    virtual bool IsCanceled() const;
    virtual void NotifyOnCancel(google::protobuf::Closure* callback);
    
private:
    bool m_failed;      // 是否失败
    std::string m_errorText; // 错误信息
};
```

#### 3.2.2 功能实现
```cpp
void MprpcController::Reset()
{
    m_failed = false;
    m_errorText = "";
}

bool MprpcController::Failed() const
{
    return m_failed;
}

std::string MprpcController::ErrorText() const
{
    return m_errorText;
}

void MprpcController::SetFailed(const std::string &reason)
{
    m_failed = true;
    m_errorText = reason;
}

void MprpcController::StartCancel()
{
    // RPC调用取消
}

bool MprpcController::IsCanceled() const
{
    return false;
}

void MprpcController::NotifyOnCancel(google::protobuf::Closure* callback)
{
    // 设置取消回调
}
```

功能描述：
MprpcController继承自google::protobuf::RpcController，用于维护RPC调用过程中的状态信息，包括：
1. 调用是否失败（Failed）
2. 错误信息（ErrorText）
3. 取消状态（IsCanceled）
4. 取消回调（NotifyOnCancel）

## 4. 服务发现机制

### 4.1 服务发现实现
```cpp
// 在CallMethod方法中实现服务发现
std::string path = "/" + service_name + "/" + method_name;
std::string node_path = m_zkClient.GetNodeData(path);
if (node_path.empty())
{
    controller->SetFailed("rpc service not found!");
    return;
}

// 解析node_path，获取host和port
size_t pos = node_path.find(":");
if (pos == std::string::npos)
{
    controller->SetFailed("rpc node path format error!");
    return;
}

std::string host = node_path.substr(0, pos);
uint16_t port = atoi(node_path.substr(pos + 1).c_str());
```

功能描述：
1. 根据服务名和方法名构建Zookeeper节点路径
2. 从Zookeeper获取节点数据（包含服务提供方的IP和端口）
3. 解析节点数据，提取服务提供方的IP和端口
4. 如果服务不存在或节点格式错误，设置错误状态并返回

### 4.2 服务缓存机制
为了提高性能，服务调用方可以缓存已发现的服务信息，避免每次调用都查询Zookeeper。这可以通过以下方式实现：

```cpp
// 在MprpcChannel类中添加服务缓存
std::unordered_map<std::string, std::string> m_serviceCache;

// 在CallMethod方法中添加缓存逻辑
if (m_serviceCache.find(path) != m_serviceCache.end())
{
    node_path = m_serviceCache[path];
}
else
{
    node_path = m_zkClient.GetNodeData(path);
    if (!node_path.empty())
    {
        m_serviceCache[path] = node_path;
    }
}
```

功能描述：
1. 在MprpcChannel中维护一个服务缓存映射
2. 在调用RPC方法时，先检查缓存中是否存在服务信息
3. 如果缓存中存在，直接使用缓存的服务信息
4. 如果缓存中不存在，从Zookeeper查询并更新缓存

## 5. 错误处理机制

### 5.1 服务不存在错误处理
```cpp
if (node_path.empty())
{
    controller->SetFailed("rpc service not found!");
    return;
}
```

当请求的服务不存在时，设置错误状态并返回。

### 5.2 节点格式错误处理
```cpp
size_t pos = node_path.find(":");
if (pos == std::string::npos)
{
    controller->SetFailed("rpc node path format error!");
    return;
}
```

当服务节点格式错误时，设置错误状态并返回。

### 5.3 连接错误处理
```cpp
// 在连接回调中处理连接错误
m_conn->setConnectionCallback(
    [this](const muduo::net::TcpConnectionPtr &conn) {
        if (conn->connected())
        {
            m_connected = true;
            // 连接建立成功，发送RPC请求
            this->SendRpcRequest();
        }
        else
        {
            m_connected = false;
            controller_->SetFailed("rpc connection failed!");
        }
    });
```

当与服务提供方连接失败时，设置错误状态并返回。

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

### 6.2 创建RPC客户端
```cpp
#include "UserServiceRpc.pb.h"
#include "mprpcchannel.h"
#include "mprpccontroller.h"

int main(int argc, char **argv)
{
    // 1. 初始化RPC环境
    MprpcApplication::Init(argc, argv);
    
    // 2. 创建RPC通道
    MprpcChannel channel;
    channel.SetZookeeperInfo("127.0.0.1", "2181");
    
    // 3. 创建RPC控制器
    MprpcController controller;
    
    // 4. 创建存根对象
    fixbug::UserServiceRpc_Stub stub(&channel);
    
    // 5. 创建请求和响应对象
    fixbug::LoginRequest request;
    fixbug::LoginResponse response;
    
    // 6. 设置请求参数
    request.set_name("Alice");
    request.set_pwd("123456");
    
    // 7. 调用RPC方法
    stub.Login(&controller, &request, &response, nullptr);
    
    // 8. 检查调用结果
    if (controller.Failed())
    {
        std::cout << "RPC调用失败: " << controller.ErrorText() << std::endl;
    }
    else
    {
        std::cout << "RPC调用成功: " << response.success() << std::endl;
    }
    
    return 0;
}
```

功能描述：
1. 初始化RPC环境
2. 创建RPC通道，设置Zookeeper连接信息
3. 创建RPC控制器
4. 创建服务存根对象
5. 创建请求和响应对象
6. 设置请求参数
7. 调用RPC方法
8. 检查调用结果并输出

## 7. 性能优化

### 7.1 连接池
为了提高性能，可以使用连接池复用TCP连接，避免每次调用都建立新连接：

```cpp
class RpcConnectionPool
{
public:
    static RpcConnectionPool& GetInstance()
    {
        static RpcConnectionPool instance;
        return instance;
    }
    
    muduo::net::TcpConnectionPtr GetConnection(const std::string &host, uint16_t port)
    {
        std::string key = host + ":" + std::to_string(port);
        
        // 检查连接池中是否有可用连接
        auto it = m_connections.find(key);
        if (it != m_connections.end() && it->second->connected())
        {
            return it->second;
        }
        
        // 没有可用连接，创建新连接
        muduo::net::InetAddress addr(host, port);
        muduo::net::TcpConnectionPtr conn(
            new muduo::net::TcpConnection(&m_eventLoop, addr, "RpcClient"));
        
        // 设置连接回调
        conn->setConnectionCallback(
            [this](const muduo::net::TcpConnectionPtr &conn) {
                if (conn->connected())
                {
                    m_connections[conn->name()] = conn;
                }
                else
                {
                    m_connections.erase(conn->name());
                }
            });
        
        // 启动连接
        conn->connect();
        
        return conn;
    }
    
private:
    RpcConnectionPool() : m_eventLoop()
    {
        // 启动事件循环
        std::thread t([&](){
            m_eventLoop.loop();
        });
        t.detach();
    }
    
    std::unordered_map<std::string, muduo::net::TcpConnectionPtr> m_connections;
    muduo::net::EventLoop m_eventLoop;
};
```

功能描述：
1. 使用单例模式创建连接池
2. 根据host和port获取TCP连接
3. 如果连接池中已有可用连接，直接返回
4. 如果没有可用连接，创建新连接并添加到连接池
5. 使用单独的事件循环管理所有连接

### 7.2 异步调用
为了提高性能，可以实现异步RPC调用，避免阻塞主线程：

```cpp
void AsyncRpcCall(MprpcChannel *channel,
                 const google::protobuf::MethodDescriptor *method,
                 const google::protobuf::Message *request,
                 google::protobuf::Message *response,
                 std::function<void(const google::protobuf::Message*)> callback)
{
    // 创建RPC控制器
    MprpcController controller;
    
    // 创建回调对象
    class AsyncCallback : public google::protobuf::Closure
    {
    public:
        AsyncCallback(std::function<void(const google::protobuf::Message*)> callback,
                    google::protobuf::Message *response)
            : m_callback(callback), m_response(response)
        {
        }
        
        virtual void Run()
        {
            m_callback(m_response);
            delete this;
        }
        
    private:
        std::function<void(const google::protobuf::Message*)> m_callback;
        google::protobuf::Message *m_response;
    };
    
    // 创建异步回调
    AsyncCallback *done = new AsyncCallback(callback, response);
    
    // 调用RPC方法
    channel->CallMethod(method, &controller, request, response, done);
}
```

功能描述：
1. 创建RPC控制器
2. 创建异步回调对象
3. 调用RPC方法，传入异步回调
4. 当RPC调用完成时，执行回调函数

## 8. 总结

MPRPC服务调用方模块（caller）通过以下方式实现了高效的RPC调用：
1. 基于Muduo网络库实现高性能网络通信
2. 结合Zookeeper实现服务发现功能
3. 采用Protobuf进行数据序列化和反序列化
4. 使用连接池提高连接复用率
5. 支持同步和异步调用模式
6. 提供完善的错误处理机制

这些特性使得MPRPC服务调用方模块能够满足分布式系统中高效、可靠的RPC调用需求。
