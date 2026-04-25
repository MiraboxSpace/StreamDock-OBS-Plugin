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

#include "handlers/handler-obs-source.h"
#include "websocket-server.h"
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <obs-source.h>
#include <obs.h>
#include <plugin-support.h>

namespace streamdock {
namespace handlers {

    std::shared_ptr<obs_source> obs_source::instance()
    {
        static std::weak_ptr<obs_source> _instance;
        static std::mutex _lock;

        std::unique_lock<std::mutex> lock(_lock);
        if (_instance.expired()) {
            auto instance = std::make_shared<obs_source>();
            _instance = instance;
            return instance;
        }

        return _instance.lock();
    }

    obs_source::obs_source()
    {
        // Handlers 将在 register_handlers 中注册
    }

    void obs_source::register_handlers(WebSocketServer* server)
    {
        if (!server) {
            return;
        }

        using namespace std::placeholders;

        // === 音频控制 ===
        server->handle_sync("source.volume.get",
            std::bind(&obs_source::_volume_get, this,
                      std::placeholders::_1, std::placeholders::_2));
        server->handle_sync("source.volume.set",
            std::bind(&obs_source::_volume_set, this,
                      std::placeholders::_1, std::placeholders::_2));
        server->handle_sync("source.mute.get",
            std::bind(&obs_source::_mute_get, this,
                      std::placeholders::_1, std::placeholders::_2));
        server->handle_sync("source.mute.set",
            std::bind(&obs_source::_mute_set, this,
                      std::placeholders::_1, std::placeholders::_2));

        // === 媒体控制 ===
        server->handle_sync("source.media.play",
            std::bind(&obs_source::_media_play, this,
                      std::placeholders::_1, std::placeholders::_2));
        server->handle_sync("source.media.pause",
            std::bind(&obs_source::_media_pause, this,
                      std::placeholders::_1, std::placeholders::_2));
        server->handle_sync("source.media.restart",
            std::bind(&obs_source::_media_restart, this,
                      std::placeholders::_1, std::placeholders::_2));
        server->handle_sync("source.media.stop",
            std::bind(&obs_source::_media_stop, this,
                      std::placeholders::_1, std::placeholders::_2));
        server->handle_sync("source.media.next",
            std::bind(&obs_source::_media_next, this,
                      std::placeholders::_1, std::placeholders::_2));
        server->handle_sync("source.media.previous",
            std::bind(&obs_source::_media_previous, this,
                      std::placeholders::_1, std::placeholders::_2));
        server->handle_sync("source.media.getstate",
            std::bind(&obs_source::_media_get_state, this,
                      std::placeholders::_1, std::placeholders::_2));

        // === 滤镜控制 ===
        server->handle_sync("source.filter.list",
            std::bind(&obs_source::_filter_list, this,
                      std::placeholders::_1, std::placeholders::_2));
        server->handle_sync("source.filter.setenabled",
            std::bind(&obs_source::_filter_set_enabled, this,
                      std::placeholders::_1, std::placeholders::_2));
        server->handle_sync("source.filter.getenabled",
            std::bind(&obs_source::_filter_get_enabled, this,
                      std::placeholders::_1, std::placeholders::_2));

        // === 源列表 ===
        server->handle_sync("source.list",
            std::bind(&obs_source::_source_list, this,
                      std::placeholders::_1, std::placeholders::_2));
        server->handle_sync("source.volume.list",
            std::bind(&obs_source::_volume_list, this,
                      std::placeholders::_1, std::placeholders::_2));
        server->handle_sync("source.media.list",
            std::bind(&obs_source::_media_list, this,
                      std::placeholders::_1, std::placeholders::_2));
    }

    // === 音频控制 ===

    void obs_source::_volume_get(
        std::shared_ptr<streamdock::jsonrpc::request> req,
        std::shared_ptr<streamdock::jsonrpc::response> res)
    {
        nlohmann::json params;
        if (!req->get_params(params)) {
            throw streamdock::jsonrpc::invalid_params_error("Missing parameters");
        }

        if (!params.contains("source") || !params["source"].is_string()) {
            throw streamdock::jsonrpc::invalid_params_error("Missing or invalid 'source' parameter");
        }

        std::string source_name = params["source"].get<std::string>();
        obs_source_t* source = obs_get_source_by_name(source_name.c_str());

        if (!source) {
            res->set_error(static_cast<int64_t>(streamdock::jsonrpc::error_codes::INVALID_PARAMS),
                          "Source not found");
            return;
        }

        float volume = obs_source_get_volume(source);

        nlohmann::json result;
        result["volume"] = volume;
        result["db"] = obs_mul_to_db(volume);
        res->set_result(result);

        obs_source_release(source);
    }

    void obs_source::_volume_set(
        std::shared_ptr<streamdock::jsonrpc::request> req,
        std::shared_ptr<streamdock::jsonrpc::response> res)
    {
        nlohmann::json params;
        if (!req->get_params(params)) {
            throw streamdock::jsonrpc::invalid_params_error("Missing parameters");
        }

        if (!params.contains("source") || !params["source"].is_string()) {
            throw streamdock::jsonrpc::invalid_params_error("Missing or invalid 'source' parameter");
        }
        if (!params.contains("volume") && !params.contains("db")) {
            throw streamdock::jsonrpc::invalid_params_error("Missing 'volume' or 'db' parameter");
        }

        std::string source_name = params["source"].get<std::string>();
        obs_source_t* source = obs_get_source_by_name(source_name.c_str());

        if (!source) {
            res->set_error(static_cast<int64_t>(streamdock::jsonrpc::error_codes::INVALID_PARAMS),
                          "Source not found");
            return;
        }

        float volume = 1.0f;

        // 优先使用 db 参数
        if (params.contains("db") && params["db"].is_number()) {
            double db = params["db"].get<double>();
            volume = obs_db_to_mul(static_cast<float>(db));
        } else if (params.contains("volume") && params["volume"].is_number()) {
            volume = params["volume"].get<float>();
        }

        obs_source_set_volume(source, volume);

        // 返回当前状态
        float new_volume = obs_source_get_volume(source);

        nlohmann::json result;
        result["volume"] = new_volume;
        result["db"] = obs_mul_to_db(new_volume);
        res->set_result(result);

        obs_source_release(source);
    }

    void obs_source::_mute_get(
        std::shared_ptr<streamdock::jsonrpc::request> req,
        std::shared_ptr<streamdock::jsonrpc::response> res)
    {
        nlohmann::json params;
        if (!req->get_params(params)) {
            throw streamdock::jsonrpc::invalid_params_error("Missing parameters");
        }

        if (!params.contains("source") || !params["source"].is_string()) {
            throw streamdock::jsonrpc::invalid_params_error("Missing or invalid 'source' parameter");
        }

        std::string source_name = params["source"].get<std::string>();
        obs_source_t* source = obs_get_source_by_name(source_name.c_str());

        if (!source) {
            res->set_error(static_cast<int64_t>(streamdock::jsonrpc::error_codes::INVALID_PARAMS),
                          "Source not found");
            return;
        }

        bool muted = obs_source_muted(source);

        nlohmann::json result;
        result["muted"] = muted;
        res->set_result(result);

        obs_source_release(source);
    }

    void obs_source::_mute_set(
        std::shared_ptr<streamdock::jsonrpc::request> req,
        std::shared_ptr<streamdock::jsonrpc::response> res)
    {
        nlohmann::json params;
        if (!req->get_params(params)) {
            throw streamdock::jsonrpc::invalid_params_error("Missing parameters");
        }

        if (!params.contains("source") || !params["source"].is_string()) {
            throw streamdock::jsonrpc::invalid_params_error("Missing or invalid 'source' parameter");
        }
        if (!params.contains("muted") || !params["muted"].is_boolean()) {
            throw streamdock::jsonrpc::invalid_params_error("Missing or invalid 'muted' parameter");
        }

        std::string source_name = params["source"].get<std::string>();
        bool muted = params["muted"].get<bool>();

        obs_source_t* source = obs_get_source_by_name(source_name.c_str());

        if (!source) {
            res->set_error(static_cast<int64_t>(streamdock::jsonrpc::error_codes::INVALID_PARAMS),
                          "Source not found");
            return;
        }

        obs_source_set_muted(source, muted);

        nlohmann::json result;
        result["muted"] = obs_source_muted(source);
        res->set_result(result);

        obs_source_release(source);
    }

    // === 媒体控制 ===

    void obs_source::_media_play(
        std::shared_ptr<streamdock::jsonrpc::request> req,
        std::shared_ptr<streamdock::jsonrpc::response> res)
    {
        nlohmann::json params;
        if (!req->get_params(params)) {
            throw streamdock::jsonrpc::invalid_params_error("Missing parameters");
        }

        if (!params.contains("source") || !params["source"].is_string()) {
            throw streamdock::jsonrpc::invalid_params_error("Missing or invalid 'source' parameter");
        }

        std::string source_name = params["source"].get<std::string>();
        obs_source_t* source = obs_get_source_by_name(source_name.c_str());

        if (!source) {
            res->set_error(static_cast<int64_t>(streamdock::jsonrpc::error_codes::INVALID_PARAMS),
                          "Source not found");
            return;
        }

        obs_source_media_play_pause(source, false);

        nlohmann::json result;
        result["success"] = true;
        res->set_result(result);

        obs_source_release(source);
    }

    void obs_source::_media_pause(
        std::shared_ptr<streamdock::jsonrpc::request> req,
        std::shared_ptr<streamdock::jsonrpc::response> res)
    {
        nlohmann::json params;
        if (!req->get_params(params)) {
            throw streamdock::jsonrpc::invalid_params_error("Missing parameters");
        }

        if (!params.contains("source") || !params["source"].is_string()) {
            throw streamdock::jsonrpc::invalid_params_error("Missing or invalid 'source' parameter");
        }

        std::string source_name = params["source"].get<std::string>();
        obs_source_t* source = obs_get_source_by_name(source_name.c_str());

        if (!source) {
            res->set_error(static_cast<int64_t>(streamdock::jsonrpc::error_codes::INVALID_PARAMS),
                          "Source not found");
            return;
        }

        obs_source_media_play_pause(source, true);

        nlohmann::json result;
        result["success"] = true;
        res->set_result(result);

        obs_source_release(source);
    }

    void obs_source::_media_restart(
        std::shared_ptr<streamdock::jsonrpc::request> req,
        std::shared_ptr<streamdock::jsonrpc::response> res)
    {
        nlohmann::json params;
        if (!req->get_params(params)) {
            throw streamdock::jsonrpc::invalid_params_error("Missing parameters");
        }

        if (!params.contains("source") || !params["source"].is_string()) {
            throw streamdock::jsonrpc::invalid_params_error("Missing or invalid 'source' parameter");
        }

        std::string source_name = params["source"].get<std::string>();
        obs_source_t* source = obs_get_source_by_name(source_name.c_str());

        if (!source) {
            res->set_error(static_cast<int64_t>(streamdock::jsonrpc::error_codes::INVALID_PARAMS),
                          "Source not found");
            return;
        }

        obs_source_media_restart(source);

        nlohmann::json result;
        result["success"] = true;
        res->set_result(result);

        obs_source_release(source);
    }

    void obs_source::_media_stop(
        std::shared_ptr<streamdock::jsonrpc::request> req,
        std::shared_ptr<streamdock::jsonrpc::response> res)
    {
        nlohmann::json params;
        if (!req->get_params(params)) {
            throw streamdock::jsonrpc::invalid_params_error("Missing parameters");
        }

        if (!params.contains("source") || !params["source"].is_string()) {
            throw streamdock::jsonrpc::invalid_params_error("Missing or invalid 'source' parameter");
        }

        std::string source_name = params["source"].get<std::string>();
        obs_source_t* source = obs_get_source_by_name(source_name.c_str());

        if (!source) {
            res->set_error(static_cast<int64_t>(streamdock::jsonrpc::error_codes::INVALID_PARAMS),
                          "Source not found");
            return;
        }

        obs_source_media_stop(source);

        nlohmann::json result;
        result["success"] = true;
        res->set_result(result);

        obs_source_release(source);
    }

    void obs_source::_media_next(
        std::shared_ptr<streamdock::jsonrpc::request> req,
        std::shared_ptr<streamdock::jsonrpc::response> res)
    {
        nlohmann::json params;
        if (!req->get_params(params)) {
            throw streamdock::jsonrpc::invalid_params_error("Missing parameters");
        }

        if (!params.contains("source") || !params["source"].is_string()) {
            throw streamdock::jsonrpc::invalid_params_error("Missing or invalid 'source' parameter");
        }

        std::string source_name = params["source"].get<std::string>();
        obs_source_t* source = obs_get_source_by_name(source_name.c_str());

        if (!source) {
            res->set_error(static_cast<int64_t>(streamdock::jsonrpc::error_codes::INVALID_PARAMS),
                          "Source not found");
            return;
        }

        obs_source_media_next(source);

        nlohmann::json result;
        result["success"] = true;
        res->set_result(result);

        obs_source_release(source);
    }

    void obs_source::_media_previous(
        std::shared_ptr<streamdock::jsonrpc::request> req,
        std::shared_ptr<streamdock::jsonrpc::response> res)
    {
        nlohmann::json params;
        if (!req->get_params(params)) {
            throw streamdock::jsonrpc::invalid_params_error("Missing parameters");
        }

        if (!params.contains("source") || !params["source"].is_string()) {
            throw streamdock::jsonrpc::invalid_params_error("Missing or invalid 'source' parameter");
        }

        std::string source_name = params["source"].get<std::string>();
        obs_source_t* source = obs_get_source_by_name(source_name.c_str());

        if (!source) {
            res->set_error(static_cast<int64_t>(streamdock::jsonrpc::error_codes::INVALID_PARAMS),
                          "Source not found");
            return;
        }

        obs_source_media_previous(source);

        nlohmann::json result;
        result["success"] = true;
        res->set_result(result);

        obs_source_release(source);
    }

    void obs_source::_media_get_state(
        std::shared_ptr<streamdock::jsonrpc::request> req,
        std::shared_ptr<streamdock::jsonrpc::response> res)
    {
        nlohmann::json params;
        if (!req->get_params(params)) {
            throw streamdock::jsonrpc::invalid_params_error("Missing parameters");
        }

        if (!params.contains("source") || !params["source"].is_string()) {
            throw streamdock::jsonrpc::invalid_params_error("Missing or invalid 'source' parameter");
        }

        std::string source_name = params["source"].get<std::string>();
        obs_source_t* source = obs_get_source_by_name(source_name.c_str());

        if (!source) {
            res->set_error(static_cast<int64_t>(streamdock::jsonrpc::error_codes::INVALID_PARAMS),
                          "Source not found");
            return;
        }

        enum obs_media_state state = obs_source_media_get_state(source);

        const char* state_str = "unknown";
        switch (state) {
            case OBS_MEDIA_STATE_STOPPED:
                state_str = "stopped";
                break;
            case OBS_MEDIA_STATE_PLAYING:
                state_str = "playing";
                break;
            case OBS_MEDIA_STATE_PAUSED:
                state_str = "paused";
                break;
            case OBS_MEDIA_STATE_OPENING:
                state_str = "opening";
                break;
            case OBS_MEDIA_STATE_ENDED:
                state_str = "ended";
                break;
            case OBS_MEDIA_STATE_ERROR:
                state_str = "error";
                break;
            default:
                state_str = "unknown";
                break;
        }

        int64_t duration = obs_source_media_get_duration(source);
        int64_t time = obs_source_media_get_time(source);

        nlohmann::json result;
        result["state"] = state_str;
        result["duration_ms"] = duration;
        result["time_ms"] = time;
        res->set_result(result);

        obs_source_release(source);
    }

    // === 滤镜控制 ===

    void obs_source::_filter_list(
        std::shared_ptr<streamdock::jsonrpc::request> req,
        std::shared_ptr<streamdock::jsonrpc::response> res)
    {
        nlohmann::json params;
        if (!req->get_params(params)) {
            throw streamdock::jsonrpc::invalid_params_error("Missing parameters");
        }

        if (!params.contains("source") || !params["source"].is_string()) {
            throw streamdock::jsonrpc::invalid_params_error("Missing or invalid 'source' parameter");
        }

        std::string source_name = params["source"].get<std::string>();
        obs_source_t* source = obs_get_source_by_name(source_name.c_str());

        if (!source) {
            res->set_error(static_cast<int64_t>(streamdock::jsonrpc::error_codes::INVALID_PARAMS),
                          "Source not found");
            return;
        }

        nlohmann::json filters = nlohmann::json::array();

        // 使用无捕获的 lambda 作为回调
        // obs_source_enum_proc_t 的签名是 void(*)(obs_source_t*, obs_source_t*, void*)
        auto enum_callback = [](obs_source_t*, obs_source_t* filter, void* param) {
            nlohmann::json* filters = static_cast<nlohmann::json*>(param);
            const char* name = obs_source_get_name(filter);
            const char* id = obs_source_get_id(filter);
            bool enabled = obs_source_enabled(filter);
            uint32_t output_flags = obs_source_get_output_flags(filter);

            nlohmann::json filter_info;
            filter_info["name"] = name ? name : "";
            filter_info["id"] = id ? id : "";
            filter_info["enabled"] = enabled;
            filter_info["type"] = "filter";

            // 添加滤镜类型标识
            bool is_audio = (output_flags & OBS_SOURCE_AUDIO) != 0;
            bool is_video = (output_flags & OBS_SOURCE_VIDEO) != 0;

            if (is_audio && !is_video) {
                filter_info["filterType"] = "audio";
            } else if (is_video && !is_audio) {
                filter_info["filterType"] = "video";
            } else if (is_audio && is_video) {
                filter_info["filterType"] = "audio_video";
            } else {
                filter_info["filterType"] = "unknown";
            }

            filters->push_back(filter_info);
        };

        obs_source_enum_filters(source, enum_callback, &filters);

        nlohmann::json result;
        result["filters"] = filters;
        res->set_result(result);

        obs_source_release(source);
    }

    void obs_source::_filter_set_enabled(
        std::shared_ptr<streamdock::jsonrpc::request> req,
        std::shared_ptr<streamdock::jsonrpc::response> res)
    {
        nlohmann::json params;
        if (!req->get_params(params)) {
            throw streamdock::jsonrpc::invalid_params_error("Missing parameters");
        }

        if (!params.contains("source") || !params["source"].is_string()) {
            throw streamdock::jsonrpc::invalid_params_error("Missing or invalid 'source' parameter");
        }
        if (!params.contains("filter") || !params["filter"].is_string()) {
            throw streamdock::jsonrpc::invalid_params_error("Missing or invalid 'filter' parameter");
        }
        if (!params.contains("enabled") || !params["enabled"].is_boolean()) {
            throw streamdock::jsonrpc::invalid_params_error("Missing or invalid 'enabled' parameter");
        }

        std::string source_name = params["source"].get<std::string>();
        std::string filter_name = params["filter"].get<std::string>();
        bool enabled = params["enabled"].get<bool>();

        obs_source_t* source = obs_get_source_by_name(source_name.c_str());

        if (!source) {
            res->set_error(static_cast<int64_t>(streamdock::jsonrpc::error_codes::INVALID_PARAMS),
                          "Source not found");
            return;
        }

        obs_source_t* filter = obs_source_get_filter_by_name(source, filter_name.c_str());

        if (!filter) {
            res->set_error(static_cast<int64_t>(streamdock::jsonrpc::error_codes::INVALID_PARAMS),
                          "Filter not found");
            obs_source_release(source);
            return;
        }

        obs_source_set_enabled(filter, enabled);

        nlohmann::json result;
        result["success"] = true;
        result["enabled"] = obs_source_enabled(filter);
        res->set_result(result);

        obs_source_release(filter);
        obs_source_release(source);
    }

    void obs_source::_filter_get_enabled(
        std::shared_ptr<streamdock::jsonrpc::request> req,
        std::shared_ptr<streamdock::jsonrpc::response> res)
    {
        nlohmann::json params;
        if (!req->get_params(params)) {
            throw streamdock::jsonrpc::invalid_params_error("Missing parameters");
        }

        if (!params.contains("source") || !params["source"].is_string()) {
            throw streamdock::jsonrpc::invalid_params_error("Missing or invalid 'source' parameter");
        }
        if (!params.contains("filter") || !params["filter"].is_string()) {
            throw streamdock::jsonrpc::invalid_params_error("Missing or invalid 'filter' parameter");
        }

        std::string source_name = params["source"].get<std::string>();
        std::string filter_name = params["filter"].get<std::string>();

        obs_source_t* source = obs_get_source_by_name(source_name.c_str());

        if (!source) {
            res->set_error(static_cast<int64_t>(streamdock::jsonrpc::error_codes::INVALID_PARAMS),
                          "Source not found");
            return;
        }

        obs_source_t* filter = obs_source_get_filter_by_name(source, filter_name.c_str());

        if (!filter) {
            res->set_error(static_cast<int64_t>(streamdock::jsonrpc::error_codes::INVALID_PARAMS),
                          "Filter not found");
            obs_source_release(source);
            return;
        }

        bool enabled = obs_source_enabled(filter);

        nlohmann::json result;
        result["enabled"] = enabled;
        res->set_result(result);

        obs_source_release(filter);
        obs_source_release(source);
    }

    // === 源列表 ===

    void obs_source::_source_list(
        std::shared_ptr<streamdock::jsonrpc::request>,
        std::shared_ptr<streamdock::jsonrpc::response> res)
    {
        execute_on_ui_thread([res]() {
            nlohmann::json sources = nlohmann::json::array();

            // 使用 obs_enum_sources 回调枚举所有输入源
            auto enum_callback = [](void* param, obs_source_t* source) {
                nlohmann::json* sources = static_cast<nlohmann::json*>(param);

                // 获取源信息
                const char* name = obs_source_get_name(source);
                const char* id = obs_source_get_id(source);
                enum obs_source_type type = obs_source_get_type(source);
                uint32_t output_flags = obs_source_get_output_flags(source);

                // 仅包含 INPUT 类型的源
                // SCENE 源由 scene.list 处理
                // TRANSITION 源由 transition.list 处理
                // FILTER 源是特定于父源的，由 source.filter.list 处理
                if (type == OBS_SOURCE_TYPE_INPUT) {
                    nlohmann::json source_info;
                    source_info["name"] = name ? name : "";
                    source_info["typeId"] = id ? id : "";  // 类型标识符 (如 "ffmpeg_source", "pulse_input_capture")

                    // 添加能力标志
                    nlohmann::json capabilities = nlohmann::json::object();
                    capabilities["video"] = (output_flags & OBS_SOURCE_VIDEO) != 0;
                    capabilities["audio"] = (output_flags & OBS_SOURCE_AUDIO) != 0;
                    capabilities["async"] = (output_flags & OBS_SOURCE_ASYNC) != 0;
                    capabilities["controllable"] = (output_flags & OBS_SOURCE_CONTROLLABLE_MEDIA) != 0;
                    source_info["capabilities"] = capabilities;

                    sources->push_back(source_info);
                }

                return true;  // 继续枚举
            };

            obs_enum_sources(enum_callback, &sources);

            nlohmann::json result;
            result["sources"] = sources;
            res->set_result(result);
        });
    }

    void obs_source::_volume_list(
        std::shared_ptr<streamdock::jsonrpc::request>,
        std::shared_ptr<streamdock::jsonrpc::response> res)
    {
        execute_on_ui_thread([res]() {
            nlohmann::json sources = nlohmann::json::array();

            // 使用 obs_enum_sources 回调枚举所有输入源
            auto enum_callback = [](void* param, obs_source_t* source) {
                nlohmann::json* sources = static_cast<nlohmann::json*>(param);

                // 获取源信息
                const char* name = obs_source_get_name(source);
                const char* id = obs_source_get_id(source);
                enum obs_source_type type = obs_source_get_type(source);
                uint32_t output_flags = obs_source_get_output_flags(source);

                // 仅包含 INPUT 类型且有音频输出的源
                if (type == OBS_SOURCE_TYPE_INPUT && (output_flags & OBS_SOURCE_AUDIO) != 0) {
                    nlohmann::json source_info;
                    source_info["name"] = name ? name : "";
                    source_info["typeId"] = id ? id : "";

                    // 获取音量信息
                    float volume = obs_source_get_volume(source);
                    bool muted = obs_source_muted(source);

                    source_info["volume"] = volume;
                    source_info["db"] = obs_mul_to_db(volume);
                    source_info["muted"] = muted;

                    sources->push_back(source_info);
                }

                return true;  // 继续枚举
            };

            obs_enum_sources(enum_callback, &sources);

            nlohmann::json result;
            result["sources"] = sources;
            res->set_result(result);
        });
    }

    void obs_source::_media_list(
        std::shared_ptr<streamdock::jsonrpc::request>,
        std::shared_ptr<streamdock::jsonrpc::response> res)
    {
        execute_on_ui_thread([res]() {
            nlohmann::json sources = nlohmann::json::array();

            // 使用 obs_enum_sources 回调枚举所有输入源
            auto enum_callback = [](void* param, obs_source_t* source) {
                nlohmann::json* sources = static_cast<nlohmann::json*>(param);

                // 获取源信息
                const char* name = obs_source_get_name(source);
                const char* id = obs_source_get_id(source);
                enum obs_source_type type = obs_source_get_type(source);
                uint32_t output_flags = obs_source_get_output_flags(source);

                // 仅包含 INPUT 类型且有可控制媒体的源
                if (type == OBS_SOURCE_TYPE_INPUT && (output_flags & OBS_SOURCE_CONTROLLABLE_MEDIA) != 0) {
                    nlohmann::json source_info;
                    source_info["name"] = name ? name : "";
                    source_info["typeId"] = id ? id : "";

                    // 获取媒体状态
                    enum obs_media_state state = obs_source_media_get_state(source);

                    const char* state_str = "unknown";
                    switch (state) {
                        case OBS_MEDIA_STATE_STOPPED:
                            state_str = "stopped";
                            break;
                        case OBS_MEDIA_STATE_PLAYING:
                            state_str = "playing";
                            break;
                        case OBS_MEDIA_STATE_PAUSED:
                            state_str = "paused";
                            break;
                        case OBS_MEDIA_STATE_OPENING:
                            state_str = "opening";
                            break;
                        case OBS_MEDIA_STATE_ENDED:
                            state_str = "ended";
                            break;
                        case OBS_MEDIA_STATE_ERROR:
                            state_str = "error";
                            break;
                        default:
                            state_str = "unknown";
                            break;
                    }

                    int64_t duration = obs_source_media_get_duration(source);
                    int64_t time = obs_source_media_get_time(source);

                    source_info["state"] = state_str;
                    source_info["duration_ms"] = duration;
                    source_info["time_ms"] = time;

                    sources->push_back(source_info);
                }

                return true;  // 继续枚举
            };

            obs_enum_sources(enum_callback, &sources);

            nlohmann::json result;
            result["sources"] = sources;
            res->set_result(result);
        });
    }

} // namespace handlers
} // namespace streamdock
