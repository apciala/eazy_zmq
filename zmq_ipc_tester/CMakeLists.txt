cmake_minimum_required(VERSION 3.16)
project(zmq_ipc_tester LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(ZMQ QUIET libzmq)
  pkg_check_modules(JSONCPP QUIET jsoncpp)
endif()

find_path(ZMQ_INCLUDE_DIR
  NAMES zmq.h
  HINTS ${ZMQ_INCLUDE_DIRS}
)
find_library(ZMQ_LIBRARY
  NAMES zmq
  HINTS ${ZMQ_LIBRARY_DIRS}
)

find_path(JSONCPP_INCLUDE_DIR
  NAMES json/json.h
  HINTS ${JSONCPP_INCLUDE_DIRS}
  PATH_SUFFIXES jsoncpp
)
find_library(JSONCPP_LIBRARY
  NAMES jsoncpp
  HINTS ${JSONCPP_LIBRARY_DIRS}
)

if(NOT ZMQ_INCLUDE_DIR OR NOT ZMQ_LIBRARY)
  message(FATAL_ERROR "libzmq not found. Please install libzmq development package.")
endif()

if(NOT JSONCPP_INCLUDE_DIR OR NOT JSONCPP_LIBRARY)
  message(FATAL_ERROR "jsoncpp not found. Please install jsoncpp development package.")
endif()

add_executable(zmq_ipc_tester
  src/main.cpp
  src/reqrep_client.cpp
  src/sub_client.cpp
)

set(CPPZMQ_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/zmq.hpp)

target_include_directories(zmq_ipc_tester PRIVATE
  ${ZMQ_INCLUDE_DIR}
  ${JSONCPP_INCLUDE_DIR}
  ${CPPZMQ_INCLUDE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(zmq_ipc_tester PRIVATE
  ${ZMQ_LIBRARY}
  ${JSONCPP_LIBRARY}
  pthread
)

set_target_properties(zmq_ipc_tester PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# ── zmq_wrapper unit test ──────────────────────────────────
# 使用独立的 zmq_wrapper 目录
set(ZMQ_WRAPPER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../zmq_wrapper)
set(CPPZMQ_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/zmq.hpp)

add_executable(test_zmq_wrapper
  test_zmq_wrapper.cpp
  ${ZMQ_WRAPPER_DIR}/zmq_context.cpp
  ${ZMQ_WRAPPER_DIR}/zmq_publisher.cpp
  ${ZMQ_WRAPPER_DIR}/zmq_subscriber.cpp
  ${ZMQ_WRAPPER_DIR}/zmq_rep_server.cpp
  ${ZMQ_WRAPPER_DIR}/zmq_req_client.cpp
  ${ZMQ_WRAPPER_DIR}/zmq_router.cpp
  ${ZMQ_WRAPPER_DIR}/zmq_dealer.cpp
)

target_include_directories(test_zmq_wrapper PRIVATE
  ${ZMQ_WRAPPER_DIR}
  ${CPPZMQ_INCLUDE_DIR}
  ${ZMQ_INCLUDE_DIR}
)

target_link_libraries(test_zmq_wrapper PRIVATE
  ${ZMQ_LIBRARY}
  pthread
)

target_compile_options(test_zmq_wrapper PRIVATE -Wall -Wextra)

set_target_properties(test_zmq_wrapper PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
