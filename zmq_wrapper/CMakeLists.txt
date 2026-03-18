cmake_minimum_required(VERSION 3.14)
project(zmq_wrapper VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 查找 libzmq
find_package(PkgConfig REQUIRED)
pkg_check_modules(ZMQ REQUIRED libzmq)

# cppzmq header-only (zmq.hpp)
set(CPPZMQ_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/../third_party/zmq.hpp)

# 源文件
set(ZMQ_WRAPPER_SRCS
    zmq_context.cpp
    zmq_publisher.cpp
    zmq_subscriber.cpp
    zmq_req_client.cpp
    zmq_rep_server.cpp
    zmq_router.cpp
    zmq_dealer.cpp
)

# 静态库
add_library(zmq_wrapper STATIC ${ZMQ_WRAPPER_SRCS})

target_include_directories(zmq_wrapper
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CPPZMQ_INCLUDE_DIR}
        ${ZMQ_INCLUDE_DIRS}
)

target_link_libraries(zmq_wrapper
    PUBLIC
        ${ZMQ_LIBRARIES}
        pthread
)

target_compile_options(zmq_wrapper PRIVATE ${ZMQ_CFLAGS_OTHER})

# 安装（可选）
install(TARGETS zmq_wrapper
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)

install(FILES
    zmq_wrapper.h
    zmq_options.h
    zmq_context.h
    zmq_publisher.h
    zmq_subscriber.h
    zmq_req_client.h
    zmq_rep_server.h
    zmq_router.h
    zmq_dealer.h
    logger.h
    DESTINATION include/zmq_wrapper
)

# 测试程序（可选）
option(BUILD_TESTS "Build test programs" ON)
if(BUILD_TESTS)
    add_subdirectory(tests)
endif()
