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
#include <obs.h>
#include "json-rpc.h"

// 前向声明
class WebSocketServer;

namespace streamdock {
namespace handlers {

    class obs_source {
    public:
        static std::shared_ptr<obs_source> instance();

        obs_source();
        ~obs_source() = default;

        // 注册 handlers 到 WebSocket 服务器
        void register_handlers(WebSocketServer* server);

    private:
        // UI 线程执行辅助函数
        template<typename Func>
        static void execute_on_ui_thread(Func&& func) {
            struct TaskData {
                std::function<void()> fn;
            };
            TaskData* task = new TaskData{std::forward<Func>(func)};
            obs_queue_task(OBS_TASK_UI, [](void* param) {
                TaskData* t = static_cast<TaskData*>(param);
                t->fn();
                delete t;
            }, task, true);  // 使用 true 同步等待 UI 线程完成
        }
        // === 音频控制 ===
        void _volume_get(
            std::shared_ptr<streamdock::jsonrpc::request> req,
            std::shared_ptr<streamdock::jsonrpc::response> res);

        void _volume_set(
            std::shared_ptr<streamdock::jsonrpc::request> req,
            std::shared_ptr<streamdock::jsonrpc::response> res);

        void _mute_get(
            std::shared_ptr<streamdock::jsonrpc::request> req,
            std::shared_ptr<streamdock::jsonrpc::response> res);

        void _mute_set(
            std::shared_ptr<streamdock::jsonrpc::request> req,
            std::shared_ptr<streamdock::jsonrpc::response> res);

        // === 媒体控制 ===
        void _media_play(
            std::shared_ptr<streamdock::jsonrpc::request> req,
            std::shared_ptr<streamdock::jsonrpc::response> res);

        void _media_pause(
            std::shared_ptr<streamdock::jsonrpc::request> req,
            std::shared_ptr<streamdock::jsonrpc::response> res);

        void _media_restart(
            std::shared_ptr<streamdock::jsonrpc::request> req,
            std::shared_ptr<streamdock::jsonrpc::response> res);

        void _media_stop(
            std::shared_ptr<streamdock::jsonrpc::request> req,
            std::shared_ptr<streamdock::jsonrpc::response> res);

        void _media_next(
            std::shared_ptr<streamdock::jsonrpc::request> req,
            std::shared_ptr<streamdock::jsonrpc::response> res);

        void _media_previous(
            std::shared_ptr<streamdock::jsonrpc::request> req,
            std::shared_ptr<streamdock::jsonrpc::response> res);

        void _media_get_state(
            std::shared_ptr<streamdock::jsonrpc::request> req,
            std::shared_ptr<streamdock::jsonrpc::response> res);

        // === 滤镜控制 ===
        void _filter_list(
            std::shared_ptr<streamdock::jsonrpc::request> req,
            std::shared_ptr<streamdock::jsonrpc::response> res);

        void _filter_set_enabled(
            std::shared_ptr<streamdock::jsonrpc::request> req,
            std::shared_ptr<streamdock::jsonrpc::response> res);

        void _filter_get_enabled(
            std::shared_ptr<streamdock::jsonrpc::request> req,
            std::shared_ptr<streamdock::jsonrpc::response> res);

        // === 源列表 ===
        void _source_list(
            std::shared_ptr<streamdock::jsonrpc::request> req,
            std::shared_ptr<streamdock::jsonrpc::response> res);

        void _volume_list(
            std::shared_ptr<streamdock::jsonrpc::request> req,
            std::shared_ptr<streamdock::jsonrpc::response> res);

        void _media_list(
            std::shared_ptr<streamdock::jsonrpc::request> req,
            std::shared_ptr<streamdock::jsonrpc::response> res);
    };

} // namespace handlers
} // namespace streamdock
