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

// 定义 ASIO_STANDALONE 以使用独立的 asio 而不是 boost.asio
#define ASIO_STANDALONE

#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

#include <thread>
#include <memory>
#include <mutex>
#include <string>
#include <set>
#include <functional>
#include <map>
#include <unordered_map>
#include <nlohmann/json.hpp>
#include "json-rpc.h"

typedef websocketpp::server<websocketpp::config::asio> websocket_server;

// 前向声明
namespace streamdock {
namespace jsonrpc {
class request;
class response;
class client;
} // namespace jsonrpc
namespace events {
class EventManager;
}
} // namespace streamdock

class WebSocketServer {
public:
	// Handler 回调类型定义
	typedef std::function<std::shared_ptr<streamdock::jsonrpc::response>(
		std::shared_ptr<streamdock::jsonrpc::request>)>
		handler_callback_t;

	typedef std::function<void(std::shared_ptr<streamdock::jsonrpc::request>,
				   std::shared_ptr<streamdock::jsonrpc::response>)>
		sync_handler_callback_t;

	// 带连接句柄的handler回调类型（用于需要访问hdl的功能，如事件订阅）
	typedef std::function<void(websocketpp::connection_hdl, std::shared_ptr<streamdock::jsonrpc::request>,
				   std::shared_ptr<streamdock::jsonrpc::response>)>
		sync_handler_with_hdl_callback_t;

public:
	WebSocketServer();
	~WebSocketServer();

	// 启动服务器
	bool start(uint16_t port = 8765);

	// 停止服务器
	void stop();

	// 发送消息到所有连接的客户端
	void broadcast(const std::string &message);

	// 发送消息到指定客户端
	void send(websocketpp::connection_hdl hdl, const std::string &message);

	// 获取服务器运行状态
	bool is_running() const;

	// 获取连接数
	size_t get_connection_count() const;

	// Handler 注册
	void handle(const std::string &method, handler_callback_t callback);
	void handle_sync(const std::string &method, sync_handler_callback_t callback);
	void handle_sync_with_hdl(const std::string &method, sync_handler_with_hdl_callback_t callback);

	// 事件通知
	void notify(const std::string &method, nlohmann::json params = nlohmann::json());

	// 设置事件管理器
	void set_event_manager(std::shared_ptr<streamdock::events::EventManager> event_manager);

	// 连接ID管理（用于事件订阅）
	using connection_id = uint64_t;
	connection_id get_connection_id(websocketpp::connection_hdl hdl);
	bool send_to_connection(connection_id id, const std::string &message);
	void cleanup_connection(websocketpp::connection_hdl hdl);

private:
	// 消息处理回调
	void on_message(websocketpp::connection_hdl hdl, websocket_server::message_ptr msg);

	// 连接建立回调
	void on_open(websocketpp::connection_hdl hdl);

	// 连接关闭回调
	void on_close(websocketpp::connection_hdl hdl);

	// 连接失败回调
	void on_fail(websocketpp::connection_hdl hdl);

	// 运行 io_service 线程函数
	void run_thread();

	// 关闭所有活跃连接
	void close_all_connections();

	// 等待线程结束（带超时）
	bool join_with_timeout(std::thread& th, unsigned int timeout_ms);

	// JSON-RPC 调用处理
	nlohmann::json handle_call(websocketpp::connection_hdl hdl, nlohmann::json &request_json);

	websocket_server m_server;
	std::thread m_server_thread;
	mutable std::mutex m_connection_mutex;
	std::map<websocketpp::connection_hdl, std::shared_ptr<streamdock::jsonrpc::client>,
		 std::owner_less<websocketpp::connection_hdl>>
		m_clients;
	std::set<websocketpp::connection_hdl, std::owner_less<websocketpp::connection_hdl>> m_connections;
	bool m_running;
	websocketpp::lib::shared_ptr<websocketpp::lib::asio::io_service::work> m_work;

	// 连接ID映射（用于事件订阅）
	std::unordered_map<connection_id, websocketpp::connection_hdl> m_id_to_connection;
	std::map<websocketpp::connection_hdl, connection_id, std::owner_less<websocketpp::connection_hdl>>
		m_connection_to_id;
	connection_id m_next_connection_id = 1;

	// Handler 存储和路由
	std::map<std::string, handler_callback_t> m_handler_default;
	std::map<std::string, sync_handler_callback_t> m_handler_sync;
	std::map<std::string, sync_handler_with_hdl_callback_t> m_handler_sync_with_hdl;

	// 事件管理器
	std::shared_ptr<streamdock::events::EventManager> m_event_manager;
};
