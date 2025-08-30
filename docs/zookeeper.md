# MPRPC项目中Zookeeper实现详解

## 1. 总体功能描述

在MPRPC框架中，Zookeeper作为服务注册与发现的核心组件，负责管理RPC服务的注册信息和服务发现功能。通过Zookeeper的树形数据结构，实现了服务的动态注册、发现和负载均衡。主要功能包括：

1. **服务注册**：服务提供方启动时，将自己的服务信息注册到Zookeeper的指定节点下
2. **服务发现**：服务调用方从Zookeeper获取可用的服务提供方信息
3. **节点管理**：创建持久节点和临时节点，确保服务信息与进程生命周期关联
4. **心跳检测**：通过Zookeeper的会话机制实现自动心跳检测，确保服务状态实时更新

## 2. 模块架构

Zookeeper模块主要由以下几个核心组件构成：
- **ZkClient类**：封装Zookeeper C API，提供简化的接口
- **节点操作**：实现节点的创建、删除、获取数据等功能
- **事件监听**：实现Watcher机制，监听节点变化

## 3. 核心组件实现

### 3.1 ZkClient类

#### 3.1.1 类定义
```cpp
class ZkClient
{
public:
    ZkClient();
    ~ZkClient();
    
    // 启动Zookeeper连接
    bool Start();
    
    // 创建ZooKeeper节点
    bool Create(const char *path, const char *data, int state = 0);
    
    // 获取节点数据
    std::string GetNodeData(const char *path, bool watch = false);
    
    // 设置节点数据
    bool SetNodeData(const char *path, const char *data, int version = -1);
    
    // 删除节点
    bool DeleteNode(const char *path, int version = -1);
    
    // 判断节点是否存在
    bool NodeExists(const char *path, bool watch = false);
    
private:
    // ZooKeeper句柄
    zhandle_t *m_zhandle;
    
    // 全局Watcher回调函数
    static void GlobalWatcher(zhandle_t *zh, int type, int state, const char *path, void *watcherCtx);
    
    // 维护心跳
    void MaintainHeartbeat();
    
    // 连接状态标志
    bool m_connected;
    
    // 心跳线程
    std::thread m_heartbeatThread;
};
```

#### 3.1.2 构造函数与析构函数
```cpp
ZkClient::ZkClient() : m_zhandle(nullptr), m_connected(false)
{
    // 初始化ZooKeeper连接
    m_zhandle = nullptr;
    m_connected = false;
}

ZkClient::~ZkClient()
{
    if (m_zhandle)
    {
        zookeeper_close(m_zhandle);
        m_zhandle = nullptr;
    }
    m_connected = false;
    
    // 确保心跳线程结束
    if (m_heartbeatThread.joinable())
    {
        m_heartbeatThread.join();
    }
}
```

功能描述：
构造函数初始化ZooKeeper句柄和连接状态标志。析构函数确保ZooKeeper连接被正确关闭，并等待心跳线程结束。

#### 3.1.3 Start方法 - 启动ZooKeeper连接
```cpp
bool ZkClient::Start()
{
    // ZooKeeper的地址，格式为"host:port"
    std::string host = "127.0.0.1:2181"; // 默认地址，可以从配置文件中读取
    
    // 创建ZooKeeper连接
    // 参数：服务器地址，会话超时时间(ms)，全局Watcher，上下文，观察模式
    m_zhandle = zookeeper_init(host.c_str(), GlobalWatcher, 30000, nullptr, this, 0);
    
    if (m_zhandle == nullptr)
    {
        std::cout << "zookeeper_init error!" << std::endl;
        return false;
    }
    
    m_connected = true;
    
    // 启动心跳线程
    m_heartbeatThread = std::thread(&ZkClient::MaintainHeartbeat, this);
    
    // 等待连接建立
    std::unique_lock<std::mutex> lock(m_mutex);
    m_cond.wait(lock, [this](){ return m_connected; });
    
    return true;
}
```

功能描述：
1. 初始化ZooKeeper连接，设置会话超时时间为30秒
2. 启动心跳线程，定期发送心跳包
3. 等待连接建立完成

#### 3.1.4 GlobalWatcher方法 - 全局Watcher回调
```cpp
void ZkClient::GlobalWatcher(zhandle_t *zh, int type, int state, const char *path, void *watcherCtx)
{
    ZkClient *client = static_cast<ZkClient*>(watcherCtx);
    
    if (type == ZOO_SESSION_EVENT)
    {
        if (state == ZOO_CONNECTED_STATE)
        {
            std::cout << "Connected to ZooKeeper!" << std::endl;
            client->m_connected = true;
            client->m_cond.notify_one();
        }
        else if (state == ZOO_EXPIRED_SESSION_STATE)
        {
            std::cout << "ZooKeeper session expired!" << std::endl;
            client->m_connected = false;
            
            // 重新连接
            client->Reconnect();
        }
        else if (state == ZOO_CONNECTING_STATE)
        {
            std::cout << "Connecting to ZooKeeper..." << std::endl;
        }
    }
    else if (type == ZOO_DELETED_EVENT)
    {
        std::cout << "Node deleted: " << path << std::endl;
    }
    else if (type == ZOO_CREATED_EVENT)
    {
        std::cout << "Node created: " << path << std::endl;
    }
    else if (type == ZOO_CHANGED_EVENT)
    {
        std::cout << "Node changed: " << path << std::endl;
    }
}
```

功能描述：
1. 处理会话事件（连接、连接中、会话超时）
2. 处理节点事件（创建、删除、变更）
3. 在连接成功时通知等待的线程
4. 在会话超时时触发重新连接

#### 3.1.5 MaintainHeartbeat方法 - 维护心跳
```cpp
void ZkClient::MaintainHeartbeat()
{
    while (m_connected)
    {
        // 处理网络事件，维持心跳
        zookeeper_process(m_zhandle, 1000); // 超时1000ms
        
        // 休眠1秒
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
}
```

功能描述：
1. 定期调用zookeeper_process()处理网络事件，维持心跳
2. 每次处理完网络事件后休眠1秒
3. 当连接断开时退出循环

#### 3.1.6 Create方法 - 创建节点
```cpp
bool ZkClient::Create(const char *path, const char *data, int state)
{
    char buffer[128] = {0};
    int buffer_len = sizeof(buffer);
    
    // 创建节点
    int ret = zoo_create(m_zhandle, path, data, strlen(data), 
                       &ZOO_OPEN_ACL_UNSAFE, state, buffer, buffer_len);
    
    if (ret == ZOK)
    {
        std::cout << "Create node success, path: " << path << ", data: " << buffer << std::endl;
        return true;
    }
    
    std::cout << "Create node error, path: " << path << ", error: " << zerror(ret) << std::endl;
    return false;
}
```

功能描述：
1. 在指定路径创建ZooKeeper节点
2. 参数包括节点路径、节点数据、节点状态（持久节点或临时节点）
3. 成功时返回true，失败时返回false并打印错误信息

#### 3.1.7 GetNodeData方法 - 获取节点数据
```cpp
std::string ZkClient::GetNodeData(const char *path, bool watch)
{
    char buffer[128] = {0};
    int buffer_len = sizeof(buffer);
    Stat stat;
    
    // 获取节点数据
    int ret = zoo_get(m_zhandle, path, watch, buffer, &buffer_len, &stat);
    
    if (ret == ZOK)
    {
        return std::string(buffer, buffer_len);
    }
    
    std::cout << "Get node data error, path: " << path << ", error: " << zerror(ret) << std::endl;
    return "";
}
```

功能描述：
1. 获取指定路径节点的数据
2. 参数包括节点路径和是否设置Watch标志
3. 成功时返回节点数据，失败时返回空字符串并打印错误信息

#### 3.1.8 SetNodeData方法 - 设置节点数据
```cpp
bool ZkClient::SetNodeData(const char *path, const char *data, int version)
{
    // 设置节点数据
    int ret = zoo_set(m_zhandle, path, data, strlen(data), version);
    
    if (ret == ZOK)
    {
        std::cout << "Set node data success, path: " << path << std::endl;
        return true;
    }
    
    std::cout << "Set node data error, path: " << path << ", error: " << zerror(ret) << std::endl;
    return false;
}
```

功能描述：
1. 设置指定路径节点的数据
2. 参数包括节点路径、新数据和版本号（-1表示忽略版本检查）
3. 成功时返回true，失败时返回false并打印错误信息

#### 3.1.9 DeleteNode方法 - 删除节点
```cpp
bool ZkClient::DeleteNode(const char *path, int version)
{
    // 删除节点
    int ret = zoo_delete(m_zhandle, path, version);
    
    if (ret == ZOK)
    {
        std::cout << "Delete node success, path: " << path << std::endl;
        return true;
    }
    
    std::cout << "Delete node error, path: " << path << ", error: " << zerror(ret) << std::endl;
    return false;
}
```

功能描述：
1. 删除指定路径的节点
2. 参数包括节点路径和版本号（-1表示忽略版本检查）
3. 成功时返回true，失败时返回false并打印错误信息

#### 3.1.10 NodeExists方法 - 检查节点是否存在
```cpp
bool ZkClient::NodeExists(const char *path, bool watch)
{
    Stat stat;
    
    // 检查节点是否存在
    int ret = zoo_exists(m_zhandle, path, watch, &stat);
    
    if (ret == ZOK)
    {
        return true;
    }
    
    return false;
}
```

功能描述：
1. 检查指定路径的节点是否存在
2. 参数包括节点路径和是否设置Watch标志
3. 存在时返回true，不存在时返回false

## 4. 服务注册与发现实现

### 4.1 服务注册实现
```cpp
// 在RpcProvider类中实现服务注册
void RpcProvider::Run(char *ip, uint16_t port)
{
    // ... 其他代码 ...
    
    // 将之前维护的 m_serviceMap 以及 m_methodMap 注册到 zookeeper 上
    // 让 rpc client(caller) 可以从 zk 上发现服务
    for (auto &sp : m_serviceMap)
    {
        std::string service_name = sp.first;
        std::string service_path = "/" + service_name;
        
        // 创建服务节点（持久节点）
        m_zkClient.Create(service_path, "", 0);
        
        for (auto &mp : sp.second.m_methodMap)
        {
            std::string method_name = mp.first;
            std::string method_path = service_path + "/" + method_name;
            
            // 创建方法节点（临时节点，序列化）
            char node_path[128] = {0};
            sprintf(node_path, "%s/%s:%d", method_path.c_str(), ip_str.c_str(), port_int);
            
            // 创建临时节点，节点中包含服务提供者的IP和端口
            m_zkClient.Create(method_path, node_path, ZOO_EPHEMERAL | ZOO_SEQUENCE);
        }
    }
    
    // ... 其他代码 ...
}
```

功能描述：
1. 为每个服务创建持久节点
2. 为每个服务方法创建临时节点（带序列号）
3. 临时节点中包含服务提供者的IP和端口信息
4. 使用ZOO_EPHEMERAL标志确保节点与服务进程生命周期关联
5. 使用ZOO_SEQUENCE标志为节点添加序列号，确保节点名唯一

### 4.2 服务发现实现
```cpp
// 在MprpcChannel类中实现服务发现
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
    
    // 3. 解析node_path，获取host和port
    size_t pos = node_path.find(":");
    if (pos == std::string::npos)
    {
        controller->SetFailed("rpc node path format error!");
        return;
    }
    
    std::string host = node_path.substr(0, pos);
    uint16_t port = atoi(node_path.substr(pos + 1).c_str());
    
    // ... 其他代码 ...
}
```

功能描述：
1. 根据服务名和方法名构建Zookeeper节点路径
2. 从Zookeeper获取节点数据（包含服务提供方的IP和端口）
3. 解析节点数据，提取服务提供方的IP和端口
4. 如果服务不存在或节点格式错误，设置错误状态并返回

## 5. 心跳检测机制实现

### 5.1 会话超时处理
```cpp
void ZkClient::GlobalWatcher(zhandle_t *zh, int type, int state, const char *path, void *watcherCtx)
{
    ZkClient *client = static_cast<ZkClient*>(watcherCtx);
    
    if (type == ZOO_SESSION_EVENT)
    {
        if (state == ZOO_CONNECTED_STATE)
        {
            std::cout << "Connected to ZooKeeper!" << std::endl;
            client->m_connected = true;
            client->m_cond.notify_one();
        }
        else if (state == ZOO_EXPIRED_SESSION_STATE)
        {
            std::cout << "ZooKeeper session expired!" << std::endl;
            client->m_connected = false;
            
            // 重新连接
            client->Reconnect();
        }
        // ... 其他状态处理 ...
    }
    // ... 其他事件处理 ...
}
```

功能描述：
1. 监听会话事件，处理连接成功状态
2. 处理会话超时状态（ZOO_EXPIRED_SESSION_STATE）
3. 在会话超时时触发重新连接机制

### 5.2 重新连接机制
```cpp
void ZkClient::Reconnect()
{
    // 关闭旧连接
    if (m_zhandle)
    {
        zookeeper_close(m_zhandle);
        m_zhandle = nullptr;
    }
    
    m_connected = false;
    
    // 重新建立连接
    std::string host = "127.0.0.1:2181"; // 默认地址，可以从配置文件中读取
    m_zhandle = zookeeper_init(host.c_str(), GlobalWatcher, 30000, nullptr, this, 0);
    
    if (m_zhandle == nullptr)
    {
        std::cout << "Reconnect to ZooKeeper failed!" << std::endl;
        return;
    }
    
    // 等待连接建立
    std::unique_lock<std::mutex> lock(m_mutex);
    m_cond.wait(lock, [this](){ return m_connected; });
    
    // 重新注册服务
    RegisterServices();
}
```

功能描述：
1. 关闭旧的ZooKeeper连接
2. 重新建立ZooKeeper连接
3. 等待连接建立完成
4. 重新注册服务

### 5.3 心跳维护
```cpp
void ZkClient::MaintainHeartbeat()
{
    while (m_connected)
    {
        // 处理网络事件，维持心跳
        zookeeper_process(m_zhandle, 1000); // 超时1000ms
        
        // 休眠1秒
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
}
```

功能描述：
1. 定期调用zookeeper_process()处理网络事件，维持心跳
2. 每次处理完网络事件后休眠1秒
3. 当连接断开时退出循环

## 6. 错误处理机制

### 6.1 连接错误处理
```cpp
bool ZkClient::Start()
{
    // 创建ZooKeeper连接
    m_zhandle = zookeeper_init(host.c_str(), GlobalWatcher, 30000, nullptr, this, 0);
    
    if (m_zhandle == nullptr)
    {
        std::cout << "zookeeper_init error!" << std::endl;
        return false;
    }
    
    // 等待连接建立
    std::unique_lock<std::mutex> lock(m_mutex);
    m_cond.wait(lock, [this](){ return m_connected; });
    
    if (!m_connected)
    {
        std::cout << "Connect to ZooKeeper timeout!" << std::endl;
        return false;
    }
    
    return true;
}
```

功能描述：
1. 检查ZooKeeper连接是否成功建立
2. 设置超时等待，避免无限等待
3. 连接失败时返回错误信息

### 6.2 节点操作错误处理
```cpp
bool ZkClient::Create(const char *path, const char *data, int state)
{
    // 创建节点
    int ret = zoo_create(m_zhandle, path, data, strlen(data), 
                       &ZOO_OPEN_ACL_UNSAFE, state, buffer, buffer_len);
    
    if (ret == ZOK)
    {
        return true;
    }
    
    std::cout << "Create node error, path: " << path << ", error: " << zerror(ret) << std::endl;
    return false;
}
```

功能描述：
1. 检查节点操作是否成功
2. 失败时打印详细的错误信息
3. 返回操作结果

## 7. 使用示例

### 7.1 服务提供方注册服务
```cpp
// 创建ZooKeeper客户端
ZkClient zk;
if (!zk.Start())
{
    std::cout << "ZooKeeper start error!" << std::endl;
    return;
}

// 创建根节点
std::string root_path = "/";
zk.Create(root_path, "", 0);

// 注册服务
std::string service_name = "UserServiceRpc";
std::string service_path = "/" + service_name;
zk.Create(service_path, "", 0);

// 注册方法
std::string method_name = "Login";
std::string method_path = service_path + "/" + method_name;
char node_path[128] = {0};
sprintf(node_path, "%s/127.0.0.1:8080", method_path.c_str());
zk.Create(method_path, node_path, ZOO_EPHEMERAL | ZOO_SEQUENCE);
```

功能描述：
1. 初始化ZooKeeper客户端
2. 创建根节点
3. 注册服务节点（持久节点）
4. 注册方法节点（临时节点，带序列号）

### 7.2 服务调用方发现服务
```cpp
// 创建ZooKeeper客户端
ZkClient zk;
if (!zk.Start())
{
    std::cout << "ZooKeeper start error!" << std::endl;
    return;
}

// 发现服务
std::string service_name = "UserServiceRpc";
std::string method_name = "Login";
std::string path = "/" + service_name + "/" + method_name;

// 获取服务提供方信息
std::string node_path = zk.GetNodeData(path);
if (node_path.empty())
{
    std::cout << "Service not found!" << std::endl;
    return;
}

// 解析IP和端口
size_t pos = node_path.find(":");
if (pos == std::string::npos)
{
    std::cout << "Invalid node path format!" << std::endl;
    return;
}

std::string host = node_path.substr(0, pos);
uint16_t port = atoi(node_path.substr(pos + 1).c_str());

std::cout << "Found service at " << host << ":" << port << std::endl;
```

功能描述：
1. 初始化ZooKeeper客户端
2. 构建服务路径
3. 获取服务提供方信息
4. 解析IP和端口

## 8. 设计特点与优势

1. **会话超时机制**：通过ZooKeeper的会话机制实现自动心跳检测，确保服务状态实时更新
2. **节点生命周期管理**：使用临时节点确保服务信息与服务进程生命周期关联，进程退出时自动清理
3. **自动重连机制**：在会话超时后自动重新连接，提高系统可靠性
4. **线程安全设计**：ZooKeeper操作封装在类中，保证多线程环境下的安全性
5. **简化接口**：提供简化的API，降低使用复杂度
6. **错误处理完善**：对各种错误情况进行处理，提供详细的错误信息

## 9. 应用场景

在MPRPC框架中，Zookeeper主要用于：
1. **服务注册**：服务提供方启动时，将自己的服务信息注册到Zookeeper
2. **服务发现**：服务调用方从Zookeeper获取可用的服务提供方信息
3. **负载均衡**：服务调用方可以从多个服务提供方中选择一个进行调用
4. **服务监控**：通过监听节点变化，实时监控服务状态
5. **配置管理**：存储和管理系统配置信息

这种设计使得MPRPC框架能够实现服务的动态注册和发现，提高系统的可扩展性和可维护性。
