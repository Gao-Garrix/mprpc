#pragma once

#include <semaphore.h>
#include <string>
#include <zookeeper/zookeeper.h>

// 封装的zk客户端类
class ZkClient
{
public:
    ZkClient();
    ~ZkClient();

    // zkclient 启动连接zkserver
    void Start();

    // 在 zkserver 上根据指定的path创建znode节点
    void Create(const char *path, const char *data, int datalen, int state = 0);

    // 根据参数指定的 znode 节点路径，或者 znode 节点的值
    std::string GetData(const char *path);

private:
    // zk的客户端句柄
    zhandle_t *m_zhandle;
};
