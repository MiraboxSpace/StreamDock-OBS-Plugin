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

// 前向声明
class WebSocketServer;

namespace streamdock {
namespace handlers {

    class obs_frontend {
    public:
        static std::shared_ptr<obs_frontend> instance();

        obs_frontend();
        ~obs_frontend() = default;

        // 注册 handlers 到 WebSocket 服务器
        void register_handlers(WebSocketServer* server);

    protected:
        // 辅助函数：在 UI 线程上执行 OBS 前端 API 调用
        // 这可以防止从 WebSocket 线程调用时出现 Qt 线程违规
        // 使用同步模式等待 UI 线程完成，以确保响应正确设置
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

    private:
        // 流控制
        void _streaming_start(
            std::shared_ptr<streamdock::jsonrpc::request> req,
            std::shared_ptr<streamdock::jsonrpc::response> res);

        void _streaming_stop(
            std::shared_ptr<streamdock::jsonrpc::request> req,
            std::shared_ptr<streamdock::jsonrpc::response> res);

        void _streaming_active(
            std::shared_ptr<streamdock::jsonrpc::request> req,
            std::shared_ptr<streamdock::jsonrpc::response> res);

        // 录制控制
        void _recording_start(
            std::shared_ptr<streamdock::jsonrpc::request> req,
            std::shared_ptr<streamdock::jsonrpc::response> res);

        void _recording_stop(
            std::shared_ptr<streamdock::jsonrpc::request> req,
            std::shared_ptr<streamdock::jsonrpc::response> res);

        void _recording_active(
            std::shared_ptr<streamdock::jsonrpc::request> req,
            std::shared_ptr<streamdock::jsonrpc::response> res);

        void _recording_pause(
            std::shared_ptr<streamdock::jsonrpc::request> req,
            std::shared_ptr<streamdock::jsonrpc::response> res);

        void _recording_unpause(
            std::shared_ptr<streamdock::jsonrpc::request> req,
            std::shared_ptr<streamdock::jsonrpc::response> res);

        void _recording_paused(
            std::shared_ptr<streamdock::jsonrpc::request> req,
            std::shared_ptr<streamdock::jsonrpc::response> res);

        // 场景控制
        void _scene_list(
            std::shared_ptr<streamdock::jsonrpc::request> req,
            std::shared_ptr<streamdock::jsonrpc::response> res);

        void _scene_get(
            std::shared_ptr<streamdock::jsonrpc::request> req,
            std::shared_ptr<streamdock::jsonrpc::response> res);

        void _scene_set(
            std::shared_ptr<streamdock::jsonrpc::request> req,
            std::shared_ptr<streamdock::jsonrpc::response> res);

        // 转场控制
        void _transition_list(
            std::shared_ptr<streamdock::jsonrpc::request> req,
            std::shared_ptr<streamdock::jsonrpc::response> res);

        void _screenshot(
            std::shared_ptr<streamdock::jsonrpc::request> req,
            std::shared_ptr<streamdock::jsonrpc::response> res);

        // === 虚拟摄像机 ===
        void _virtualcam_start(
            std::shared_ptr<streamdock::jsonrpc::request> req,
            std::shared_ptr<streamdock::jsonrpc::response> res);

        void _virtualcam_stop(
            std::shared_ptr<streamdock::jsonrpc::request> req,
            std::shared_ptr<streamdock::jsonrpc::response> res);

        void _virtualcam_active(
            std::shared_ptr<streamdock::jsonrpc::request> req,
            std::shared_ptr<streamdock::jsonrpc::response> res);

        // === 回放缓存 ===
        void _replaybuffer_enabled(
            std::shared_ptr<streamdock::jsonrpc::request> req,
            std::shared_ptr<streamdock::jsonrpc::response> res);

        void _replaybuffer_start(
            std::shared_ptr<streamdock::jsonrpc::request> req,
            std::shared_ptr<streamdock::jsonrpc::response> res);

        void _replaybuffer_save(
            std::shared_ptr<streamdock::jsonrpc::request> req,
            std::shared_ptr<streamdock::jsonrpc::response> res);

        void _replaybuffer_stop(
            std::shared_ptr<streamdock::jsonrpc::request> req,
            std::shared_ptr<streamdock::jsonrpc::response> res);

        void _replaybuffer_active(
            std::shared_ptr<streamdock::jsonrpc::request> req,
            std::shared_ptr<streamdock::jsonrpc::response> res);

        // === 场景集合 ===
        void _scenecollection_list(
            std::shared_ptr<streamdock::jsonrpc::request> req,
            std::shared_ptr<streamdock::jsonrpc::response> res);

        void _scenecollection_get(
            std::shared_ptr<streamdock::jsonrpc::request> req,
            std::shared_ptr<streamdock::jsonrpc::response> res);

        void _scenecollection_set(
            std::shared_ptr<streamdock::jsonrpc::request> req,
            std::shared_ptr<streamdock::jsonrpc::response> res);

        // === 配置文件 ===
        void _profile_list(
            std::shared_ptr<streamdock::jsonrpc::request> req,
            std::shared_ptr<streamdock::jsonrpc::response> res);

        void _profile_get(
            std::shared_ptr<streamdock::jsonrpc::request> req,
            std::shared_ptr<streamdock::jsonrpc::response> res);

        void _profile_set(
            std::shared_ptr<streamdock::jsonrpc::request> req,
            std::shared_ptr<streamdock::jsonrpc::response> res);

        // === 工作室模式 ===
        void _studiomode_enable(
            std::shared_ptr<streamdock::jsonrpc::request> req,
            std::shared_ptr<streamdock::jsonrpc::response> res);

        void _studiomode_disable(
            std::shared_ptr<streamdock::jsonrpc::request> req,
            std::shared_ptr<streamdock::jsonrpc::response> res);

        void _studiomode_active(
            std::shared_ptr<streamdock::jsonrpc::request> req,
            std::shared_ptr<streamdock::jsonrpc::response> res);

        // === 转场控制 ===
        void _transition_get(
            std::shared_ptr<streamdock::jsonrpc::request> req,
            std::shared_ptr<streamdock::jsonrpc::response> res);

        void _transition_set(
            std::shared_ptr<streamdock::jsonrpc::request> req,
            std::shared_ptr<streamdock::jsonrpc::response> res);

        void _transition_studio(
            std::shared_ptr<streamdock::jsonrpc::request> req,
            std::shared_ptr<streamdock::jsonrpc::response> res);

        // === 章节标记 ===
        void _recording_add_chapter(
            std::shared_ptr<streamdock::jsonrpc::request> req,
            std::shared_ptr<streamdock::jsonrpc::response> res);

        void _recording_split_file(
            std::shared_ptr<streamdock::jsonrpc::request> req,
            std::shared_ptr<streamdock::jsonrpc::response> res);
    };

} // namespace handlers
} // namespace streamdock
