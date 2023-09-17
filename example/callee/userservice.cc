#include <iostream>
#include <string>

#include "user.pb.h"
#include "mprpc_app.h"
#include "mprpc_provider.h"

/** UserService原来是一个本地服务，现在使用在 rpc 服务发布端（rpc服务提供者）
 * 提供了两个进程内的本地方法: Login 和 Register
 * */
class UserService : public RPC::UserServiceRpc
{
public:
    bool Login(std::string name, std::string pwd)
    {
        std::cout << "doing local servic::Login" << std::endl;
        std::cout << "name: " << name << ", pwd: " << pwd << std::endl;  
        return true;
    }

    bool Register(uint32_t id, std::string name, std::string pwd)
    {
        std::cout << "doing local service::Register" << std::endl;
        std::cout << "id: " << id << ", name:" << name << ", pwd:" << pwd << std::endl;
        return true;
    }

    /**
     * 重写基类 UserServiceRpc 的虚函数 下面这些方法都是框架直接调用的
     * 1. caller => Login(LoginRequest) => (muduo，其实是一个 client 没必要高并发，直接 connect 就行) => callee 
     * 2. callee => Login(LoginRequest) => 交到下面重写的这个Login方法上了
     * */
    void Login(::google::protobuf::RpcController* controller,
                       const ::RPC::LoginRequest* request,
                       ::RPC::LoginResponse* response,
                       ::google::protobuf::Closure* done)
    {
        // #1 框架给业务上报了请求参数 LoginRequest，应用获取相应数据做本地业务
        // 相当于将序列化的数据通过 protobuf 反序列化得到具体数据
        std::string name = request->name();
        std::string pwd = request->pwd();

        // #2 做本地业务
        bool login_result = Login(name, pwd); 

        // #3 把响应写入, 包括错误码、错误消息、返回值, 构造 response
        RPC::ResultCode *code = response->mutable_result();
        code->set_errcode(0);
        code->set_errmsg("");
        response->set_sucess(login_result);

        // #4 执行回调操作, 执行响应对象数据的序列化和网络发送（都是由框架来完成的）
        done->Run();
    }

    void Register(::google::protobuf::RpcController* controller,
                       const ::RPC::RegisterRequest* request,
                       ::RPC::RegisterResponse* response,
                       ::google::protobuf::Closure* done)
    {
        // #1
        uint32_t id = request->id();
        std::string name = request->name();
        std::string pwd = request->pwd();

        // #2 
        bool ret = Register(id, name, pwd);

        // #3
        response->mutable_result()->set_errcode(0);
        response->mutable_result()->set_errmsg("");
        response->set_sucess(ret);

        // #4
        done->Run();
    }
};

int main(int argc, char **argv)
{
    // 调用框架的初始化操作
    MprpcApplication::Init(argc, argv);

    // provider是一个rpc网络服务对象。把UserService对象发布到rpc节点上
    RpcProvider provider;
    provider.NotifyService(new UserService());

    // 启动一个rpc服务发布节点   Run以后，进程进入阻塞状态，等待远程的rpc调用请求
    provider.Run();

    return 0;
}
