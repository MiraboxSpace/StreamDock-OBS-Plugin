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

#include "handlers/handler-events.h"
#include "websocket-server.h"
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <plugin-support.h>

namespace streamdock {
namespace events {
namespace handlers {

    std::shared_ptr<events> events::instance()
    {
        static std::weak_ptr<events> _instance;
        static std::mutex _lock;

        std::unique_lock<std::mutex> lock(_lock);
        if (_instance.expired()) {
            auto instance = std::make_shared<events>();
            _instance = instance;
            return instance;
        }

        return _instance.lock();
    }

    events::events()
        : m_server(nullptr)
    {
        // Handlers 将在 register_handlers 中注册
    }

    void events::set_event_manager(std::shared_ptr<streamdock::events::EventManager> event_manager)
    {
        m_event_manager = event_manager;
    }

    void events::register_handlers(WebSocketServer* server)
    {
        if (!server) {
            return;
        }

        m_server = server;

        using namespace std::placeholders;

        // 事件订阅相关方法 - 使用带hdl的版本
        server->handle_sync_with_hdl("events.subscribe",
            std::bind(&events::_subscribe_with_hdl, this,
                      std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
        server->handle_sync_with_hdl("events.unsubscribe",
            std::bind(&events::_unsubscribe_with_hdl, this,
                      std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
        server->handle_sync_with_hdl("events.get_subscriptions",
            std::bind(&events::_get_subscriptions_with_hdl, this,
                      std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
        server->handle_sync("events.get_available",
            std::bind(&events::_get_available_events, this,
                      std::placeholders::_1, std::placeholders::_2));

        // obs_log(LOG_INFO, "Event handlers registered");
    }

    void events::_subscribe_with_hdl(
        websocketpp::connection_hdl hdl,
        std::shared_ptr<streamdock::jsonrpc::request> req,
        std::shared_ptr<streamdock::jsonrpc::response> res)
    {
        if (!m_event_manager || !m_server) {
            res->set_error(static_cast<int64_t>(streamdock::jsonrpc::error_codes::INTERNAL_ERROR),
                          "Event manager or server not initialized");
            return;
        }

        nlohmann::json params;
        if (!req->get_params(params)) {
            throw streamdock::jsonrpc::invalid_params_error("Missing parameters");
        }

        if (!params.contains("events") || !params["events"].is_array()) {
            throw streamdock::jsonrpc::invalid_params_error("Missing or invalid 'events' parameter");
        }

        // 解析事件列表
        std::set<streamdock::events::EventType> event_types;
        for (const auto& event_str : params["events"]) {
            if (event_str.is_string()) {
                std::string event_name = event_str.get<std::string>();
                auto event_type = streamdock::events::string_to_event_type(event_name);
                if (event_type != streamdock::events::EventType::Custom) {
                    event_types.insert(event_type);
                }
            }
        }

        // 获取连接ID
        uint64_t connection_id = m_server->get_connection_id(hdl);
        if (connection_id == 0) {
            res->set_error(static_cast<int64_t>(streamdock::jsonrpc::error_codes::INTERNAL_ERROR),
                          "Invalid connection");
            return;
        }

        // 执行订阅
        bool success = m_event_manager->subscribe(connection_id, event_types);

        nlohmann::json result;
        result["success"] = success;
        if (success) {
            result["subscribed_count"] = static_cast<int>(event_types.size());

            nlohmann::json subscribed_events;
            for (const auto& event_type : event_types) {
                subscribed_events.push_back(streamdock::events::event_type_to_string(event_type));
            }
            result["events"] = subscribed_events;
        } else {
            result["message"] = "Failed to subscribe to events";
        }
        res->set_result(result);

        // obs_log(LOG_INFO, "Client subscribed to %zu events", event_types.size());
    }

    void events::_unsubscribe_with_hdl(
        websocketpp::connection_hdl hdl,
        std::shared_ptr<streamdock::jsonrpc::request> req,
        std::shared_ptr<streamdock::jsonrpc::response> res)
    {
        if (!m_event_manager || !m_server) {
            res->set_error(static_cast<int64_t>(streamdock::jsonrpc::error_codes::INTERNAL_ERROR),
                          "Event manager or server not initialized");
            return;
        }

        nlohmann::json params;
        if (!req->get_params(params)) {
            throw streamdock::jsonrpc::invalid_params_error("Missing parameters");
        }

        if (!params.contains("events") || !params["events"].is_array()) {
            throw streamdock::jsonrpc::invalid_params_error("Missing or invalid 'events' parameter");
        }

        // 获取连接ID
        uint64_t connection_id = m_server->get_connection_id(hdl);
        if (connection_id == 0) {
            res->set_error(static_cast<int64_t>(streamdock::jsonrpc::error_codes::INTERNAL_ERROR),
                          "Invalid connection");
            return;
        }

        // 解析事件列表并取消订阅
        std::set<streamdock::events::EventType> event_types;
        for (const auto& event_str : params["events"]) {
            if (event_str.is_string()) {
                std::string event_name = event_str.get<std::string>();
                auto event_type = streamdock::events::string_to_event_type(event_name);
                if (event_type != streamdock::events::EventType::Custom) {
                    event_types.insert(event_type);
                }
            }
        }

        // 执行取消订阅
        for (const auto& event_type : event_types) {
            m_event_manager->unsubscribe(connection_id, event_type);
        }

        nlohmann::json result;
        result["success"] = true;
        result["unsubscribed_count"] = static_cast<int>(event_types.size());

        nlohmann::json unsubscribed_events;
        for (const auto& event_type : event_types) {
            unsubscribed_events.push_back(streamdock::events::event_type_to_string(event_type));
        }
        result["events"] = unsubscribed_events;

        res->set_result(result);

        // obs_log(LOG_INFO, "Client unsubscribed from %zu events", event_types.size());
    }

    void events::_get_subscriptions_with_hdl(
        websocketpp::connection_hdl hdl,
        [[maybe_unused]] std::shared_ptr<streamdock::jsonrpc::request> req,
        std::shared_ptr<streamdock::jsonrpc::response> res)
    {
        if (!m_event_manager || !m_server) {
            res->set_error(static_cast<int64_t>(streamdock::jsonrpc::error_codes::INTERNAL_ERROR),
                          "Event manager or server not initialized");
            return;
        }

        // 获取连接ID
        uint64_t connection_id = m_server->get_connection_id(hdl);
        if (connection_id == 0) {
            res->set_error(static_cast<int64_t>(streamdock::jsonrpc::error_codes::INTERNAL_ERROR),
                          "Invalid connection");
            return;
        }

        std::set<streamdock::events::EventType> subscriptions = m_event_manager->get_subscriptions(connection_id);

        nlohmann::json result;
        result["subscriptions"] = nlohmann::json::array();
        for (const auto& event_type : subscriptions) {
            result["subscriptions"].push_back(streamdock::events::event_type_to_string(event_type));
        }
        res->set_result(result);
    }

    void events::_subscribe(
        std::shared_ptr<streamdock::jsonrpc::request> req,
        std::shared_ptr<streamdock::jsonrpc::response> res)
    {
        if (!m_event_manager) {
            res->set_error(static_cast<int64_t>(streamdock::jsonrpc::error_codes::INTERNAL_ERROR),
                          "Event manager not initialized");
            return;
        }

        nlohmann::json params;
        if (!req->get_params(params)) {
            throw streamdock::jsonrpc::invalid_params_error("Missing parameters");
        }

        if (!params.contains("events") || !params["events"].is_array()) {
            throw streamdock::jsonrpc::invalid_params_error("Missing or invalid 'events' parameter");
        }

        // 获取连接句柄（从请求中获取）
        // 注意：这里需要从WebSocketServer获取connection_hdl
        // 为了简化，我们假设client中存储了hdl，或者通过其他方式获取
        // 实际实现可能需要调整request类以存储connection_hdl

        // 解析事件列表
        std::set<streamdock::events::EventType> event_types;
        for (const auto& event_str : params["events"]) {
            if (event_str.is_string()) {
                std::string event_name = event_str.get<std::string>();
                auto event_type = streamdock::events::string_to_event_type(event_name);
                if (event_type != streamdock::events::EventType::Custom) {
                    event_types.insert(event_type);
                }
            }
        }

        // 注意：这里的实现需要访问connection_hdl
        // 由于当前设计中request不包含hdl，我们需要修改设计
        // 暂时返回一个提示，需要在实际集成时修改

        nlohmann::json result;
        result["success"] = false;
        result["message"] = "Event subscription requires connection handle integration";
        res->set_result(result);
    }

    void events::_unsubscribe(
        std::shared_ptr<streamdock::jsonrpc::request> req,
        std::shared_ptr<streamdock::jsonrpc::response> res)
    {
        if (!m_event_manager) {
            res->set_error(static_cast<int64_t>(streamdock::jsonrpc::error_codes::INTERNAL_ERROR),
                          "Event manager not initialized");
            return;
        }

        nlohmann::json params;
        if (!req->get_params(params)) {
            throw streamdock::jsonrpc::invalid_params_error("Missing parameters");
        }

        if (!params.contains("events") || !params["events"].is_array()) {
            throw streamdock::jsonrpc::invalid_params_error("Missing or invalid 'events' parameter");
        }

        // 类似_subscribe，需要connection_hdl
        nlohmann::json result;
        result["success"] = false;
        result["message"] = "Event unsubscription requires connection handle integration";
        res->set_result(result);
    }

    void events::_get_subscriptions(
        [[maybe_unused]] std::shared_ptr<streamdock::jsonrpc::request> req,
        std::shared_ptr<streamdock::jsonrpc::response> res)
    {
        if (!m_event_manager) {
            res->set_error(static_cast<int64_t>(streamdock::jsonrpc::error_codes::INTERNAL_ERROR),
                          "Event manager not initialized");
            return;
        }

        // 需要connection_hdl来获取订阅
        nlohmann::json result;
        result["subscriptions"] = nlohmann::json::array();
        result["message"] = "Getting subscriptions requires connection handle integration";
        res->set_result(result);
    }

    void events::_get_available_events(
        [[maybe_unused]] std::shared_ptr<streamdock::jsonrpc::request> req,
        std::shared_ptr<streamdock::jsonrpc::response> res)
    {
        std::vector<std::string> available_events = streamdock::events::EventManager::get_available_events();

        nlohmann::json result;
        result["events"] = available_events;
        res->set_result(result);
    }

} // namespace handlers
} // namespace events
} // namespace streamdock
