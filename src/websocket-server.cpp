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

#include "websocket-server.h"
#include "event-manager.h"
#include <obs-module.h>
#include <plugin-support.h>
#include <atomic>
#include <chrono>
#include <thread>

using namespace streamdock::jsonrpc;
WebSocketServer::WebSocketServer()
    : m_running(false)
{
}

WebSocketServer::~WebSocketServer()
{
    stop();
}

bool WebSocketServer::start(uint16_t port)
{
    if (m_running) {
        // obs_log(LOG_WARNING, "WebSocket server is already running");
        return false;
    }

    try {
        // 设置日志级别
        m_server.set_access_channels(websocketpp::log::alevel::all);
        m_server.clear_access_channels(websocketpp::log::alevel::frame_payload);

        // 初始化 Asio
        m_server.init_asio();

        // 设置消息处理器
        m_server.set_message_handler(
            websocketpp::lib::bind(&WebSocketServer::on_message, this,
                                  websocketpp::lib::placeholders::_1,
                                  websocketpp::lib::placeholders::_2)
        );

        // 设置连接打开处理器
        m_server.set_open_handler(
            websocketpp::lib::bind(&WebSocketServer::on_open, this,
                                  websocketpp::lib::placeholders::_1)
        );

        // 设置连接关闭处理器
        m_server.set_close_handler(
            websocketpp::lib::bind(&WebSocketServer::on_close, this,
                                  websocketpp::lib::placeholders::_1)
        );

        // 设置连接失败处理器
        m_server.set_fail_handler(
            websocketpp::lib::bind(&WebSocketServer::on_fail, this,
                                  websocketpp::lib::placeholders::_1)
        );

        // 监听指定端口
        m_server.listen(port);

        // 开始接受连接
        m_server.start_accept();

        // 创建 work 对象以防止 io_service 在没有任务时退出
        m_work = websocketpp::lib::make_shared<websocketpp::lib::asio::io_service::work>(
            m_server.get_io_service()
        );

        // 启动服务器线程
        m_running = true;
        m_server_thread = std::thread(&WebSocketServer::run_thread, this);

        // obs_log(LOG_INFO, "WebSocket server started on port %d", port);
        return true;

    } catch (websocketpp::exception const& e) {
        obs_log(LOG_ERROR, "WebSocket server exception: %s", e.what());
        m_running = false;
        return false;
    } catch (std::exception const& e) {
        obs_log(LOG_ERROR, "Standard exception: %s", e.what());
        m_running = false;
        return false;
    } catch (...) {
        obs_log(LOG_ERROR, "Unknown exception while starting WebSocket server");
        m_running = false;
        return false;
    }
}

void WebSocketServer::stop()
{
    if (!m_running) {
        return;
    }

    m_running = false;

    try {
        // 步骤 1: 关闭所有活跃的 WebSocket 连接
        // obs_log(LOG_INFO, "Closing all WebSocket connections...");
        close_all_connections();

        // 步骤 2: 给连接一些时间优雅关闭
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // 步骤 3: 停止监听新连接
        m_server.stop_listening();

        // 步骤 4: 停止 work 对象，允许 io_service 退出
        if (m_work) {
            m_work.reset();
        }

        // 步骤 5: 停止服务器
        m_server.stop();

        // 步骤 6: 等待线程结束，带 100 毫秒超时
        if (m_server_thread.joinable()) {
            // obs_log(LOG_INFO, "Waiting for WebSocket server thread to stop...");
            if (!join_with_timeout(m_server_thread, 100)) {
                obs_log(LOG_WARNING, "WebSocket server thread did not stop in time, continuing anyway");
            } else {
                // obs_log(LOG_INFO, "WebSocket server thread stopped successfully");
            }
        }

        // 步骤 7: 清空连接和客户端映射
        {
            std::lock_guard<std::mutex> lock(m_connection_mutex);
            m_connections.clear();
            m_clients.clear();
            m_id_to_connection.clear();
            m_connection_to_id.clear();
        }

        // obs_log(LOG_INFO, "WebSocket server stopped");

    } catch (websocketpp::exception const& e) {
        obs_log(LOG_ERROR, "WebSocket exception during stop: %s", e.what());
    } catch (std::exception const& e) {
        obs_log(LOG_ERROR, "Standard exception during stop: %s", e.what());
    } catch (...) {
        obs_log(LOG_ERROR, "Unknown exception during stop");
    }
}

void WebSocketServer::broadcast(const std::string& message)
{
    if (!m_running) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_connection_mutex);

    for (auto it = m_connections.begin(); it != m_connections.end(); ) {
        try {
            m_server.send(*it, message, websocketpp::frame::opcode::text);
            ++it;
        } catch (const websocketpp::exception&) {
            // obs_log(LOG_WARNING, "Failed to send message to client: %s", e.what());
            // 移除无效的连接
            it = m_connections.erase(it);
        }
    }
}

void WebSocketServer::send(websocketpp::connection_hdl hdl, const std::string& message)
{
    if (!m_running) {
        return;
    }

    try {
        m_server.send(hdl, message, websocketpp::frame::opcode::text);
    } catch (const websocketpp::exception&) {
        // obs_log(LOG_WARNING, "Failed to send message: %s", e.what());
    }
}

bool WebSocketServer::is_running() const
{
    return m_running;
}

size_t WebSocketServer::get_connection_count() const
{
    std::lock_guard<std::mutex> lock(m_connection_mutex);
    return m_connections.size();
}

void WebSocketServer::on_message(websocketpp::connection_hdl hdl, websocket_server::message_ptr msg)
{
    std::string payload = msg->get_payload();
    // obs_log(LOG_INFO, "Received WebSocket message: %s", payload.c_str());

    try {
        // 解析 JSON
        nlohmann::json request_json;
        try {
            request_json = nlohmann::json::parse(payload);
        } catch (const nlohmann::json::parse_error&) {
            // JSON 解析错误
            // obs_log(LOG_WARNING, "JSON parse error: %s", e.what());
            nlohmann::json error_response;
            error_response["jsonrpc"] = "2.0";
            error_response["id"] = nullptr;
            error_response["error"]["code"] = static_cast<int64_t>(error_codes::PARSE_ERROR);
            error_response["error"]["message"] = "Parse error";

            std::string response_str = error_response.dump();
            m_server.send(hdl, response_str, websocketpp::frame::opcode::text);
            return;
        }

        // 处理 JSON-RPC 调用
        nlohmann::json response_json = handle_call(hdl, request_json);

        // 发送响应
        if (!response_json.is_null()) {
            std::string response_str = response_json.dump();
            m_server.send(hdl, response_str, websocketpp::frame::opcode::text);
        }

    } catch (const websocketpp::exception& e) {
        obs_log(LOG_ERROR, "Failed to send response: %s", e.what());
    } catch (const std::exception& e) {
        obs_log(LOG_ERROR, "Error processing message: %s", e.what());
    }
}

void WebSocketServer::on_open(websocketpp::connection_hdl hdl)
{
    std::lock_guard<std::mutex> lock(m_connection_mutex);
    m_connections.insert(hdl);

    // 分配连接ID
    connection_id conn_id = m_next_connection_id++;
    m_id_to_connection[conn_id] = hdl;
    m_connection_to_id[hdl] = conn_id;

    // 创建新的客户端实例
    auto client = std::make_shared<streamdock::jsonrpc::client>();
    m_clients[hdl] = client;

    // obs_log(LOG_INFO, "WebSocket connection opened (ID: %llu), total connections: %zu", conn_id, m_connections.size());
}

void WebSocketServer::on_close(websocketpp::connection_hdl hdl)
{
    std::lock_guard<std::mutex> lock(m_connection_mutex);

    // 清理事件订阅
    if (m_event_manager) {
        auto it = m_connection_to_id.find(hdl);
        if (it != m_connection_to_id.end()) {
            connection_id conn_id = it->second;
            m_event_manager->unsubscribe_all(conn_id);
            m_id_to_connection.erase(conn_id);
            m_connection_to_id.erase(it);
        }
    }

    m_connections.erase(hdl);
    m_clients.erase(hdl);
    // obs_log(LOG_INFO, "WebSocket connection closed, total connections: %zu", m_connections.size());
}

void WebSocketServer::on_fail(websocketpp::connection_hdl hdl)
{
    std::lock_guard<std::mutex> lock(m_connection_mutex);

    // 清理事件订阅
    if (m_event_manager) {
        auto it = m_connection_to_id.find(hdl);
        if (it != m_connection_to_id.end()) {
            connection_id conn_id = it->second;
            m_event_manager->unsubscribe_all(conn_id);
            m_id_to_connection.erase(conn_id);
            m_connection_to_id.erase(it);
        }
    }

    m_connections.erase(hdl);
    m_clients.erase(hdl);
    // obs_log(LOG_WARNING, "WebSocket connection failed");
}

void WebSocketServer::run_thread()
{
    // obs_log(LOG_INFO, "WebSocket server thread started");
    try {
        m_server.run();
    } catch (const std::exception& e) {
        obs_log(LOG_ERROR, "WebSocket server thread exception: %s", e.what());
    }
    // obs_log(LOG_INFO, "WebSocket server thread ended");
}

void WebSocketServer::close_all_connections()
{
    std::lock_guard<std::mutex> lock(m_connection_mutex);

    // 为每个连接发送关闭帧
    for (auto it = m_connections.begin(); it != m_connections.end(); ++it) {
        try {
            // 发送正常关闭的状态码
            m_server.close(*it, websocketpp::close::status::going_away,
                          "OBS is shutting down");
        } catch (const websocketpp::exception&) {
            // obs_log(LOG_WARNING, "Failed to close connection: %s", e.what());
        }
    }

    // obs_log(LOG_INFO, "Initiated close for %zu connections", m_connections.size());
}

bool WebSocketServer::join_with_timeout(std::thread& th, unsigned int timeout_ms)
{
    if (!th.joinable()) {
        return true;
    }

    // 使用 std::timed_join 或条件变量实现超时
    // C++ 标准库的 std::thread 没有 join_with_timeout，需要使用其他方法
    // 这里使用 detached + 等待的变通方法

    std::atomic<bool> done{false};
    std::thread waiter([&]() {
        th.join();
        done = true;
    });

    waiter.detach(); // 分离等待线程

    // 等待直到线程结束或超时
    auto start = std::chrono::steady_clock::now();
    while (!done) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();

        if (elapsed >= timeout_ms) {
            // obs_log(LOG_WARNING, "Thread join timeout after %u ms", timeout_ms);
            return false;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    return true;
}

// Handler 注册
void WebSocketServer::handle(const std::string& method, handler_callback_t callback)
{
    m_handler_default[method] = callback;
}

void WebSocketServer::handle_sync(const std::string& method, sync_handler_callback_t callback)
{
    m_handler_sync[method] = callback;
}

void WebSocketServer::handle_sync_with_hdl(const std::string& method, sync_handler_with_hdl_callback_t callback)
{
    m_handler_sync_with_hdl[method] = callback;
}

// 事件通知
void WebSocketServer::notify(const std::string& method, nlohmann::json params)
{
    nlohmann::json notification;
    notification["jsonrpc"] = "2.0";
    notification["method"] = method;
    if (!params.is_null()) {
        notification["params"] = params;
    }

    std::string message = notification.dump();
    broadcast(message);
}

// JSON-RPC 调用处理
nlohmann::json WebSocketServer::handle_call(websocketpp::connection_hdl hdl, nlohmann::json& request_json)
{
    // 获取客户端
    std::shared_ptr<client> client_ptr;
    {
        std::lock_guard<std::mutex> lock(m_connection_mutex);
        auto it = m_clients.find(hdl);
        if (it != m_clients.end()) {
            client_ptr = it->second;
        } else {
            client_ptr = std::make_shared<client>();
            m_clients[hdl] = client_ptr;
        }
    }

    // 创建请求对象
    auto req = std::make_shared<request>(request_json, client_ptr.get());

    // 创建响应对象
    nlohmann::json response_json;
    response_json["jsonrpc"] = "2.0";

    // 复制 id
    if (request_json.contains("id")) {
        response_json["id"] = request_json["id"];
    }

    auto res = std::make_shared<response>(response_json, client_ptr.get());

    try {
        // 验证请求
        req->validate();

        // 获取方法名
        std::string method = req->get_method();

        // 查找处理器
        // 首先检查带hdl的同步处理器（用于事件订阅等需要hdl的功能）
        auto sync_hdl_it = m_handler_sync_with_hdl.find(method);
        if (sync_hdl_it != m_handler_sync_with_hdl.end()) {
            sync_hdl_it->second(hdl, req, res);
            return res->compile();
        }

        // 然后检查普通同步处理器
        auto sync_it = m_handler_sync.find(method);
        if (sync_it != m_handler_sync.end()) {
            sync_it->second(req, res);
            return res->compile();
        }

        // 最后检查默认处理器
        auto default_it = m_handler_default.find(method);
        if (default_it != m_handler_default.end()) {
            auto response_ptr = default_it->second(req);
            if (response_ptr) {
                return response_ptr->compile();
            }
            return nullptr; // 无响应（通知）
        }

        // 方法未找到
        res->set_error(static_cast<int64_t>(error_codes::METHOD_NOT_FOUND),
                      "Method not found");

    } catch (const streamdock::jsonrpc::error& e) {
        res->set_error(e.id(), e.what());
    } catch (const std::exception& e) {
        res->set_error(static_cast<int64_t>(error_codes::INTERNAL_ERROR), e.what());
    }

    return res->compile();
}

// 设置事件管理器
void WebSocketServer::set_event_manager(std::shared_ptr<streamdock::events::EventManager> event_manager)
{
    m_event_manager = event_manager;
}

// 连接ID管理
WebSocketServer::connection_id WebSocketServer::get_connection_id(websocketpp::connection_hdl hdl)
{
    std::lock_guard<std::mutex> lock(m_connection_mutex);
    auto it = m_connection_to_id.find(hdl);
    if (it != m_connection_to_id.end()) {
        return it->second;
    }
    return 0; // 未找到
}

bool WebSocketServer::send_to_connection(connection_id id, const std::string& message)
{
    if (!m_running) {
        return false;
    }

    std::lock_guard<std::mutex> lock(m_connection_mutex);
    auto it = m_id_to_connection.find(id);
    if (it != m_id_to_connection.end()) {
        try {
            m_server.send(it->second, message, websocketpp::frame::opcode::text);
            return true;
        } catch (const websocketpp::exception&) {
            // obs_log(LOG_WARNING, "Failed to send message to connection %llu: %s", id, e.what());
            return false;
        }
    }
    return false;
}

void WebSocketServer::cleanup_connection(websocketpp::connection_hdl hdl)
{
    std::lock_guard<std::mutex> lock(m_connection_mutex);
    auto it = m_connection_to_id.find(hdl);
    if (it != m_connection_to_id.end()) {
        connection_id conn_id = it->second;
        m_id_to_connection.erase(conn_id);
        m_connection_to_id.erase(it);
    }
}
