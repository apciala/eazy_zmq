cmake_minimum_required(VERSION 3.14)
project(zmq_gateway VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 查找 libzmq
find_package(PkgConfig REQUIRED)
pkg_check_modules(ZMQ REQUIRED libzmq)

# cppzmq header-only
set(CPPZMQ_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/../third_party/zmq.hpp)

# zmq_wrapper 目录
set(ZMQ_WRAPPER_DIR ${CMAKE_SOURCE_DIR}/../zmq_wrapper)

# 源文件
set(ZMQ_GATEWAY_SRCS
    main.cpp
    config.cpp
    gateway.cpp
)

# 可执行文件
add_executable(zmq_gateway ${ZMQ_GATEWAY_SRCS})

target_include_directories(zmq_gateway
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${ZMQ_WRAPPER_DIR}
        ${CPPZMQ_INCLUDE_DIR}
        ${ZMQ_INCLUDE_DIRS}
)

# 链接 zmq_wrapper 静态库
target_link_libraries(zmq_gateway
    PRIVATE
        zmq_wrapper
        ${ZMQ_LIBRARIES}
        pthread
)

# 添加 zmq_wrapper 子目录（如果独立编译）
if(NOT TARGET zmq_wrapper)
    add_subdirectory(${ZMQ_WRAPPER_DIR} ${CMAKE_BINARY_DIR}/zmq_wrapper)
endif()

# 确保 zmq_wrapper 的 include 目录传播
target_link_libraries(zmq_gateway PRIVATE zmq_wrapper)

target_compile_options(zmq_gateway PRIVATE ${ZMQ_CFLAGS_OTHER})

# 创建 IPC 目录（安装时）
install(CODE "
    execute_process(COMMAND mkdir -p /userdata/ipc/zmq_gateway)
")

# 安装
install(TARGETS zmq_gateway
    RUNTIME DESTINATION bin
)

# 测试程序（可选）
option(BUILD_TESTS "Build test programs" ON)
if(BUILD_TESTS)
    add_subdirectory(tests)
endif()