#include <iostream>
#include "mprpc_app.h"
#include "friend.pb.h"

int main(int argc, char **argv)
{
    // 整个程序启动以后，想使用mprpc框架来享受rpc服务调用，一定需要先调用框架的初始化函数（只初始化一次）
    MprpcApplication::Init(argc, argv);

    /**************** 演示调用远程发布的 rpc 方法 GetFriendsList ****************/
    RPC::FriendServiceRpc_Stub stub(new MprpcChannel());
    
    // #1 rpc 方法的请求参数
    RPC::GetFriendsListRequest request;
    request.set_userid(1000);
    
    // #2 rpc方法的响应，并发起rpc方法的调用  同步的 rpc 调用过程  MprpcChannel::callmethod
    // RpcChannel->RpcChannel::callMethod 集中来做所有 rpc 方法调用的参数序列化和网络发送
    RPC::GetFriendsListResponse response;
    MprpcController controller;
    stub.GetFriendsList(&controller, &request, &response, nullptr);

    // #3 一次rpc调用完成，读调用的结果
    if (controller.Failed())
    {
        std::cout << controller.ErrorText() << std::endl;
    }
    else
    {
        if (0 == response.result().errcode())
        {
            std::cout << "rpc GetFriendsList response success!" << std::endl;
            int size = response.friends_size();
            for (int i=0; i < size; ++i)
            {
                std::cout << "index: " << i << ", name: " << response.friends(i) << std::endl;
            }
        }
        else
        {
            std::cout << "rpc GetFriendsList response error : " << response.result().errmsg() << std::endl;
        }
    }

    return 0;
}
