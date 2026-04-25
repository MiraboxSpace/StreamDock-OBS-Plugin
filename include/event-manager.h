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
#include <set>
#include <string>
#include <functional>
#include <unordered_map>
#include <map>
#include <nlohmann/json.hpp>
#include <obs.h>
#include <obs-frontend-api.h>

// 前向声明
class WebSocketServer;

namespace streamdock {
namespace events {

    // 连接ID类型（避免直接依赖websocketpp）
    using connection_id = uint64_t;

    // 事件类型定义
    enum class EventType {
        // 流事件
        StreamingStarting,
        StreamingStarted,
        StreamingStopping,
        StreamingStopped,

        // 录制事件
        RecordingStarting,
        RecordingStarted,
        RecordingStopping,
        RecordingStopped,
        RecordingPaused,
        RecordingResumed,

        // 场景事件
        SceneChanged,
        SceneListChanged,
        SceneTransitionStarted,
        SceneTransitionEnded,

        // 源事件
        SourceCreated,
        SourceDestroyed,
        SourceVolumeChanged,
        SourceMuteStateChanged,
        SourceAdded,
        SourceRemoved,

        // 转场事件
        TransitionChanged,
        TransitionDurationChanged,

        // 虚拟摄像机事件
        VirtualCamStarted,
        VirtualCamStopped,

        // 回放缓存事件
        ReplayBufferStarted,
        ReplayBufferStopped,
        ReplayBufferSaved,

        // 其他事件
        ExitStarted,
        StudioModeEnabled,
        StudioModeDisabled,
        ProfileChanged,
        SceneCollectionChanged,

        // 回放缓存额外事件
        ReplayBufferStarting,
        ReplayBufferStopping,

        // 转场额外事件
        TransitionListChanged,

        // 场景集合额外事件
        SceneCollectionListChanged,
        SceneCollectionRenamed,

        // 配置文件额外事件
        ProfileListChanged,
        ProfileRenamed,

        // OBS系统事件
        ObsLoaded,
        ScenePreviewChanged,
        ScreenshotTaken,

        // 源信号事件
        SourceFilterEnabledChanged,
        SceneItemVisibilityChanged,
        SceneItemLockedChanged,
        SourceMediaStateChanged,
        SourceRenamed,

        // 自定义事件
        Custom
    };

    // 事件类型转字符串
    std::string event_type_to_string(EventType type);
    EventType string_to_event_type(const std::string& str);

    // 事件回调函数类型
    typedef std::function<void(const nlohmann::json&)> event_callback_t;

    // 事件订阅管理器
    class EventManager {
    public:
        static std::shared_ptr<EventManager> instance();

        EventManager();
        ~EventManager();

        // 初始化事件系统（注册OBS回调）
        void initialize(WebSocketServer* server);

        // 清理事件系统
        void shutdown();

        // 订阅事件
        bool subscribe(connection_id conn_id, EventType event_type);

        // 批量订阅
        bool subscribe(connection_id conn_id, const std::set<EventType>& event_types);

        // 取消订阅
        bool unsubscribe(connection_id conn_id, EventType event_type);

        // 取消所有订阅
        bool unsubscribe_all(connection_id conn_id);

        // 获取客户端的所有订阅
        std::set<EventType> get_subscriptions(connection_id conn_id);

        // 触发事件
        void emit_event(EventType event_type, const nlohmann::json& data);

        // 获取所有可用的订阅类型
        static std::vector<std::string> get_available_events();

        // OBS事件回调处理函数（需要公开给静态包装函数调用）
        void on_frontend_event(enum obs_frontend_event event, void *private_data);

        // 全局源信号回调方法（公开供静态回调函数调用）
        void on_source_create(obs_source_t *source);
        void on_source_destroy(obs_source_t *source);

        // 每源信号回调方法
        void on_source_volume_changed(obs_source_t *source, float volume);
        void on_source_mute_changed(obs_source_t *source, bool muted);
        void on_source_renamed(obs_source_t *source, const char *new_name, const char *prev_name);
        void on_source_filter_add(obs_source_t *source, obs_source_t *filter);
        void on_source_filter_remove(obs_source_t *source, obs_source_t *filter);
        void on_source_filter_enabled(obs_source_t *filter, bool enabled);
        void on_scene_item_add(obs_scene_t *scene, obs_sceneitem_t *item);
        void on_scene_item_remove(obs_scene_t *scene, obs_sceneitem_t *item);
        void on_scene_item_visible(obs_scene_t *scene, obs_sceneitem_t *item, bool visible);
        void on_scene_item_locked(obs_scene_t *scene, obs_sceneitem_t *item, bool locked);
        void on_source_media_state(obs_source_t *source, const std::string &state);

        // 信号注册/注销（供静态包装函数调用，须为 public）
        void register_source_signals(obs_source_t *source);
        void unregister_source_signals(obs_source_t *source);

    private:
        // 注册OBS事件回调
        void register_obs_callbacks();
        void unregister_obs_callbacks();

        // 具体事件处理
        void handle_streaming_starting();
        void handle_streaming_started();
        void handle_streaming_stopping();
        void handle_streaming_stopped();

        void handle_recording_starting();
        void handle_recording_started();
        void handle_recording_stopping();
        void handle_recording_stopped();
        void handle_recording_paused();
        void handle_recording_resumed();

        void handle_scene_changed();
        void handle_scene_list_changed();
        void handle_transition_begin();
        void handle_transition_end();

        void handle_source_created(obs_source_t* source);
        void handle_source_destroyed(obs_source_t* source);
        void handle_source_volume_changed(obs_source_t* source, float volume);
        void handle_source_mute_changed(obs_source_t* source);

        void handle_virtualcam_started();
        void handle_virtualcam_stopped();

        void handle_replay_buffer_started();
        void handle_replay_buffer_stopped();
        void handle_replay_buffer_saved();

        void handle_exit_started();
        void handle_studiomode_enabled();
        void handle_studiomode_disabled();
        void handle_profile_changed();
        void handle_scene_collection_changed();

        // 新增前端事件处理
        void handle_replay_buffer_starting();
        void handle_replay_buffer_stopping();
        void handle_transition_changed();
        void handle_transition_list_changed();
        void handle_transition_duration_changed();
        void handle_scenecollection_list_changed();
        void handle_scenecollection_renamed();
        void handle_profile_list_changed();
        void handle_profile_renamed();
        void handle_obs_loaded();
        void handle_preview_scene_changed();
        void handle_screenshot_taken();

        // 源信号事件处理
        void handle_source_added(obs_scene_t *scene, obs_sceneitem_t *item);
        void handle_source_removed(obs_scene_t *scene, obs_sceneitem_t *item);
        void handle_source_filter_enabled_impl(obs_source_t *source, obs_source_t *filter, bool enabled);
        void handle_scene_item_visibility(obs_scene_t *scene, obs_sceneitem_t *item, bool visible);
        void handle_scene_item_locked(obs_scene_t *scene, obs_sceneitem_t *item, bool locked);
        void handle_source_media_state_changed(obs_source_t *source, const std::string &state);
        void handle_source_renamed_impl(obs_source_t *source, const char *new_name, const char *prev_name);

        // 信号注册/注销辅助
        void register_existing_sources();

        // 辅助函数：获取场景信息
        nlohmann::json get_scene_info(obs_source_t *scene);

        // 辅助函数：获取源信息
        nlohmann::json get_source_info(obs_source_t *source, float override_volume = -1.0f);

    private:
        std::mutex m_mutex;
        WebSocketServer* m_server;

        // 订阅映射：connection_id -> 订阅的事件类型集合
        std::unordered_map<connection_id, std::set<EventType>> m_subscriptions;

        // 反向映射：事件类型 -> 订阅该事件的连接集合
        std::map<EventType, std::set<connection_id>> m_event_subscribers;

        // 已注册信号的源集合（避免重复注册）
        std::set<obs_source_t *> m_tracked_sources;

        // 滤镜 → 父源 映射（用于filter.enabledchanged事件）
        std::unordered_map<obs_source_t *, obs_source_t *> m_filter_parent_map;

        // 群组源 → 群组内部场景 映射（用于群组内嵌项的事件监听）
        std::unordered_map<obs_source_t *, obs_scene_t *> m_tracked_group_scenes;

        bool m_initialized;
    };

    // 辅助函数：创建事件通知JSON
    nlohmann::json create_event_notification(EventType event_type, const nlohmann::json& data);

} // namespace events
} // namespace streamdock
