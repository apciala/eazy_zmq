cmake_minimum_required(VERSION 3.14)

# 测试程序
add_executable(zmq_wrapper_test test_zmq_wrapper.cpp)

target_link_libraries(zmq_wrapper_test PRIVATE zmq_wrapper)

# 确保测试可以找到头文件
target_include_directories(zmq_wrapper_test PRIVATE ${CMAKE_SOURCE_DIR})