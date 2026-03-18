cmake_minimum_required(VERSION 3.14)

# 测试程序
add_executable(test_gateway test_gateway.cpp)

target_include_directories(test_gateway
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${ZMQ_WRAPPER_DIR}
        ${CPPZMQ_INCLUDE_DIR}
        ${ZMQ_INCLUDE_DIRS}
)

target_link_libraries(test_gateway
    PRIVATE
        zmq_wrapper
        ${ZMQ_LIBRARIES}
        pthread
)

target_compile_options(test_gateway PRIVATE ${ZMQ_CFLAGS_OTHER})