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

#include "handlers/handler-system.h"
#include "websocket-server.h"
#include <obs-module.h>
#include <plugin-support.h>

// 定义插件版本（需要在某处定义）
#ifndef PLUGIN_VERSION
#define PLUGIN_VERSION "1.0.0"
#endif

namespace streamdock {
namespace handlers {

    std::shared_ptr<system> system::instance()
    {
        static std::weak_ptr<system> _instance;
        static std::mutex _lock;

        std::unique_lock<std::mutex> lock(_lock);
        if (_instance.expired()) {
            auto instance = std::make_shared<system>();
            _instance = instance;
            return instance;
        }

        return _instance.lock();
    }

    system::system()
    {
        // Handlers 将在 register_handlers 中注册
    }

    void system::register_handlers(WebSocketServer* server)
    {
        if (!server) {
            return;
        }

        // 注册 ping handler（无响应）
        server->handle("ping",
            std::bind(&system::_ping, this, std::placeholders::_1));

        // 注册 version handler（同步）
        server->handle_sync("version",
            std::bind(&system::_version, this,
                      std::placeholders::_1, std::placeholders::_2));
    }

    std::shared_ptr<streamdock::jsonrpc::response> system::_ping(
        std::shared_ptr<streamdock::jsonrpc::request>)
    {
        // Ping 无需响应
        return nullptr;
    }

    void system::_version(
        std::shared_ptr<streamdock::jsonrpc::request> req,
        std::shared_ptr<streamdock::jsonrpc::response> res)
    {
        // 获取客户端版本（如果提供）
        nlohmann::json params;
        if (req->get_params(params)) {
            if (params.is_object() && params.contains("version") &&
                params["version"].is_string()) {
                auto client = req->get_client();
                if (client) {
                    client->set_version(params["version"].get<std::string>());
                }
            }
        }

        // 构建响应
        nlohmann::json result = nlohmann::json::object();
        result["version"] = PLUGIN_VERSION;
        result["semver"] = {1, 0, 0}; // 主版本、次版本、补丁版本
        result["obsver"] = obs_get_version();
        result["obsverstr"] = obs_get_version_string();

        res->set_result(result);
    }

} // namespace handlers
} // namespace streamdock
