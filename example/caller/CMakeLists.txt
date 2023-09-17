# set(SRC_LIST call_userservice.cc ../user.pb.cc)
set(SRC_LIST call_friendservice.cc ../friend.pb.cc)

add_executable(consumer ${SRC_LIST})

# 使用了 mprpc 框架以及 protobuf
target_link_libraries(consumer mprpc protobuf)
