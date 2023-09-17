aux_source_directory(. SRC_LIST)
# set(SRC_LIST 
#     mprpc_app.cc 
#     mprpc_config.cc 
#     mprpc_header.pb.cc 
#     mprpc_provider.cc 
#     mprpc_channel.cc
#     mprpc_controller.cc
#     logger.cc
#     zookeeperutil.cc
# )

# 由于 muduo 是静态库，所以这里没有编译成 SHARED
add_library(mprpc ${SRC_LIST})

# 注意 muduo 库链接顺序，zookeeper_mt 是多线程版本
target_link_libraries(mprpc muduo_net muduo_base pthread zookeeper_mt)