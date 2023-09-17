#include <iostream>
#include "mprpc_app.h"
#include "user.pb.h"
#include "mprpc_channel.h"

int main(int argc, char **argv)
{
    // 整个程序启动以后，想使用 mprpc 框架来享受 rpc 服务调用，一定需要先调用框架的初始化函数（只初始化一次）
    MprpcApplication::Init(argc, argv);

    /**************** 演示调用远程发布的 rpc 方法 Login ****************/
    RPC::UserServiceRpc_Stub stub(new MprpcChannel());

    // #1 构造 rpc request 方法的请求参数
    RPC::LoginRequest request;
    request.set_name("zhang san");
    request.set_pwd("123456");

    // #2 构造 rpc 方法的响应，并发起 rpc 方法的调用，同步的 rpc 调用过程，MprpcChannel::callmethod
    RPC::LoginResponse response;
    // RpcChannel->RpcChannel::callMethod 集中来做所有rpc方法调用的参数序列化和网络发送
    stub.Login(nullptr, &request, &response, nullptr);

    // #3 读取一次 rpc 调用完成，读调用的结果，当然最后使用 controller 提前判断一下
    if (0 == response.result().errcode())
    {
        std::cout << "rpc login response success:" << response.sucess() << std::endl;
    }
    else
    {
        std::cout << "rpc login response error : " << response.result().errmsg() << std::endl;
    }

    /**************** 演示调用远程发布的 rpc 方法 Register ****************/
    RPC::RegisterRequest req;
    req.set_id(2000);
    req.set_name("mprpc");
    req.set_pwd("666666");

    RPC::RegisterResponse rsp;
    // 以同步的方式发起 rpc 调用请求，等待返回结果
    stub.Register(nullptr, &req, &rsp, nullptr);

    // 一次rpc调用完成，读调用的结果
    if (0 == rsp.result().errcode())
    {
        std::cout << "rpc register response success:" << rsp.sucess() << std::endl;
    }
    else
    {
        std::cout << "rpc register response error : " << rsp.result().errmsg() << std::endl;
    }

    return 0;
}
