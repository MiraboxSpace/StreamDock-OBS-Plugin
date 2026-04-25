/*
Plugin Name
Copyright (C) <2026> <MiraBox> <Email Address>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#pragma once

#include <memory>
#include <mutex>
#include <functional>
#include <obs.h>
#include "json-rpc.h"
#include "event-manager.h"

// 前向声明 WebSocket 服务器类
class WebSocketServer;

// 前向声明 websocketpp connection_hdl 类型
namespace websocketpp {
template<typename config> class server;
using connection_hdl = std::weak_ptr<void>;
} // namespace websocketpp

namespace streamdock {
namespace events {

// 连接ID类型（与EventManager中的定义一致）
using connection_id = uint64_t;
namespace handlers {

class events {
public:
	static std::shared_ptr<events> instance();

	events();
	~events() = default;

	// 注册 handlers 到 WebSocket 服务器
	void register_handlers(WebSocketServer *server);

	// 设置事件管理器
	void set_event_manager(std::shared_ptr<streamdock::events::EventManager> event_manager);

private:
	WebSocketServer *m_server;
	std::shared_ptr<streamdock::events::EventManager> m_event_manager;

private:
	// 带连接句柄的订阅方法
	void _subscribe_with_hdl(websocketpp::connection_hdl hdl, std::shared_ptr<streamdock::jsonrpc::request> req,
				 std::shared_ptr<streamdock::jsonrpc::response> res);

	// 带连接句柄的取消订阅方法
	void _unsubscribe_with_hdl(websocketpp::connection_hdl hdl, std::shared_ptr<streamdock::jsonrpc::request> req,
				   std::shared_ptr<streamdock::jsonrpc::response> res);

	// 带连接句柄的获取订阅方法
	void _get_subscriptions_with_hdl(websocketpp::connection_hdl hdl,
					 std::shared_ptr<streamdock::jsonrpc::request> req,
					 std::shared_ptr<streamdock::jsonrpc::response> res);

private:
	// 订阅事件
	void _subscribe(std::shared_ptr<streamdock::jsonrpc::request> req,
			std::shared_ptr<streamdock::jsonrpc::response> res);

	// 取消订阅事件
	void _unsubscribe(std::shared_ptr<streamdock::jsonrpc::request> req,
			  std::shared_ptr<streamdock::jsonrpc::response> res);

	// 获取当前订阅
	void _get_subscriptions(std::shared_ptr<streamdock::jsonrpc::request> req,
				std::shared_ptr<streamdock::jsonrpc::response> res);

	// 获取可用事件列表
	void _get_available_events(std::shared_ptr<streamdock::jsonrpc::request> req,
				   std::shared_ptr<streamdock::jsonrpc::response> res);
};

} // namespace handlers
} // namespace events
} // namespace streamdock
