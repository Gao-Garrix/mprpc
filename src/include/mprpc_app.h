#pragma once

#include "mprpc_channel.h"
#include "mprpc_config.h"
#include "mprpc_controller.h"

/* 懒汉式单例: mprpc框架的基础类，负责框架的一些初始化操作 */
class MprpcApplication
{
public:
    static void Init(int argc, char **argv); // 初始化 MPRPC, 加载配置文件
    static MprpcApplication &GetInstance();  // 获取 app 单例
    static MprpcConfig &GetConfig();         // 返回 m_config

private:
    static MprpcConfig m_config;

    MprpcApplication() {}
    MprpcApplication(const MprpcApplication &) = delete;
    MprpcApplication(MprpcApplication &&) = delete;
};
