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
#include "json-rpc.h"

// 前向声明
class WebSocketServer;

namespace streamdock {
namespace handlers {

    class system {
    public:
        static std::shared_ptr<system> instance();

        system();
        ~system() = default;

        // 注册 handlers 到 WebSocket 服务器
        void register_handlers(WebSocketServer* server);

    private:
        // Handler 方法
        std::shared_ptr<streamdock::jsonrpc::response> _ping(
            std::shared_ptr<streamdock::jsonrpc::request> req);

        void _version(
            std::shared_ptr<streamdock::jsonrpc::request> req,
            std::shared_ptr<streamdock::jsonrpc::response> res);
    };

} // namespace handlers
} // namespace streamdock
