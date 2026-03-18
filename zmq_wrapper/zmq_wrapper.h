#pragma once

/// zmq_wrapper：ZMQ 通用封装库
///
/// 业务代码只需 include 这一个头文件。
///
/// 快速上手：
///
///   // PUB/SUB
///   zmq_wrap::PublisherSocket  pub("ipc:///userdata/ipc/events.sock");
///   zmq_wrap::SubscriberSocket sub("ipc:///userdata/ipc/events.sock");
///
///   // REQ/REP
///   zmq_wrap::RequesterSocket req("ipc:///userdata/ipc/cmd.sock");
///   zmq_wrap::ReplierSocket   rep("ipc:///userdata/ipc/cmd.sock");
///
///   // ROUTER/DEALER
///   zmq_wrap::RouterSocket router("tcp://*:5555");
///   zmq_wrap::DealerSocket dealer("tcp://localhost:5555");
///
/// 所有类型均在 zmq_wrap 命名空间下。
/// 配置项见 SocketConfig；统计项见 SocketStats；路由消息见 RoutedMessage。

#include "zmq_options.h"
#include "zmq_publisher.h"
#include "zmq_subscriber.h"
#include "zmq_req_client.h"
#include "zmq_rep_server.h"
#include "zmq_router.h"
#include "zmq_dealer.h"
