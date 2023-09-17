#include <iostream>
#include <string>
#include <vector>

#include "friend.pb.h"
#include "mprpc_app.h"
#include "mprpc_provider.h"
#include "logger.h"

class FriendService : public RPC::FriendServiceRpc
{
public:
    std::vector<std::string> GetFriendsList(uint32_t userid)
    {
        std::cout << "do GetFriendsList service! userid:" << userid << std::endl;
        std::vector<std::string> vec;
        vec.push_back("gao yang");
        vec.push_back("liu hong");
        vec.push_back("wang shuo");
        return vec;
    }

    // 重写基类方法
    void GetFriendsList(::google::protobuf::RpcController* controller,
                       const ::RPC::GetFriendsListRequest* request,
                       ::RPC::GetFriendsListResponse* response,
                       ::google::protobuf::Closure* done)
    {
        uint32_t userid = request->userid();
        
        std::vector<std::string> friendsList = GetFriendsList(userid);
        
        response->mutable_result()->set_errcode(0);
        response->mutable_result()->set_errmsg("");
        for (std::string &name : friendsList)
        {
            std::string *p = response->add_friends();
            *p = name;
        }
        
        done->Run();
    }
};

int main(int argc, char **argv)
{
    LOG_ERR("ddddd1");
    LOG_INFO("ddddd2");

    // #1 调用框架的初始化操作
    MprpcApplication::Init(argc, argv);

    // #2 provider 是一个rpc网络服务对象，把 UserService 对象发布到 rpc 节点上
    RpcProvider provider;
    provider.NotifyService(new FriendService());

    // #3 启动一个 rpc 服务发布节点，Run 以后，进程进入阻塞状态，等待远程的 rpc 调用请求
    provider.Run();

    return 0;
}
