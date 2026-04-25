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

#include "handlers/handler-obs-frontend.h"
#include "websocket-server.h"
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <obs-source.h>
#include <plugin-support.h>
#include <condition_variable>
#include <chrono>

namespace {
    // 一次性事件等待器，用于等待 OBS 前端事件触发
    class EventWaiter {
        std::mutex mtx;
        std::condition_variable cv;
        bool event_received = false;
        obs_frontend_event target_event;

        // 静态回调函数
        static void event_callback(obs_frontend_event event, void* param) {
            EventWaiter* waiter = static_cast<EventWaiter*>(param);
            if (event == waiter->target_event) {
                {
                    std::lock_guard<std::mutex> inner_lock(waiter->mtx);
                    waiter->event_received = true;
                }
                waiter->cv.notify_one();
            }
        }

    public:
        // 等待指定事件，返回是否成功收到事件
        bool wait_for_event(obs_frontend_event event, int timeout_ms = 3000) {
            std::unique_lock<std::mutex> lock(mtx);
            event_received = false;
            target_event = event;

            // 注册一次性事件回调
            obs_frontend_add_event_callback(event_callback, this);

            // 等待事件触发
            bool success = cv.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                [this] { return event_received; });

            // 清理回调
            obs_frontend_remove_event_callback(event_callback, this);
            return success;
        }
    };
}

namespace streamdock {
namespace handlers {

    std::shared_ptr<obs_frontend> obs_frontend::instance()
    {
        static std::weak_ptr<obs_frontend> _instance;
        static std::mutex _lock;

        std::unique_lock<std::mutex> lock(_lock);
        if (_instance.expired()) {
            auto instance = std::make_shared<obs_frontend>();
            _instance = instance;
            return instance;
        }

        return _instance.lock();
    }

    obs_frontend::obs_frontend()
    {
        // Handlers 将在 register_handlers 中注册
    }

    void obs_frontend::register_handlers(WebSocketServer* server)
    {
        if (!server) {
            return;
        }

        using namespace std::placeholders;

        // === 流控制 ===
        server->handle_sync("streaming.start",
            std::bind(&obs_frontend::_streaming_start, this,
                      std::placeholders::_1, std::placeholders::_2));
        server->handle_sync("streaming.stop",
            std::bind(&obs_frontend::_streaming_stop, this,
                      std::placeholders::_1, std::placeholders::_2));
        server->handle_sync("streaming.active",
            std::bind(&obs_frontend::_streaming_active, this,
                      std::placeholders::_1, std::placeholders::_2));

        // === 录制控制 ===
        server->handle_sync("recording.start",
            std::bind(&obs_frontend::_recording_start, this,
                      std::placeholders::_1, std::placeholders::_2));
        server->handle_sync("recording.stop",
            std::bind(&obs_frontend::_recording_stop, this,
                      std::placeholders::_1, std::placeholders::_2));
        server->handle_sync("recording.active",
            std::bind(&obs_frontend::_recording_active, this,
                      std::placeholders::_1, std::placeholders::_2));
        server->handle_sync("recording.pause",
            std::bind(&obs_frontend::_recording_pause, this,
                      std::placeholders::_1, std::placeholders::_2));
        server->handle_sync("recording.unpause",
            std::bind(&obs_frontend::_recording_unpause, this,
                      std::placeholders::_1, std::placeholders::_2));
        server->handle_sync("recording.paused",
            std::bind(&obs_frontend::_recording_paused, this,
                      std::placeholders::_1, std::placeholders::_2));

        // === 场景控制 ===
        server->handle_sync("scene.list",
            std::bind(&obs_frontend::_scene_list, this,
                      std::placeholders::_1, std::placeholders::_2));
        server->handle_sync("scene.get",
            std::bind(&obs_frontend::_scene_get, this,
                      std::placeholders::_1, std::placeholders::_2));
        server->handle_sync("scene.set",
            std::bind(&obs_frontend::_scene_set, this,
                      std::placeholders::_1, std::placeholders::_2));

        // === 转场控制 ===
        server->handle_sync("transition.list",
            std::bind(&obs_frontend::_transition_list, this,
                      std::placeholders::_1, std::placeholders::_2));

        // === 其他功能 ===
        server->handle_sync("screenshot",
            std::bind(&obs_frontend::_screenshot, this,
                      std::placeholders::_1, std::placeholders::_2));

        // === 虚拟摄像机 ===
        server->handle_sync("virtualcam.start",
            std::bind(&obs_frontend::_virtualcam_start, this,
                      std::placeholders::_1, std::placeholders::_2));
        server->handle_sync("virtualcam.stop",
            std::bind(&obs_frontend::_virtualcam_stop, this,
                      std::placeholders::_1, std::placeholders::_2));
        server->handle_sync("virtualcam.active",
            std::bind(&obs_frontend::_virtualcam_active, this,
                      std::placeholders::_1, std::placeholders::_2));

        // === 回放缓存 ===
        server->handle_sync("replaybuffer.enabled",
            std::bind(&obs_frontend::_replaybuffer_enabled, this,
                      std::placeholders::_1, std::placeholders::_2));
        server->handle_sync("replaybuffer.start",
            std::bind(&obs_frontend::_replaybuffer_start, this,
                      std::placeholders::_1, std::placeholders::_2));
        server->handle_sync("replaybuffer.save",
            std::bind(&obs_frontend::_replaybuffer_save, this,
                      std::placeholders::_1, std::placeholders::_2));
        server->handle_sync("replaybuffer.stop",
            std::bind(&obs_frontend::_replaybuffer_stop, this,
                      std::placeholders::_1, std::placeholders::_2));
        server->handle_sync("replaybuffer.active",
            std::bind(&obs_frontend::_replaybuffer_active, this,
                      std::placeholders::_1, std::placeholders::_2));

        // === 场景集合 ===
        server->handle_sync("scenecollection.list",
            std::bind(&obs_frontend::_scenecollection_list, this,
                      std::placeholders::_1, std::placeholders::_2));
        server->handle_sync("scenecollection.get",
            std::bind(&obs_frontend::_scenecollection_get, this,
                      std::placeholders::_1, std::placeholders::_2));
        server->handle_sync("scenecollection.set",
            std::bind(&obs_frontend::_scenecollection_set, this,
                      std::placeholders::_1, std::placeholders::_2));

        // === 配置文件 ===
        server->handle_sync("profile.list",
            std::bind(&obs_frontend::_profile_list, this,
                      std::placeholders::_1, std::placeholders::_2));
        server->handle_sync("profile.get",
            std::bind(&obs_frontend::_profile_get, this,
                      std::placeholders::_1, std::placeholders::_2));
        server->handle_sync("profile.set",
            std::bind(&obs_frontend::_profile_set, this,
                      std::placeholders::_1, std::placeholders::_2));

        // === 工作室模式 ===
        server->handle_sync("studiomode.enable",
            std::bind(&obs_frontend::_studiomode_enable, this,
                      std::placeholders::_1, std::placeholders::_2));
        server->handle_sync("studiomode.disable",
            std::bind(&obs_frontend::_studiomode_disable, this,
                      std::placeholders::_1, std::placeholders::_2));
        server->handle_sync("studiomode.active",
            std::bind(&obs_frontend::_studiomode_active, this,
                      std::placeholders::_1, std::placeholders::_2));

        // === 转场控制 ===
        server->handle_sync("transition.get",
            std::bind(&obs_frontend::_transition_get, this,
                      std::placeholders::_1, std::placeholders::_2));
        server->handle_sync("transition.set",
            std::bind(&obs_frontend::_transition_set, this,
                      std::placeholders::_1, std::placeholders::_2));
        server->handle_sync("transition.studio",
            std::bind(&obs_frontend::_transition_studio, this,
                      std::placeholders::_1, std::placeholders::_2));

        // === 章节标记 ===
        server->handle_sync("recording.addchapter",
            std::bind(&obs_frontend::_recording_add_chapter, this,
                      std::placeholders::_1, std::placeholders::_2));
        server->handle_sync("recording.splitfile",
            std::bind(&obs_frontend::_recording_split_file, this,
                      std::placeholders::_1, std::placeholders::_2));
    }

    // === 流控制 ===

    void obs_frontend::_streaming_start(
        std::shared_ptr<streamdock::jsonrpc::request>,
        std::shared_ptr<streamdock::jsonrpc::response> res)
    {
        execute_on_ui_thread([res]() {
            obs_frontend_streaming_start();

            nlohmann::json result;
            result["success"] = true;
            res->set_result(result);
        });
    }

    void obs_frontend::_streaming_stop(
        std::shared_ptr<streamdock::jsonrpc::request>,
        std::shared_ptr<streamdock::jsonrpc::response> res)
    {
        execute_on_ui_thread([res]() {
            obs_frontend_streaming_stop();

            nlohmann::json result;
            result["success"] = true;
            res->set_result(result);
        });
    }

    void obs_frontend::_streaming_active(
        std::shared_ptr<streamdock::jsonrpc::request>,
        std::shared_ptr<streamdock::jsonrpc::response> res)
    {
        bool active = obs_frontend_streaming_active();

        nlohmann::json result;
        result["active"] = active;
        res->set_result(result);
    }

    // === 录制控制 ===

    void obs_frontend::_recording_start(
        std::shared_ptr<streamdock::jsonrpc::request>,
        std::shared_ptr<streamdock::jsonrpc::response> res)
    {
        execute_on_ui_thread([res]() {
            obs_frontend_recording_start();

            nlohmann::json result;
            result["success"] = true;
            res->set_result(result);
        });
    }

    void obs_frontend::_recording_stop(
        std::shared_ptr<streamdock::jsonrpc::request>,
        std::shared_ptr<streamdock::jsonrpc::response> res)
    {
        execute_on_ui_thread([res]() {
            obs_frontend_recording_stop();

            nlohmann::json result;
            result["success"] = true;
            res->set_result(result);
        });
    }

    void obs_frontend::_recording_active(
        std::shared_ptr<streamdock::jsonrpc::request>,
        std::shared_ptr<streamdock::jsonrpc::response> res)
    {
        bool active = obs_frontend_recording_active();
        bool paused = obs_frontend_recording_paused();

        nlohmann::json result;
        result["active"] = active;
        result["paused"] = paused;
        res->set_result(result);
    }

    void obs_frontend::_recording_pause(
        std::shared_ptr<streamdock::jsonrpc::request>,
        std::shared_ptr<streamdock::jsonrpc::response> res)
    {
        execute_on_ui_thread([res]() {
            obs_frontend_recording_pause(true);

            nlohmann::json result;
            result["success"] = true;
            res->set_result(result);
        });
    }

    void obs_frontend::_recording_unpause(
        std::shared_ptr<streamdock::jsonrpc::request>,
        std::shared_ptr<streamdock::jsonrpc::response> res)
    {
        execute_on_ui_thread([res]() {
            obs_frontend_recording_pause(false);

            nlohmann::json result;
            result["success"] = true;
            res->set_result(result);
        });
    }

    void obs_frontend::_recording_paused(
        std::shared_ptr<streamdock::jsonrpc::request>,
        std::shared_ptr<streamdock::jsonrpc::response> res)
    {
        bool paused = obs_frontend_recording_paused();

        nlohmann::json result;
        result["paused"] = paused;
        res->set_result(result);
    }

    // === 场景控制 ===

    void obs_frontend::_scene_list(
        std::shared_ptr<streamdock::jsonrpc::request>,
        std::shared_ptr<streamdock::jsonrpc::response> res)
    {
        execute_on_ui_thread([res]() {
            // 使用正确的 OBS API 获取场景列表
            nlohmann::json scenes = nlohmann::json::array();

            struct obs_frontend_source_list list = {0};
            obs_frontend_get_scenes(&list);

            for (size_t idx = 0; idx < list.sources.num; idx++) {
                obs_source_t* scene = *(list.sources.array + idx);
                const char* name = obs_source_get_name(scene);

                nlohmann::json scene_info;
                scene_info["name"] = name ? name : "";
                scene_info["id"] = static_cast<int>(idx);
                scenes.push_back(scene_info);
            }

            obs_frontend_source_list_free(&list);

            nlohmann::json result;
            result["scenes"] = scenes;
            res->set_result(result);
        });
    }

    void obs_frontend::_scene_get(
        std::shared_ptr<streamdock::jsonrpc::request>,
        std::shared_ptr<streamdock::jsonrpc::response> res)
    {
        execute_on_ui_thread([res]() {
            obs_source_t* current_scene = obs_frontend_get_current_scene();

            if (!current_scene) {
                res->set_error(static_cast<int64_t>(streamdock::jsonrpc::error_codes::INTERNAL_ERROR),
                              "Failed to get current scene");
                return;
            }

            const char* name = obs_source_get_name(current_scene);

            nlohmann::json result;
            result["name"] = name ? name : "";

            obs_source_release(current_scene);
            res->set_result(result);
        });
    }

    void obs_frontend::_scene_set(
        std::shared_ptr<streamdock::jsonrpc::request> req,
        std::shared_ptr<streamdock::jsonrpc::response> res)
    {
        nlohmann::json params;
        if (!req->get_params(params)) {
            throw streamdock::jsonrpc::invalid_params_error("Missing parameters");
        }

        if (!params.contains("name") || !params["name"].is_string()) {
            throw streamdock::jsonrpc::invalid_params_error("Missing or invalid 'name' parameter");
        }

        std::string scene_name = params["name"].get<std::string>();

        execute_on_ui_thread([res, scene_name]() {
            // 使用 OBS API 查找场景
            obs_source_t* target_scene = obs_get_source_by_name(scene_name.c_str());

            if (!target_scene) {
                res->set_error(static_cast<int64_t>(streamdock::jsonrpc::error_codes::INVALID_PARAMS),
                              "Scene not found");
                return;
            }

            obs_frontend_set_current_scene(target_scene);
            obs_source_release(target_scene);

            nlohmann::json result;
            result["success"] = true;
            result["scene"] = scene_name;
            res->set_result(result);
        });
    }

    // === 转场控制 ===

    void obs_frontend::_transition_list(
        std::shared_ptr<streamdock::jsonrpc::request>,
        std::shared_ptr<streamdock::jsonrpc::response> res)
    {
        execute_on_ui_thread([res]() {
            // 获取所有可用的转场
            struct obs_frontend_source_list list = {0};
            obs_frontend_get_transitions(&list);

            nlohmann::json transitions = nlohmann::json::array();
            for (size_t idx = 0; idx < list.sources.num; idx++) {
                obs_source_t* transition = list.sources.array[idx];
                const char* name = obs_source_get_name(transition);
                if (name) {
                    transitions.push_back(name);
                }
            }

            obs_frontend_source_list_free(&list);

            nlohmann::json result;
            result["transitions"] = transitions;
            res->set_result(result);
        });
    }

    void obs_frontend::_screenshot(
        std::shared_ptr<streamdock::jsonrpc::request>,
        std::shared_ptr<streamdock::jsonrpc::response> res)
    {
        execute_on_ui_thread([res]() {
            obs_frontend_take_screenshot();

            nlohmann::json result;
            result["success"] = true;
            res->set_result(result);
        });
    }

    // === 虚拟摄像机 ===

    void obs_frontend::_virtualcam_start(
        std::shared_ptr<streamdock::jsonrpc::request>,
        std::shared_ptr<streamdock::jsonrpc::response> res)
    {
        execute_on_ui_thread([res]() {
            if (!obs_frontend_virtualcam_active()) {
                obs_frontend_start_virtualcam();

                // 等待 VIRTUALCAM_STARTED 事件触发
                EventWaiter waiter;
                waiter.wait_for_event(OBS_FRONTEND_EVENT_VIRTUALCAM_STARTED);
            }

            nlohmann::json result;
            result["active"] = obs_frontend_virtualcam_active();
            res->set_result(result);
        });
    }

    void obs_frontend::_virtualcam_stop(
        std::shared_ptr<streamdock::jsonrpc::request>,
        std::shared_ptr<streamdock::jsonrpc::response> res)
    {
        execute_on_ui_thread([res]() {
            if (obs_frontend_virtualcam_active()) {
                obs_frontend_stop_virtualcam();

                // 等待 VIRTUALCAM_STOPPED 事件触发
                EventWaiter waiter;
                waiter.wait_for_event(OBS_FRONTEND_EVENT_VIRTUALCAM_STOPPED);
            }

            nlohmann::json result;
            result["active"] = obs_frontend_virtualcam_active();
            res->set_result(result);
        });
    }

    void obs_frontend::_virtualcam_active(
        std::shared_ptr<streamdock::jsonrpc::request>,
        std::shared_ptr<streamdock::jsonrpc::response> res)
    {
        bool active = obs_frontend_virtualcam_active();

        nlohmann::json result;
        result["active"] = active;
        res->set_result(result);
    }

    // === 回放缓存 ===

    void obs_frontend::_replaybuffer_enabled(
        std::shared_ptr<streamdock::jsonrpc::request>,
        std::shared_ptr<streamdock::jsonrpc::response> res)
    {
        execute_on_ui_thread([res]() {
            // 检查是否配置了回放缓存
            obs_output_t* replay_buffer = obs_frontend_get_replay_buffer_output();
            bool enabled = (replay_buffer != nullptr);
            if (replay_buffer) {
                obs_output_release(replay_buffer);
            }

            nlohmann::json result;
            result["enabled"] = enabled;
            res->set_result(result);
        });
    }

    void obs_frontend::_replaybuffer_start(
        std::shared_ptr<streamdock::jsonrpc::request>,
        std::shared_ptr<streamdock::jsonrpc::response> res)
    {
        execute_on_ui_thread([res]() {
            if (!obs_frontend_replay_buffer_active()) {
                obs_frontend_replay_buffer_start();

                // 等待 REPLAY_BUFFER_STARTED 事件触发
                EventWaiter waiter;
                waiter.wait_for_event(OBS_FRONTEND_EVENT_REPLAY_BUFFER_STARTED);
            }

            nlohmann::json result;
            result["active"] = obs_frontend_replay_buffer_active();
            res->set_result(result);
        });
    }

    void obs_frontend::_replaybuffer_save(
        std::shared_ptr<streamdock::jsonrpc::request>,
        std::shared_ptr<streamdock::jsonrpc::response> res)
    {
        execute_on_ui_thread([res]() {
            // 检查是否启用和活动
            obs_output_t* replay_buffer = obs_frontend_get_replay_buffer_output();
            bool enabled = (replay_buffer != nullptr);
            bool active = obs_frontend_replay_buffer_active();
            bool success = false;

            if (replay_buffer) {
                obs_output_release(replay_buffer);
            }

            if (enabled && active) {
                obs_frontend_replay_buffer_save();
                success = true;
            }

            nlohmann::json result;
            result["success"] = success;
            res->set_result(result);
        });
    }

    void obs_frontend::_replaybuffer_stop(
        std::shared_ptr<streamdock::jsonrpc::request>,
        std::shared_ptr<streamdock::jsonrpc::response> res)
    {
        execute_on_ui_thread([res]() {
            if (obs_frontend_replay_buffer_active()) {
                obs_frontend_replay_buffer_stop();

                // 等待 REPLAY_BUFFER_STOPPED 事件触发
                EventWaiter waiter;
                waiter.wait_for_event(OBS_FRONTEND_EVENT_REPLAY_BUFFER_STOPPED);
            }

            nlohmann::json result;
            result["active"] = obs_frontend_replay_buffer_active();
            res->set_result(result);
        });
    }

    void obs_frontend::_replaybuffer_active(
        std::shared_ptr<streamdock::jsonrpc::request>,
        std::shared_ptr<streamdock::jsonrpc::response> res)
    {
        bool active = obs_frontend_replay_buffer_active();

        nlohmann::json result;
        result["active"] = active;
        res->set_result(result);
    }

    // === 场景集合 ===

    void obs_frontend::_scenecollection_list(
        std::shared_ptr<streamdock::jsonrpc::request>,
        std::shared_ptr<streamdock::jsonrpc::response> res)
    {
        execute_on_ui_thread([res]() {
            nlohmann::json collections = nlohmann::json::array();

            char** list = obs_frontend_get_scene_collections();
            for (char** ptr = list; *ptr != nullptr; ptr++) {
                collections.push_back(*ptr);
            }
            bfree(list);

            nlohmann::json result;
            result["collections"] = collections;
            res->set_result(result);
        });
    }

    void obs_frontend::_scenecollection_get(
        std::shared_ptr<streamdock::jsonrpc::request>,
        std::shared_ptr<streamdock::jsonrpc::response> res)
    {
        char* current = obs_frontend_get_current_scene_collection();

        nlohmann::json result;
        result["name"] = current ? current : "";
        res->set_result(result);

        if (current) {
            bfree(current);
        }
    }

    void obs_frontend::_scenecollection_set(
        std::shared_ptr<streamdock::jsonrpc::request> req,
        std::shared_ptr<streamdock::jsonrpc::response> res)
    {
        nlohmann::json params;
        if (!req->get_params(params)) {
            throw streamdock::jsonrpc::invalid_params_error("Missing parameters");
        }

        if (!params.contains("name") || !params["name"].is_string()) {
            throw streamdock::jsonrpc::invalid_params_error("Missing or invalid 'name' parameter");
        }

        std::string collection_name = params["name"].get<std::string>();

        execute_on_ui_thread([res, collection_name]() {
            obs_frontend_set_current_scene_collection(collection_name.c_str());

            nlohmann::json result;
            result["success"] = true;
            result["name"] = collection_name;
            res->set_result(result);
        });
    }

    // === 配置文件 ===

    void obs_frontend::_profile_list(
        std::shared_ptr<streamdock::jsonrpc::request>,
        std::shared_ptr<streamdock::jsonrpc::response> res)
    {
        execute_on_ui_thread([res]() {
            nlohmann::json profiles = nlohmann::json::array();

            char** list = obs_frontend_get_profiles();
            for (char** ptr = list; *ptr != nullptr; ptr++) {
                profiles.push_back(*ptr);
            }
            bfree(list);

            nlohmann::json result;
            result["profiles"] = profiles;
            res->set_result(result);
        });
    }

    void obs_frontend::_profile_get(
        std::shared_ptr<streamdock::jsonrpc::request>,
        std::shared_ptr<streamdock::jsonrpc::response> res)
    {
        char* current = obs_frontend_get_current_profile();

        nlohmann::json result;
        result["name"] = current ? current : "";
        res->set_result(result);

        if (current) {
            bfree(current);
        }
    }

    void obs_frontend::_profile_set(
        std::shared_ptr<streamdock::jsonrpc::request> req,
        std::shared_ptr<streamdock::jsonrpc::response> res)
    {
        nlohmann::json params;
        if (!req->get_params(params)) {
            throw streamdock::jsonrpc::invalid_params_error("Missing parameters");
        }

        if (!params.contains("name") || !params["name"].is_string()) {
            throw streamdock::jsonrpc::invalid_params_error("Missing or invalid 'name' parameter");
        }

        std::string profile_name = params["name"].get<std::string>();

        execute_on_ui_thread([res, profile_name]() {
            obs_frontend_set_current_profile(profile_name.c_str());

            nlohmann::json result;
            result["success"] = true;
            result["name"] = profile_name;
            res->set_result(result);
        });
    }

    // === 工作室模式 ===

    void obs_frontend::_studiomode_enable(
        std::shared_ptr<streamdock::jsonrpc::request>,
        std::shared_ptr<streamdock::jsonrpc::response> res)
    {
        execute_on_ui_thread([res]() {
            if (!obs_frontend_preview_program_mode_active()) {
                obs_frontend_set_preview_program_mode(true);
            }

            nlohmann::json result;
            result["active"] = obs_frontend_preview_program_mode_active();
            res->set_result(result);
        });
    }

    void obs_frontend::_studiomode_disable(
        std::shared_ptr<streamdock::jsonrpc::request>,
        std::shared_ptr<streamdock::jsonrpc::response> res)
    {
        execute_on_ui_thread([res]() {
            if (obs_frontend_preview_program_mode_active()) {
                obs_frontend_set_preview_program_mode(false);
            }

            nlohmann::json result;
            result["active"] = obs_frontend_preview_program_mode_active();
            res->set_result(result);
        });
    }

    void obs_frontend::_studiomode_active(
        std::shared_ptr<streamdock::jsonrpc::request>,
        std::shared_ptr<streamdock::jsonrpc::response> res)
    {
        bool active = obs_frontend_preview_program_mode_active();

        nlohmann::json result;
        result["active"] = active;
        res->set_result(result);
    }

    // === 转场控制 ===

    void obs_frontend::_transition_get(
        std::shared_ptr<streamdock::jsonrpc::request>,
        std::shared_ptr<streamdock::jsonrpc::response> res)
    {
        execute_on_ui_thread([res]() {
            obs_source_t* current = obs_frontend_get_current_transition();
            const char* name = obs_source_get_name(current);
            int duration = obs_frontend_get_transition_duration();

            nlohmann::json result;
            result["name"] = name ? name : "";
            result["duration"] = duration;

            obs_source_release(current);
            res->set_result(result);
        });
    }

    void obs_frontend::_transition_set(
        std::shared_ptr<streamdock::jsonrpc::request> req,
        std::shared_ptr<streamdock::jsonrpc::response> res)
    {
        nlohmann::json params;
        if (!req->get_params(params)) {
            throw streamdock::jsonrpc::invalid_params_error("Missing parameters");
        }

        // Extract parameters before executing on UI thread
        std::string transition_name;
        bool has_name = params.contains("name") && params["name"].is_string();
        if (has_name) {
            transition_name = params["name"].get<std::string>();
        }

        bool has_duration = params.contains("duration") && params["duration"].is_number_integer();
        int duration = has_duration ? params["duration"].get<int>() : 0;

        execute_on_ui_thread([res, has_name, transition_name, has_duration, duration]() {
            // 设置转场类型
            if (has_name) {
                // 获取所有转场并查找匹配的
                struct obs_frontend_source_list list = {0};
                obs_frontend_get_transitions(&list);

                for (size_t idx = 0; idx < list.sources.num; idx++) {
                    obs_source_t* transition = *(list.sources.array + idx);
                    const char* name = obs_source_get_name(transition);
                    if (name && transition_name == name) {
                        obs_frontend_set_current_transition(transition);
                        break;
                    }
                }

                obs_frontend_source_list_free(&list);
            }

            // 设置转场持续时间
            if (has_duration) {
                obs_frontend_set_transition_duration(duration);
            }

            // 返回当前状态
            obs_source_t* current = obs_frontend_get_current_transition();
            const char* name = obs_source_get_name(current);
            int actual_duration = obs_frontend_get_transition_duration();

            nlohmann::json result;
            result["name"] = name ? name : "";
            result["duration"] = actual_duration;

            obs_source_release(current);
            res->set_result(result);
        });
    }

    void obs_frontend::_transition_studio(
        std::shared_ptr<streamdock::jsonrpc::request>,
        std::shared_ptr<streamdock::jsonrpc::response> res)
    {
        execute_on_ui_thread([res]() {
            // 触发工作室模式转场
            if (obs_frontend_preview_program_mode_active()) {
                obs_frontend_preview_program_trigger_transition();
            }

            nlohmann::json result;
            result["success"] = true;
            res->set_result(result);
        });
    }

    // === 章节标记 ===

    void obs_frontend::_recording_add_chapter(
        std::shared_ptr<streamdock::jsonrpc::request> req,
        std::shared_ptr<streamdock::jsonrpc::response> res)
    {
        nlohmann::json params;
        std::string chapter_name;

        if (req->get_params(params)) {
            if (params.contains("name") && params["name"].is_string()) {
                chapter_name = params["name"].get<std::string>();
            }
        }

        execute_on_ui_thread([res, chapter_name]() {
            // 直接调用 OBS Frontend API 添加章节标记
            // 如果 chapter_name 为空，OBS 会自动生成名称
            bool success = obs_frontend_recording_add_chapter(
                chapter_name.empty() ? nullptr : chapter_name.c_str());

            nlohmann::json result;
            result["success"] = success;
            res->set_result(result);
        });
    }

    void obs_frontend::_recording_split_file(
        std::shared_ptr<streamdock::jsonrpc::request>,
        std::shared_ptr<streamdock::jsonrpc::response> res)
    {
        execute_on_ui_thread([res]() {
            // 直接调用 OBS Frontend API 分割录制文件
            bool success = obs_frontend_recording_split_file();

            nlohmann::json result;
            result["success"] = success;
            res->set_result(result);
        });
    }

} // namespace handlers
} // namespace streamdock
