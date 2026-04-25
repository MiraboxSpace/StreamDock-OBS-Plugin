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

#include "event-manager.h"
#include "websocket-server.h"
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <plugin-support.h>

namespace streamdock {
namespace events {

    // 全局实例访问
    std::shared_ptr<EventManager> EventManager::instance()
    {
        static std::weak_ptr<EventManager> _instance;
        static std::mutex _lock;

        std::unique_lock<std::mutex> lock(_lock);
        if (_instance.expired()) {
            auto instance = std::make_shared<EventManager>();
            _instance = instance;
            return instance;
        }

        return _instance.lock();
    }

    // 事件类型名称映射
    static const std::unordered_map<EventType, std::string> event_type_names = {
        {EventType::StreamingStarting, "streaming.starting"},
        {EventType::StreamingStarted, "streaming.started"},
        {EventType::StreamingStopping, "streaming.stopping"},
        {EventType::StreamingStopped, "streaming.stopped"},

        {EventType::RecordingStarting, "recording.starting"},
        {EventType::RecordingStarted, "recording.started"},
        {EventType::RecordingStopping, "recording.stopping"},
        {EventType::RecordingStopped, "recording.stopped"},
        {EventType::RecordingPaused, "recording.paused"},
        {EventType::RecordingResumed, "recording.resumed"},

        {EventType::SceneChanged, "scene.changed"},
        {EventType::SceneListChanged, "scene.listchanged"},
        {EventType::SceneTransitionStarted, "scene.transitionstarted"},
        {EventType::SceneTransitionEnded, "scene.transitionended"},

        {EventType::SourceCreated, "source.created"},
        {EventType::SourceDestroyed, "source.destroyed"},
        {EventType::SourceVolumeChanged, "source.volumechanged"},
        {EventType::SourceMuteStateChanged, "source.mutestatechanged"},
        {EventType::SourceAdded, "source.added"},
        {EventType::SourceRemoved, "source.removed"},

        {EventType::TransitionChanged, "transition.changed"},
        {EventType::TransitionDurationChanged, "transition.durationchanged"},
        {EventType::TransitionListChanged, "transition.listchanged"},

        {EventType::VirtualCamStarted, "virtualcam.started"},
        {EventType::VirtualCamStopped, "virtualcam.stopped"},

        {EventType::ReplayBufferStarting, "replaybuffer.starting"},
        {EventType::ReplayBufferStopping, "replaybuffer.stopping"},
        {EventType::ReplayBufferStarted, "replaybuffer.started"},
        {EventType::ReplayBufferStopped, "replaybuffer.stopped"},
        {EventType::ReplayBufferSaved, "replaybuffer.saved"},

        {EventType::ExitStarted, "exit.started"},
        {EventType::StudioModeEnabled, "studiomode.enabled"},
        {EventType::StudioModeDisabled, "studiomode.disabled"},
        {EventType::ProfileChanged, "profile.changed"},
        {EventType::ProfileListChanged, "profile.listchanged"},
        {EventType::ProfileRenamed, "profile.renamed"},
        {EventType::SceneCollectionChanged, "scenecollection.changed"},
        {EventType::SceneCollectionListChanged, "scenecollection.listchanged"},
        {EventType::SceneCollectionRenamed, "scenecollection.renamed"},

        {EventType::ObsLoaded, "obs.loaded"},
        {EventType::ScenePreviewChanged, "scene.previewchanged"},
        {EventType::ScreenshotTaken, "screenshot.taken"},

        {EventType::SourceFilterEnabledChanged, "source.filter.enabledchanged"},
        {EventType::SceneItemVisibilityChanged, "scene.item.visibilitychanged"},
        {EventType::SceneItemLockedChanged, "scene.item.lockedchanged"},
        {EventType::SourceMediaStateChanged, "source.media.statechanged"},
        {EventType::SourceRenamed, "source.renamed"},
    };

    std::string event_type_to_string(EventType type)
    {
        auto it = event_type_names.find(type);
        if (it != event_type_names.end()) {
            return it->second;
        }
        return "custom";
    }

    EventType string_to_event_type(const std::string& str)
    {
        for (const auto& pair : event_type_names) {
            if (pair.second == str) {
                return pair.first;
            }
        }
        return EventType::Custom;
    }

    EventManager::EventManager()
        : m_server(nullptr)
        , m_initialized(false)
    {
    }

    EventManager::~EventManager()
    {
        shutdown();
    }

    void EventManager::initialize(WebSocketServer* server)
    {
        if (m_initialized) {
            // obs_log(LOG_WARNING, "EventManager already initialized");
            return;
        }

        m_server = server;
        register_obs_callbacks();
        m_initialized = true;

        // obs_log(LOG_INFO, "EventManager initialized");
    }

    void EventManager::shutdown()
    {
        if (!m_initialized) {
            return;
        }

        unregister_obs_callbacks();

        std::lock_guard<std::mutex> lock(m_mutex);
        m_subscriptions.clear();
        m_event_subscribers.clear();
        m_initialized = false;

        // obs_log(LOG_INFO, "EventManager shutdown");
    }

    bool EventManager::subscribe(connection_id conn_id, EventType event_type)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        // 添加到客户端订阅
        m_subscriptions[conn_id].insert(event_type);

        // 添加到事件订阅者
        m_event_subscribers[event_type].insert(conn_id);

        // obs_log(LOG_INFO, "Client subscribed to event: %s",
        //         event_type_to_string(event_type).c_str());

        return true;
    }

    bool EventManager::subscribe(connection_id conn_id, const std::set<EventType>& event_types)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        for (const auto& event_type : event_types) {
            m_subscriptions[conn_id].insert(event_type);
            m_event_subscribers[event_type].insert(conn_id);
        }

        // obs_log(LOG_INFO, "Client subscribed to %zu events", event_types.size());
        return true;
    }

    bool EventManager::unsubscribe(connection_id conn_id, EventType event_type)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        // 从客户端订阅中移除
        auto sub_it = m_subscriptions.find(conn_id);
        if (sub_it != m_subscriptions.end()) {
            sub_it->second.erase(event_type);
            if (sub_it->second.empty()) {
                m_subscriptions.erase(sub_it);
            }
        }

        // 从事件订阅者中移除
        auto event_it = m_event_subscribers.find(event_type);
        if (event_it != m_event_subscribers.end()) {
            event_it->second.erase(conn_id);
            if (event_it->second.empty()) {
                m_event_subscribers.erase(event_it);
            }
        }

        // obs_log(LOG_INFO, "Client unsubscribed from event: %s",
        //         event_type_to_string(event_type).c_str());

        return true;
    }

    bool EventManager::unsubscribe_all(connection_id conn_id)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        auto sub_it = m_subscriptions.find(conn_id);
        if (sub_it == m_subscriptions.end()) {
            return false;
        }

        // 从所有事件订阅者中移除
        for (const auto& event_type : sub_it->second) {
            auto event_it = m_event_subscribers.find(event_type);
            if (event_it != m_event_subscribers.end()) {
                event_it->second.erase(conn_id);
                if (event_it->second.empty()) {
                    m_event_subscribers.erase(event_it);
                }
            }
        }

        m_subscriptions.erase(sub_it);

        // obs_log(LOG_INFO, "Client unsubscribed from all events");
        return true;
    }

    std::set<EventType> EventManager::get_subscriptions(connection_id conn_id)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        auto it = m_subscriptions.find(conn_id);
        if (it != m_subscriptions.end()) {
            return it->second;
        }

        return std::set<EventType>();
    }

    void EventManager::emit_event(EventType event_type, const nlohmann::json& data)
    {
        if (!m_server || !m_initialized) {
            return;
        }

        std::lock_guard<std::mutex> lock(m_mutex);

        auto it = m_event_subscribers.find(event_type);
        if (it == m_event_subscribers.end() || it->second.empty()) {
            // 没有订阅者，跳过
            return;
        }

        // 创建事件通知
        nlohmann::json notification = create_event_notification(event_type, data);
        std::string message = notification.dump();

        // 向所有订阅者发送
        std::vector<connection_id> failed_connections;
        for (const auto& conn_id : it->second) {
            try {
                m_server->send_to_connection(conn_id, message);
            } catch (const std::exception&) {
                // obs_log(LOG_WARNING, "Failed to send event to client: %s", e.what());
                failed_connections.push_back(conn_id);
            }
        }

        // 清理失败的连接
        for (const auto& conn_id : failed_connections) {
            unsubscribe_all(conn_id);
        }
    }

    std::vector<std::string> EventManager::get_available_events()
    {
        std::vector<std::string> events;
        for (const auto& pair : event_type_names) {
            events.push_back(pair.second);
        }
        return events;
    }

    // ---------------------------------------------------------------
    // 静态回调包装器：前端事件
    // ---------------------------------------------------------------

    static void frontend_event_wrapper(enum obs_frontend_event event, void *private_data)
    {
        EventManager *manager = static_cast<EventManager *>(private_data);
        if (manager) {
            manager->on_frontend_event(event, private_data);
        }
    }

    // ---------------------------------------------------------------
    // 静态回调包装器：全局源信号
    // ---------------------------------------------------------------

    static void global_source_create_cb(void *data, calldata_t *cd)
    {
        EventManager *mgr = static_cast<EventManager *>(data);
        obs_source_t *source = static_cast<obs_source_t *>(calldata_ptr(cd, "source"));
        if (mgr && source) {
            mgr->register_source_signals(source);
            mgr->on_source_create(source);
        }
    }

    static void global_source_destroy_cb(void *data, calldata_t *cd)
    {
        EventManager *mgr = static_cast<EventManager *>(data);
        obs_source_t *source = static_cast<obs_source_t *>(calldata_ptr(cd, "source"));
        if (mgr && source) {
            mgr->on_source_destroy(source);
            mgr->unregister_source_signals(source);
        }
    }

    // ---------------------------------------------------------------
    // 静态回调包装器：每源信号
    // ---------------------------------------------------------------

    static void source_volume_cb(void *data, calldata_t *cd)
    {
        EventManager *mgr = static_cast<EventManager *>(data);
        obs_source_t *source = static_cast<obs_source_t *>(calldata_ptr(cd, "source"));
        float volume = (float)calldata_float(cd, "volume");
        if (mgr && source)
            mgr->on_source_volume_changed(source, volume);
    }

    static void source_mute_cb(void *data, calldata_t *cd)
    {
        EventManager *mgr = static_cast<EventManager *>(data);
        obs_source_t *source = static_cast<obs_source_t *>(calldata_ptr(cd, "source"));
        bool muted = calldata_bool(cd, "muted");
        if (mgr && source)
            mgr->on_source_mute_changed(source, muted);
    }

    static void source_rename_cb(void *data, calldata_t *cd)
    {
        EventManager *mgr = static_cast<EventManager *>(data);
        obs_source_t *source = static_cast<obs_source_t *>(calldata_ptr(cd, "source"));
        const char *new_name = calldata_string(cd, "new_name");
        const char *prev_name = calldata_string(cd, "prev_name");
        if (mgr && source)
            mgr->on_source_renamed(source, new_name, prev_name);
    }

    static void source_filter_add_cb(void *data, calldata_t *cd)
    {
        EventManager *mgr = static_cast<EventManager *>(data);
        obs_source_t *source = static_cast<obs_source_t *>(calldata_ptr(cd, "source"));
        obs_source_t *filter = static_cast<obs_source_t *>(calldata_ptr(cd, "filter"));
        if (mgr && source && filter)
            mgr->on_source_filter_add(source, filter);
    }

    static void source_filter_remove_cb(void *data, calldata_t *cd)
    {
        EventManager *mgr = static_cast<EventManager *>(data);
        obs_source_t *source = static_cast<obs_source_t *>(calldata_ptr(cd, "source"));
        obs_source_t *filter = static_cast<obs_source_t *>(calldata_ptr(cd, "filter"));
        if (mgr && source && filter)
            mgr->on_source_filter_remove(source, filter);
    }

    static void source_filter_enable_cb(void *data, calldata_t *cd)
    {
        EventManager *mgr = static_cast<EventManager *>(data);
        obs_source_t *filter = static_cast<obs_source_t *>(calldata_ptr(cd, "source"));
        bool enabled = calldata_bool(cd, "enabled");
        if (mgr && filter)
            mgr->on_source_filter_enabled(filter, enabled);
    }

    static void source_media_play_cb(void *data, calldata_t *cd)
    {
        EventManager *mgr = static_cast<EventManager *>(data);
        obs_source_t *source = static_cast<obs_source_t *>(calldata_ptr(cd, "source"));
        if (mgr && source)
            mgr->on_source_media_state(source, "playing");
    }

    static void source_media_pause_cb(void *data, calldata_t *cd)
    {
        EventManager *mgr = static_cast<EventManager *>(data);
        obs_source_t *source = static_cast<obs_source_t *>(calldata_ptr(cd, "source"));
        if (mgr && source)
            mgr->on_source_media_state(source, "paused");
    }

    static void source_media_restart_cb(void *data, calldata_t *cd)
    {
        EventManager *mgr = static_cast<EventManager *>(data);
        obs_source_t *source = static_cast<obs_source_t *>(calldata_ptr(cd, "source"));
        if (mgr && source)
            mgr->on_source_media_state(source, "playing");
    }

    static void source_media_stopped_cb(void *data, calldata_t *cd)
    {
        EventManager *mgr = static_cast<EventManager *>(data);
        obs_source_t *source = static_cast<obs_source_t *>(calldata_ptr(cd, "source"));
        if (mgr && source)
            mgr->on_source_media_state(source, "stopped");
    }

    static void source_media_next_cb(void *data, calldata_t *cd)
    {
        EventManager *mgr = static_cast<EventManager *>(data);
        obs_source_t *source = static_cast<obs_source_t *>(calldata_ptr(cd, "source"));
        if (mgr && source)
            mgr->on_source_media_state(source, "playing");
    }

    static void source_media_prev_cb(void *data, calldata_t *cd)
    {
        EventManager *mgr = static_cast<EventManager *>(data);
        obs_source_t *source = static_cast<obs_source_t *>(calldata_ptr(cd, "source"));
        if (mgr && source)
            mgr->on_source_media_state(source, "playing");
    }

    static void source_media_started_cb(void *data, calldata_t *cd)
    {
        EventManager *mgr = static_cast<EventManager *>(data);
        obs_source_t *source = static_cast<obs_source_t *>(calldata_ptr(cd, "source"));
        if (mgr && source)
            mgr->on_source_media_state(source, "playing");
    }

    static void source_media_ended_cb(void *data, calldata_t *cd)
    {
        EventManager *mgr = static_cast<EventManager *>(data);
        obs_source_t *source = static_cast<obs_source_t *>(calldata_ptr(cd, "source"));
        if (mgr && source)
            mgr->on_source_media_state(source, "ended");
    }

    // ---------------------------------------------------------------
    // 静态回调包装器：场景 item 信号
    // ---------------------------------------------------------------

    static void scene_item_add_cb(void *data, calldata_t *cd)
    {
        EventManager *mgr = static_cast<EventManager *>(data);
        obs_scene_t *scene = static_cast<obs_scene_t *>(calldata_ptr(cd, "scene"));
        obs_sceneitem_t *item = static_cast<obs_sceneitem_t *>(calldata_ptr(cd, "item"));
        if (mgr && scene && item)
            mgr->on_scene_item_add(scene, item);
    }

    static void scene_item_remove_cb(void *data, calldata_t *cd)
    {
        EventManager *mgr = static_cast<EventManager *>(data);
        obs_scene_t *scene = static_cast<obs_scene_t *>(calldata_ptr(cd, "scene"));
        obs_sceneitem_t *item = static_cast<obs_sceneitem_t *>(calldata_ptr(cd, "item"));
        if (mgr && scene && item)
            mgr->on_scene_item_remove(scene, item);
    }

    static void scene_item_visible_cb(void *data, calldata_t *cd)
    {
        EventManager *mgr = static_cast<EventManager *>(data);
        obs_scene_t *scene = static_cast<obs_scene_t *>(calldata_ptr(cd, "scene"));
        obs_sceneitem_t *item = static_cast<obs_sceneitem_t *>(calldata_ptr(cd, "item"));
        bool visible = calldata_bool(cd, "visible");
        if (mgr && scene && item)
            mgr->on_scene_item_visible(scene, item, visible);
    }

    static void scene_item_locked_cb(void *data, calldata_t *cd)
    {
        EventManager *mgr = static_cast<EventManager *>(data);
        obs_scene_t *scene = static_cast<obs_scene_t *>(calldata_ptr(cd, "scene"));
        obs_sceneitem_t *item = static_cast<obs_sceneitem_t *>(calldata_ptr(cd, "item"));
        bool locked = calldata_bool(cd, "locked");
        if (mgr && scene && item)
            mgr->on_scene_item_locked(scene, item, locked);
    }

    // ---------------------------------------------------------------
    // 信号注册/注销
    // ---------------------------------------------------------------

    void EventManager::register_source_signals(obs_source_t *source)
    {
        if (!source)
            return;

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_tracked_sources.count(source))
                return; // 已注册
            m_tracked_sources.insert(source);
        }

        signal_handler_t *sh = obs_source_get_signal_handler(source);
        if (!sh)
            return;

        signal_handler_connect(sh, "volume", source_volume_cb, this);
        signal_handler_connect(sh, "mute", source_mute_cb, this);
        signal_handler_connect(sh, "rename", source_rename_cb, this);
        signal_handler_connect(sh, "filter_add", source_filter_add_cb, this);
        signal_handler_connect(sh, "filter_remove", source_filter_remove_cb, this);
        signal_handler_connect(sh, "media_play", source_media_play_cb, this);
        signal_handler_connect(sh, "media_pause", source_media_pause_cb, this);
        signal_handler_connect(sh, "media_restart", source_media_restart_cb, this);
        signal_handler_connect(sh, "media_stopped", source_media_stopped_cb, this);
        signal_handler_connect(sh, "media_next", source_media_next_cb, this);
        signal_handler_connect(sh, "media_previous", source_media_prev_cb, this);
        signal_handler_connect(sh, "media_started", source_media_started_cb, this);
        signal_handler_connect(sh, "media_ended", source_media_ended_cb, this);

        // 如果是场景类型，注册场景 item 信号
        obs_scene_t *scene = obs_scene_from_source(source);
        if (scene) {
            signal_handler_connect(sh, "item_add", scene_item_add_cb, this);
            signal_handler_connect(sh, "item_remove", scene_item_remove_cb, this);
            signal_handler_connect(sh, "item_visible", scene_item_visible_cb, this);
            signal_handler_connect(sh, "item_locked", scene_item_locked_cb, this);
        }

        // 如果是群组类型，也要注册群组内部场景的 item 信号
        obs_scene_t *group_scene = obs_group_from_source(source);
        if (group_scene) {
            obs_source_t *group_scene_source = obs_scene_get_source(group_scene);
            if (group_scene_source) {
                signal_handler_t *group_sh = obs_source_get_signal_handler(group_scene_source);
                if (group_sh) {
                    signal_handler_connect(group_sh, "item_add", scene_item_add_cb, this);
                    signal_handler_connect(group_sh, "item_remove", scene_item_remove_cb, this);
                    signal_handler_connect(group_sh, "item_visible", scene_item_visible_cb, this);
                    signal_handler_connect(group_sh, "item_locked", scene_item_locked_cb, this);
                }
            }

            // 跟踪群组场景以便后续清理
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_tracked_group_scenes[source] = group_scene;
            }
        }

        // 注册已有滤镜的 enable 信号
        obs_source_enum_filters(source, [](obs_source_t *, obs_source_t *filter, void *data) {
            EventManager *mgr = static_cast<EventManager *>(data);
            signal_handler_t *fsh = obs_source_get_signal_handler(filter);
            if (fsh)
                signal_handler_connect(fsh, "enable", source_filter_enable_cb, mgr);
        }, this);
    }

    void EventManager::unregister_source_signals(obs_source_t *source)
    {
        if (!source)
            return;

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_tracked_sources.erase(source);
            // 清理此源作为父源的滤镜映射
            for (auto it = m_filter_parent_map.begin(); it != m_filter_parent_map.end();) {
                if (it->second == source)
                    it = m_filter_parent_map.erase(it);
                else
                    ++it;
            }
        }

        signal_handler_t *sh = obs_source_get_signal_handler(source);
        if (!sh)
            return;

        signal_handler_disconnect(sh, "volume", source_volume_cb, this);
        signal_handler_disconnect(sh, "mute", source_mute_cb, this);
        signal_handler_disconnect(sh, "rename", source_rename_cb, this);
        signal_handler_disconnect(sh, "filter_add", source_filter_add_cb, this);
        signal_handler_disconnect(sh, "filter_remove", source_filter_remove_cb, this);
        signal_handler_disconnect(sh, "media_play", source_media_play_cb, this);
        signal_handler_disconnect(sh, "media_pause", source_media_pause_cb, this);
        signal_handler_disconnect(sh, "media_restart", source_media_restart_cb, this);
        signal_handler_disconnect(sh, "media_stopped", source_media_stopped_cb, this);
        signal_handler_disconnect(sh, "media_next", source_media_next_cb, this);
        signal_handler_disconnect(sh, "media_previous", source_media_prev_cb, this);
        signal_handler_disconnect(sh, "media_started", source_media_started_cb, this);
        signal_handler_disconnect(sh, "media_ended", source_media_ended_cb, this);

        obs_scene_t *scene = obs_scene_from_source(source);
        if (scene) {
            signal_handler_disconnect(sh, "item_add", scene_item_add_cb, this);
            signal_handler_disconnect(sh, "item_remove", scene_item_remove_cb, this);
            signal_handler_disconnect(sh, "item_visible", scene_item_visible_cb, this);
            signal_handler_disconnect(sh, "item_locked", scene_item_locked_cb, this);
        }

        // 注销群组内部场景的信号
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_tracked_group_scenes.find(source);
            if (it != m_tracked_group_scenes.end()) {
                obs_scene_t *group_scene = it->second;
                obs_source_t *group_scene_source = obs_scene_get_source(group_scene);
                if (group_scene_source) {
                    signal_handler_t *group_sh = obs_source_get_signal_handler(group_scene_source);
                    if (group_sh) {
                        signal_handler_disconnect(group_sh, "item_add", scene_item_add_cb, this);
                        signal_handler_disconnect(group_sh, "item_remove", scene_item_remove_cb, this);
                        signal_handler_disconnect(group_sh, "item_visible", scene_item_visible_cb, this);
                        signal_handler_disconnect(group_sh, "item_locked", scene_item_locked_cb, this);
                    }
                }
                m_tracked_group_scenes.erase(it);
            }
        }
    }

    void EventManager::register_existing_sources()
    {
        // 枚举所有输入源
        obs_enum_sources([](void *data, obs_source_t *source) -> bool {
            EventManager *mgr = static_cast<EventManager *>(data);
            mgr->register_source_signals(source);
            return true;
        }, this);

        // 枚举所有场景源
        struct obs_frontend_source_list scenes = {0};
        obs_frontend_get_scenes(&scenes);
        for (size_t i = 0; i < scenes.sources.num; i++) {
            register_source_signals(scenes.sources.array[i]);
        }
        obs_frontend_source_list_free(&scenes);
    }

    void EventManager::register_obs_callbacks()
    {
        // 注册前端事件回调
        obs_frontend_add_event_callback(frontend_event_wrapper, this);

        // 注册全局源信号
        signal_handler_t *global_sh = obs_get_signal_handler();
        if (global_sh) {
            signal_handler_connect(global_sh, "source_create", global_source_create_cb, this);
            signal_handler_connect(global_sh, "source_destroy", global_source_destroy_cb, this);
        }

        // 为已存在的源注册信号
        register_existing_sources();

        // obs_log(LOG_INFO, "OBS event callbacks registered");
    }

    void EventManager::unregister_obs_callbacks()
    {
        obs_frontend_remove_event_callback(frontend_event_wrapper, this);

        // 注销全局源信号
        signal_handler_t *global_sh = obs_get_signal_handler();
        if (global_sh) {
            signal_handler_disconnect(global_sh, "source_create", global_source_create_cb, this);
            signal_handler_disconnect(global_sh, "source_destroy", global_source_destroy_cb, this);
        }

        // 注销所有已跟踪源的信号
        std::set<obs_source_t *> sources_copy;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            sources_copy = m_tracked_sources;
        }
        for (obs_source_t *source : sources_copy) {
            unregister_source_signals(source);
        }

        // obs_log(LOG_INFO, "OBS event callbacks unregistered");
    }

    void EventManager::on_frontend_event(enum obs_frontend_event event, [[maybe_unused]] void *private_data)
    {
        switch (event) {
        case OBS_FRONTEND_EVENT_STREAMING_STARTING:
            handle_streaming_starting();
            break;

        case OBS_FRONTEND_EVENT_STREAMING_STARTED:
            handle_streaming_started();
            break;

        case OBS_FRONTEND_EVENT_STREAMING_STOPPING:
            handle_streaming_stopping();
            break;

        case OBS_FRONTEND_EVENT_STREAMING_STOPPED:
            handle_streaming_stopped();
            break;

        case OBS_FRONTEND_EVENT_RECORDING_STARTING:
            handle_recording_starting();
            break;

        case OBS_FRONTEND_EVENT_RECORDING_STARTED:
            handle_recording_started();
            break;

        case OBS_FRONTEND_EVENT_RECORDING_STOPPING:
            handle_recording_stopping();
            break;

        case OBS_FRONTEND_EVENT_RECORDING_STOPPED:
            handle_recording_stopped();
            break;

        case OBS_FRONTEND_EVENT_RECORDING_PAUSED:
            handle_recording_paused();
            break;

        case OBS_FRONTEND_EVENT_RECORDING_UNPAUSED:
            handle_recording_resumed();
            break;

        case OBS_FRONTEND_EVENT_SCENE_CHANGED:
            handle_scene_changed();
            break;

        case OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED:
            handle_scene_list_changed();
            break;

        case OBS_FRONTEND_EVENT_TRANSITION_CHANGED:
            handle_transition_begin();    // 保持旧映射：scene.transitionstarted
            handle_transition_changed();  // 新增：transition.changed
            break;

        case OBS_FRONTEND_EVENT_TRANSITION_STOPPED:
            handle_transition_end();
            break;

        case OBS_FRONTEND_EVENT_TRANSITION_LIST_CHANGED:
            handle_transition_list_changed();
            break;

        case OBS_FRONTEND_EVENT_TRANSITION_DURATION_CHANGED:
            handle_transition_duration_changed();
            break;

        case OBS_FRONTEND_EVENT_VIRTUALCAM_STARTED:
            handle_virtualcam_started();
            break;

        case OBS_FRONTEND_EVENT_VIRTUALCAM_STOPPED:
            handle_virtualcam_stopped();
            break;

        case OBS_FRONTEND_EVENT_REPLAY_BUFFER_STARTING:
            handle_replay_buffer_starting();
            break;

        case OBS_FRONTEND_EVENT_REPLAY_BUFFER_STARTED:
            handle_replay_buffer_started();
            break;

        case OBS_FRONTEND_EVENT_REPLAY_BUFFER_STOPPING:
            handle_replay_buffer_stopping();
            break;

        case OBS_FRONTEND_EVENT_REPLAY_BUFFER_STOPPED:
            handle_replay_buffer_stopped();
            break;

        case OBS_FRONTEND_EVENT_REPLAY_BUFFER_SAVED:
            handle_replay_buffer_saved();
            break;

        case OBS_FRONTEND_EVENT_EXIT:
            handle_exit_started();
            break;

        case OBS_FRONTEND_EVENT_STUDIO_MODE_ENABLED:
            handle_studiomode_enabled();
            break;

        case OBS_FRONTEND_EVENT_STUDIO_MODE_DISABLED:
            handle_studiomode_disabled();
            break;

        case OBS_FRONTEND_EVENT_PROFILE_CHANGED:
            handle_profile_changed();
            break;

        case OBS_FRONTEND_EVENT_PROFILE_LIST_CHANGED:
            handle_profile_list_changed();
            break;

        case OBS_FRONTEND_EVENT_PROFILE_RENAMED:
            handle_profile_renamed();
            break;

        case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
            handle_scene_collection_changed();
            break;

        case OBS_FRONTEND_EVENT_SCENE_COLLECTION_LIST_CHANGED:
            handle_scenecollection_list_changed();
            break;

        case OBS_FRONTEND_EVENT_SCENE_COLLECTION_RENAMED:
            handle_scenecollection_renamed();
            break;

        case OBS_FRONTEND_EVENT_FINISHED_LOADING:
            handle_obs_loaded();
            break;

        case OBS_FRONTEND_EVENT_PREVIEW_SCENE_CHANGED:
            handle_preview_scene_changed();
            break;

        case OBS_FRONTEND_EVENT_SCREENSHOT_TAKEN:
            handle_screenshot_taken();
            break;

        default:
            break;
        }
    }

    // 具体事件处理实现

    void EventManager::handle_streaming_starting()
    {
        nlohmann::json data;
        data["alwaysOn"] = false;
        emit_event(EventType::StreamingStarting, data);
    }

    void EventManager::handle_streaming_started()
    {
        nlohmann::json data;
        emit_event(EventType::StreamingStarted, data);
    }

    void EventManager::handle_streaming_stopping()
    {
        nlohmann::json data;
        emit_event(EventType::StreamingStopping, data);
    }

    void EventManager::handle_streaming_stopped()
    {
        nlohmann::json data;
        emit_event(EventType::StreamingStopped, data);
    }

    void EventManager::handle_recording_starting()
    {
        nlohmann::json data;
        emit_event(EventType::RecordingStarting, data);
    }

    void EventManager::handle_recording_started()
    {
        nlohmann::json data;
        emit_event(EventType::RecordingStarted, data);
    }

    void EventManager::handle_recording_stopping()
    {
        nlohmann::json data;
        emit_event(EventType::RecordingStopping, data);
    }

    void EventManager::handle_recording_stopped()
    {
        nlohmann::json data;
        emit_event(EventType::RecordingStopped, data);
    }

    void EventManager::handle_recording_paused()
    {
        nlohmann::json data;
        emit_event(EventType::RecordingPaused, data);
    }

    void EventManager::handle_recording_resumed()
    {
        nlohmann::json data;
        emit_event(EventType::RecordingResumed, data);
    }

    void EventManager::handle_scene_changed()
    {
        obs_source_t* current_scene = obs_frontend_get_current_scene();
        if (current_scene) {
            nlohmann::json data = get_scene_info(current_scene);
            emit_event(EventType::SceneChanged, data);
            obs_source_release(current_scene);
        }
    }

    void EventManager::handle_scene_list_changed()
    {
        nlohmann::json data;
        emit_event(EventType::SceneListChanged, data);
    }

    void EventManager::handle_transition_begin()
    {
        nlohmann::json data;
        emit_event(EventType::SceneTransitionStarted, data);
    }

    void EventManager::handle_transition_end()
    {
        nlohmann::json data;
        emit_event(EventType::SceneTransitionEnded, data);
    }

    void EventManager::handle_transition_changed()
    {
        obs_source_t *trans = obs_frontend_get_current_transition();
        nlohmann::json data;
        if (trans) {
            const char *name = obs_source_get_name(trans);
            data["transitionName"] = name ? name : "";
            data["duration"] = obs_frontend_get_transition_duration();
            obs_source_release(trans);
        }
        emit_event(EventType::TransitionChanged, data);
    }

    void EventManager::handle_transition_list_changed()
    {
        struct obs_frontend_source_list list = {0};
        obs_frontend_get_transitions(&list);
        nlohmann::json transitions = nlohmann::json::array();
        for (size_t i = 0; i < list.sources.num; i++) {
            const char *name = obs_source_get_name(list.sources.array[i]);
            if (name)
                transitions.push_back(name);
        }
        obs_frontend_source_list_free(&list);
        nlohmann::json data;
        data["transitions"] = transitions;
        emit_event(EventType::TransitionListChanged, data);
    }

    void EventManager::handle_transition_duration_changed()
    {
        nlohmann::json data;
        data["duration"] = obs_frontend_get_transition_duration();
        emit_event(EventType::TransitionDurationChanged, data);
    }

    void EventManager::handle_source_created(obs_source_t* source)
    {
        nlohmann::json data = get_source_info(source);
        emit_event(EventType::SourceCreated, data);
    }

    void EventManager::handle_source_destroyed(obs_source_t* source)
    {
        nlohmann::json data = get_source_info(source);
        emit_event(EventType::SourceDestroyed, data);
    }

    void EventManager::handle_source_volume_changed(obs_source_t* source, float volume)
    {
        nlohmann::json data = get_source_info(source, volume);
        emit_event(EventType::SourceVolumeChanged, data);
    }

    void EventManager::handle_source_mute_changed(obs_source_t* source)
    {
        nlohmann::json data = get_source_info(source);
        emit_event(EventType::SourceMuteStateChanged, data);
    }

    void EventManager::handle_virtualcam_started()
    {
        nlohmann::json data;
        emit_event(EventType::VirtualCamStarted, data);
    }

    void EventManager::handle_virtualcam_stopped()
    {
        nlohmann::json data;
        emit_event(EventType::VirtualCamStopped, data);
    }

    void EventManager::handle_replay_buffer_started()
    {
        nlohmann::json data;
        emit_event(EventType::ReplayBufferStarted, data);
    }

    void EventManager::handle_replay_buffer_stopped()
    {
        nlohmann::json data;
        emit_event(EventType::ReplayBufferStopped, data);
    }

    void EventManager::handle_replay_buffer_saved()
    {
        nlohmann::json data;
        emit_event(EventType::ReplayBufferSaved, data);
    }

    void EventManager::handle_replay_buffer_starting()
    {
        nlohmann::json data;
        emit_event(EventType::ReplayBufferStarting, data);
    }

    void EventManager::handle_replay_buffer_stopping()
    {
        nlohmann::json data;
        emit_event(EventType::ReplayBufferStopping, data);
    }

    void EventManager::handle_exit_started()
    {
        nlohmann::json data;
        emit_event(EventType::ExitStarted, data);
    }

    void EventManager::handle_studiomode_enabled()
    {
        nlohmann::json data;
        emit_event(EventType::StudioModeEnabled, data);
    }

    void EventManager::handle_studiomode_disabled()
    {
        nlohmann::json data;
        emit_event(EventType::StudioModeDisabled, data);
    }

    void EventManager::handle_profile_changed()
    {
        char *current_profile = obs_frontend_get_current_profile();
        nlohmann::json data;
        data["profileName"] = current_profile ? current_profile : "";
        emit_event(EventType::ProfileChanged, data);
        if (current_profile) {
            bfree(current_profile);
        }
    }

    void EventManager::handle_profile_list_changed()
    {
        char **profiles = obs_frontend_get_profiles();
        nlohmann::json list = nlohmann::json::array();
        if (profiles) {
            for (int i = 0; profiles[i]; i++)
                list.push_back(profiles[i]);
            bfree(profiles);
        }
        nlohmann::json data;
        data["profiles"] = list;
        emit_event(EventType::ProfileListChanged, data);
    }

    void EventManager::handle_profile_renamed()
    {
        char *current_profile = obs_frontend_get_current_profile();
        nlohmann::json data;
        data["profileName"] = current_profile ? current_profile : "";
        emit_event(EventType::ProfileRenamed, data);
        if (current_profile) {
            bfree(current_profile);
        }
    }

    void EventManager::handle_scene_collection_changed()
    {
        char *current_collection = obs_frontend_get_current_scene_collection();
        nlohmann::json data;
        data["sceneCollectionName"] = current_collection ? current_collection : "";
        emit_event(EventType::SceneCollectionChanged, data);
        if (current_collection) {
            bfree(current_collection);
        }
    }

    void EventManager::handle_scenecollection_list_changed()
    {
        char **collections = obs_frontend_get_scene_collections();
        nlohmann::json list = nlohmann::json::array();
        if (collections) {
            for (int i = 0; collections[i]; i++)
                list.push_back(collections[i]);
            bfree(collections);
        }
        nlohmann::json data;
        data["sceneCollections"] = list;
        emit_event(EventType::SceneCollectionListChanged, data);
    }

    void EventManager::handle_scenecollection_renamed()
    {
        char *current_collection = obs_frontend_get_current_scene_collection();
        nlohmann::json data;
        data["sceneCollectionName"] = current_collection ? current_collection : "";
        emit_event(EventType::SceneCollectionRenamed, data);
        if (current_collection) {
            bfree(current_collection);
        }
    }

    void EventManager::handle_obs_loaded()
    {
        nlohmann::json data;
        emit_event(EventType::ObsLoaded, data);
    }

    void EventManager::handle_preview_scene_changed()
    {
        obs_source_t *preview = obs_frontend_get_current_preview_scene();
        nlohmann::json data;
        if (preview) {
            const char *name = obs_source_get_name(preview);
            data["name"] = name ? name : "";
            obs_source_release(preview);
        }
        emit_event(EventType::ScenePreviewChanged, data);
    }

    void EventManager::handle_screenshot_taken()
    {
        nlohmann::json data;
        emit_event(EventType::ScreenshotTaken, data);
    }

    // ---------------------------------------------------------------
    // 源信号相关的公开回调方法
    // ---------------------------------------------------------------

    void EventManager::on_source_create(obs_source_t *source)
    {
        handle_source_created(source);
    }

    void EventManager::on_source_destroy(obs_source_t *source)
    {
        handle_source_destroyed(source);
    }

    void EventManager::on_source_volume_changed(obs_source_t *source, float volume)
    {
        handle_source_volume_changed(source, volume);
    }

    void EventManager::on_source_mute_changed(obs_source_t *source, bool /*muted*/)
    {
        handle_source_mute_changed(source);
    }

    void EventManager::on_source_renamed(obs_source_t *source, const char *new_name,
                                         const char *prev_name)
    {
        handle_source_renamed_impl(source, new_name, prev_name);
    }

    void EventManager::on_source_filter_add(obs_source_t *source, obs_source_t *filter)
    {
        // 记录滤镜→父源映射
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_filter_parent_map[filter] = source;
        }
        // 注册 enable 信号
        signal_handler_t *fsh = obs_source_get_signal_handler(filter);
        if (fsh)
            signal_handler_connect(fsh, "enable", source_filter_enable_cb, this);
    }

    void EventManager::on_source_filter_remove(obs_source_t * /*source*/, obs_source_t *filter)
    {
        // 注销 enable 信号
        signal_handler_t *fsh = obs_source_get_signal_handler(filter);
        if (fsh)
            signal_handler_disconnect(fsh, "enable", source_filter_enable_cb, this);
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_filter_parent_map.erase(filter);
        }
    }

    void EventManager::on_source_filter_enabled(obs_source_t *filter, bool enabled)
    {
        obs_source_t *parent = nullptr;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_filter_parent_map.find(filter);
            if (it != m_filter_parent_map.end())
                parent = it->second;
        }
        handle_source_filter_enabled_impl(parent, filter, enabled);
    }

    void EventManager::on_scene_item_add(obs_scene_t *scene, obs_sceneitem_t *item)
    {
        handle_source_added(scene, item);

        // 如果新添加的 item 是群组，注册其内部场景信号
        obs_source_t *item_source = obs_sceneitem_get_source(item);
        if (item_source) {
            obs_scene_t *group_scene = obs_group_from_source(item_source);
            if (group_scene) {
                obs_source_t *group_scene_source = obs_scene_get_source(group_scene);
                if (group_scene_source) {
                    signal_handler_t *group_sh = obs_source_get_signal_handler(group_scene_source);
                    if (group_sh) {
                        signal_handler_connect(group_sh, "item_add", scene_item_add_cb, this);
                        signal_handler_connect(group_sh, "item_remove", scene_item_remove_cb, this);
                        signal_handler_connect(group_sh, "item_visible", scene_item_visible_cb, this);
                        signal_handler_connect(group_sh, "item_locked", scene_item_locked_cb, this);
                    }
                }

                // 跟踪群组场景以便后续清理
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_tracked_group_scenes[item_source] = group_scene;
                }
            }
        }
    }

    void EventManager::on_scene_item_remove(obs_scene_t *scene, obs_sceneitem_t *item)
    {
        // 如果移除的 item 是群组，先注销其内部场景信号
        obs_source_t *item_source = obs_sceneitem_get_source(item);
        if (item_source) {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_tracked_group_scenes.find(item_source);
            if (it != m_tracked_group_scenes.end()) {
                obs_scene_t *group_scene = it->second;
                obs_source_t *group_scene_source = obs_scene_get_source(group_scene);
                if (group_scene_source) {
                    signal_handler_t *group_sh = obs_source_get_signal_handler(group_scene_source);
                    if (group_sh) {
                        signal_handler_disconnect(group_sh, "item_add", scene_item_add_cb, this);
                        signal_handler_disconnect(group_sh, "item_remove", scene_item_remove_cb, this);
                        signal_handler_disconnect(group_sh, "item_visible", scene_item_visible_cb, this);
                        signal_handler_disconnect(group_sh, "item_locked", scene_item_locked_cb, this);
                    }
                }
                m_tracked_group_scenes.erase(it);
            }
        }

        handle_source_removed(scene, item);
    }

    void EventManager::on_scene_item_visible(obs_scene_t *scene, obs_sceneitem_t *item,
                                             bool visible)
    {
        handle_scene_item_visibility(scene, item, visible);
    }

    void EventManager::on_scene_item_locked(obs_scene_t *scene, obs_sceneitem_t *item,
                                            bool locked)
    {
        handle_scene_item_locked(scene, item, locked);
    }

    void EventManager::on_source_media_state(obs_source_t *source, const std::string &state)
    {
        handle_source_media_state_changed(source, state);
    }

    // ---------------------------------------------------------------
    // 源信号事件处理实现
    // ---------------------------------------------------------------

    void EventManager::handle_source_added(obs_scene_t *scene, obs_sceneitem_t *item)
    {
        nlohmann::json data;
        obs_source_t *item_source = obs_sceneitem_get_source(item);
        if (item_source) {
            const char *src_name = obs_source_get_name(item_source);
            data["sourceName"] = src_name ? src_name : "";
            const char *src_id = obs_source_get_id(item_source);
            data["sourceType"] = src_id ? src_id : "unknown";
        }
        obs_source_t *scene_source = obs_scene_get_source(scene);
        if (scene_source) {
            const char *scene_name = obs_source_get_name(scene_source);
            data["sceneName"] = scene_name ? scene_name : "";
        }
        data["itemId"] = static_cast<int64_t>(obs_sceneitem_get_id(item));
        emit_event(EventType::SourceAdded, data);
    }

    void EventManager::handle_source_removed(obs_scene_t *scene, obs_sceneitem_t *item)
    {
        nlohmann::json data;
        obs_source_t *item_source = obs_sceneitem_get_source(item);
        if (item_source) {
            const char *src_name = obs_source_get_name(item_source);
            data["sourceName"] = src_name ? src_name : "";
        }
        obs_source_t *scene_source = obs_scene_get_source(scene);
        if (scene_source) {
            const char *scene_name = obs_source_get_name(scene_source);
            data["sceneName"] = scene_name ? scene_name : "";
        }
        data["itemId"] = static_cast<int64_t>(obs_sceneitem_get_id(item));
        emit_event(EventType::SourceRemoved, data);
    }

    void EventManager::handle_source_filter_enabled_impl(obs_source_t *parent,
                                                         obs_source_t *filter, bool enabled)
    {
        nlohmann::json data;
        if (parent) {
            const char *parent_name = obs_source_get_name(parent);
            data["sourceName"] = parent_name ? parent_name : "";
        }
        if (filter) {
            const char *filter_name = obs_source_get_name(filter);
            data["filterName"] = filter_name ? filter_name : "";
        }
        data["enabled"] = enabled;
        emit_event(EventType::SourceFilterEnabledChanged, data);
    }

    void EventManager::handle_scene_item_visibility(obs_scene_t *scene, obs_sceneitem_t *item,
                                                    bool visible)
    {
        nlohmann::json data;
        obs_source_t *scene_source = obs_scene_get_source(scene);
        if (scene_source) {
            const char *scene_name = obs_source_get_name(scene_source);
            data["sceneName"] = scene_name ? scene_name : "";
        }
        obs_source_t *item_source = obs_sceneitem_get_source(item);
        if (item_source) {
            const char *src_name = obs_source_get_name(item_source);
            data["sourceName"] = src_name ? src_name : "";
        }
        data["itemId"] = static_cast<int64_t>(obs_sceneitem_get_id(item));
        data["visible"] = visible;
        emit_event(EventType::SceneItemVisibilityChanged, data);
    }

    void EventManager::handle_scene_item_locked(obs_scene_t *scene, obs_sceneitem_t *item,
                                                bool locked)
    {
        nlohmann::json data;
        obs_source_t *scene_source = obs_scene_get_source(scene);
        if (scene_source) {
            const char *scene_name = obs_source_get_name(scene_source);
            data["sceneName"] = scene_name ? scene_name : "";
        }
        obs_source_t *item_source = obs_sceneitem_get_source(item);
        if (item_source) {
            const char *src_name = obs_source_get_name(item_source);
            data["sourceName"] = src_name ? src_name : "";
        }
        data["itemId"] = static_cast<int64_t>(obs_sceneitem_get_id(item));
        data["locked"] = locked;
        emit_event(EventType::SceneItemLockedChanged, data);
    }

    void EventManager::handle_source_media_state_changed(obs_source_t *source,
                                                         const std::string &state)
    {
        nlohmann::json data;
        const char *name = obs_source_get_name(source);
        data["sourceName"] = name ? name : "";
        data["state"] = state;
        emit_event(EventType::SourceMediaStateChanged, data);
    }

    void EventManager::handle_source_renamed_impl(obs_source_t *source, const char *new_name,
                                                  const char *prev_name)
    {
        nlohmann::json data;
        data["name"] = new_name ? new_name : "";
        data["prevName"] = prev_name ? prev_name : "";
        const char *src_id = obs_source_get_id(source);
        data["type"] = src_id ? src_id : "unknown";
        emit_event(EventType::SourceRenamed, data);
    }

    nlohmann::json EventManager::get_scene_info(obs_source_t *scene)
    {
        nlohmann::json info;
        if (!scene) {
            return info;
        }

        const char* name = obs_source_get_name(scene);
        info["name"] = name ? name : "";
        info["type"] = "scene";

        // 获取场景中的源
        obs_scene_t* obs_scene = obs_scene_from_source(scene);
        if (obs_scene) {
            nlohmann::json sources = nlohmann::json::array();

            obs_scene_enum_items(obs_scene, []([[maybe_unused]] obs_scene_t* scene,
                                              obs_sceneitem_t* item, void* param) {
                nlohmann::json* sources_array = static_cast<nlohmann::json*>(param);

                obs_source_t* source = obs_sceneitem_get_source(item);
                if (source) {
                    nlohmann::json source_info;
                    const char* source_name = obs_source_get_name(source);
                    source_info["name"] = source_name ? source_name : "";

                    const char* source_id = obs_source_get_id(source);
                    source_info["type"] = source_id ? source_id : "unknown";

                    source_info["id"] = reinterpret_cast<uintptr_t>(item);

                    sources_array->push_back(source_info);
                }

                return true;
            }, &sources);

            info["sources"] = sources;
        }

        return info;
    }

    nlohmann::json EventManager::get_source_info(obs_source_t *source, float override_volume)
    {
        nlohmann::json info;
        if (!source) {
            return info;
        }

        const char* name = obs_source_get_name(source);
        info["name"] = name ? name : "";

        const char* source_id = obs_source_get_id(source);
        info["type"] = source_id ? source_id : "unknown";

        info["id"] = reinterpret_cast<uintptr_t>(source);

        // 如果是音频源，添加音量和静音状态
        uint32_t flags = obs_source_get_output_flags(source);
        if (flags & OBS_SOURCE_AUDIO) {
            // 使用提供的音量值（如果 >= 0），否则查询源
            float volume;
            if (override_volume >= 0.0f) {
                volume = override_volume;
            } else {
                volume = obs_source_get_volume(source);
            }
            info["volume"] = volume;
            info["db"] = obs_mul_to_db(volume);
            info["muted"] = obs_source_muted(source);
        }

        return info;
    }

    // 辅助函数：创建事件通知JSON
    nlohmann::json create_event_notification(EventType event_type, const nlohmann::json& data)
    {
        nlohmann::json notification;
        notification["jsonrpc"] = "2.0";
        notification["method"] = "event";
        notification["params"]["event_type"] = event_type_to_string(event_type);
        notification["params"]["data"] = data;
        return notification;
    }

} // namespace events
} // namespace streamdock
