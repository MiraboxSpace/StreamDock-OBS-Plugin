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

#include "handlers/handler-obs-scene.h"
#include "websocket-server.h"
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <obs.h>
#include <plugin-support.h>

namespace streamdock {
namespace handlers {

    std::shared_ptr<obs_scene> obs_scene::instance()
    {
        static std::weak_ptr<obs_scene> _instance;
        static std::mutex _lock;

        std::unique_lock<std::mutex> lock(_lock);
        if (_instance.expired()) {
            auto instance = std::make_shared<obs_scene>();
            _instance = instance;
            return instance;
        }

        return _instance.lock();
    }

    obs_scene::obs_scene()
    {
        // Handlers 将在 register_handlers 中注册
    }

    void obs_scene::register_handlers(WebSocketServer* server)
    {
        if (!server) {
            return;
        }

        using namespace std::placeholders;

        // === 场景项控制 ===
        server->handle_sync("scene.item.list",
            std::bind(&obs_scene::_item_list, this,
                      std::placeholders::_1, std::placeholders::_2));
        server->handle_sync("scene.item.visible.get",
            std::bind(&obs_scene::_item_visible_get, this,
                      std::placeholders::_1, std::placeholders::_2));
        server->handle_sync("scene.item.visible.set",
            std::bind(&obs_scene::_item_visible_set, this,
                      std::placeholders::_1, std::placeholders::_2));
        server->handle_sync("scene.item.locked.get",
            std::bind(&obs_scene::_item_locked_get, this,
                      std::placeholders::_1, std::placeholders::_2));
        server->handle_sync("scene.item.locked.set",
            std::bind(&obs_scene::_item_locked_set, this,
                      std::placeholders::_1, std::placeholders::_2));
        server->handle_sync("scene.item.transform.get",
            std::bind(&obs_scene::_item_transform_get, this,
                      std::placeholders::_1, std::placeholders::_2));
        server->handle_sync("scene.item.transform.set",
            std::bind(&obs_scene::_item_transform_set, this,
                      std::placeholders::_1, std::placeholders::_2));
    }

    // 辅助函数：获取场景
    static obs_scene_t* get_scene_by_name(const std::string& scene_name)
    {
        obs_source_t* source = obs_get_source_by_name(scene_name.c_str());
        if (!source) {
            return nullptr;
        }

        obs_scene_t* scene = obs_scene_from_source(source);
        obs_source_release(source); // scene 从 source 获取，需要释放 source

        return scene;
    }

    // 递归 ID 搜索的辅助结构
    struct SceneItemSearchData {
        int64_t target_id;
        obs_sceneitem_t* result;

        SceneItemSearchData(int64_t id) : target_id(id), result(nullptr) {}
    };

    // 回调函数：在群组中递归搜索 ID
    static bool search_sceneitem_by_id_callback(obs_scene_t* scene,
                                                 obs_sceneitem_t* item,
                                                 void* param)
    {
        (void)scene;  // API 要求的参数，当前实现中未使用
        SceneItemSearchData* data = static_cast<SceneItemSearchData*>(param);

        // 检查当前项是否匹配
        int64_t current_id = obs_sceneitem_get_id(item);
        if (current_id == data->target_id) {
            data->result = item;
            return false; // 找到后停止枚举
        }

        // 如果是群组，递归搜索
        if (obs_sceneitem_is_group(item)) {
            obs_sceneitem_group_enum_items(item, search_sceneitem_by_id_callback, param);
            if (data->result) {
                return false; // 在嵌套群组中找到，停止枚举
            }
        }

        return true; // 继续枚举
    }

    // 递归搜索场景项（通过 ID）
    static obs_sceneitem_t* get_sceneitem_by_id_recursive(obs_scene_t* scene, int64_t item_id)
    {
        if (!scene) {
            return nullptr;
        }

        SceneItemSearchData search_data(item_id);
        obs_scene_enum_items(scene, search_sceneitem_by_id_callback, &search_data);

        return search_data.result;
    }

    // 辅助函数：获取场景项（支持递归搜索嵌套项）
    static obs_sceneitem_t* get_sceneitem(obs_scene_t* scene, const std::string& item_name, int item_id = -1)
    {
        if (!scene) {
            return nullptr;
        }

        if (item_id >= 0) {
            // 使用自定义递归 ID 搜索
            return get_sceneitem_by_id_recursive(scene, item_id);
        } else {
            // 使用 OBS 内置的递归名称搜索
            return obs_scene_find_source_recursive(scene, item_name.c_str());
        }
    }

    // === 场景项控制 ===

    void obs_scene::_item_list(
        std::shared_ptr<streamdock::jsonrpc::request> req,
        std::shared_ptr<streamdock::jsonrpc::response> res)
    {
        nlohmann::json params;
        std::string scene_name;

        if (req->get_params(params)) {
            if (params.contains("scene") && params["scene"].is_string()) {
                scene_name = params["scene"].get<std::string>();
            }
        }

        execute_on_ui_thread([res, scene_name]() {
            obs_scene_t* scene = nullptr;

            if (scene_name.empty()) {
                // 使用当前场景
                obs_source_t* current_scene = obs_frontend_get_current_scene();
                if (current_scene) {
                    scene = obs_scene_from_source(current_scene);
                    obs_source_release(current_scene);
                }
            } else {
                scene = get_scene_by_name(scene_name);
            }

            if (!scene) {
                res->set_error(static_cast<int64_t>(streamdock::jsonrpc::error_codes::INVALID_PARAMS),
                              "Scene not found");
                return;
            }

            nlohmann::json items = nlohmann::json::array();

            // Helper structure for enum callbacks
            struct item_enum_data {
                std::function<void(obs_sceneitem_t*, nlohmann::json&)> add_item_info;
                nlohmann::json* items_array;
            };

            // Static callback function for top-level items
            auto enum_callback = [](obs_scene_t*, obs_sceneitem_t* item, void* param) -> bool {
                item_enum_data* data = static_cast<item_enum_data*>(param);
                if (data && data->add_item_info && data->items_array) {
                    data->add_item_info(item, *data->items_array);
                }
                return true;
            };

            // Static callback function for group items
            auto group_enum_callback = [](obs_scene_t*, obs_sceneitem_t* nested_item, void* param) -> bool {
                item_enum_data* data = static_cast<item_enum_data*>(param);
                if (data && data->add_item_info && data->items_array) {
                    data->add_item_info(nested_item, *data->items_array);
                }
                return true;
            };

            // Helper function to add scene item info including nested groups
            std::function<void(obs_sceneitem_t*, nlohmann::json&)> add_item_info =
                [&add_item_info, &group_enum_callback](obs_sceneitem_t* item, nlohmann::json& items_array) {
                    obs_source_t* source = obs_sceneitem_get_source(item);
                    const char* name = obs_source_get_name(source);
                    const char* type_id = obs_source_get_id(source);
                    int64_t id = obs_sceneitem_get_id(item);
                    bool visible = obs_sceneitem_visible(item);
                    bool locked = obs_sceneitem_locked(item);
                    bool is_group = obs_sceneitem_is_group(item);

                    nlohmann::json item_info;
                    item_info["name"] = name ? name : "";
                    item_info["id"] = id;
                    item_info["typeId"] = type_id ? type_id : "";
                    item_info["visible"] = visible;
                    item_info["locked"] = locked;
                    item_info["isGroup"] = is_group;

                    // Handle nested items in groups
                    if (is_group) {
                        nlohmann::json nested_items = nlohmann::json::array();

                        item_enum_data nested_data;
                        nested_data.add_item_info = add_item_info;
                        nested_data.items_array = &nested_items;

                        obs_sceneitem_group_enum_items(item, group_enum_callback, &nested_data);
                        item_info["nestedItems"] = nested_items;
                    }

                    items_array.push_back(item_info);
                };

            item_enum_data enum_data;
            enum_data.add_item_info = add_item_info;
            enum_data.items_array = &items;

            obs_scene_enum_items(scene, enum_callback, &enum_data);

            nlohmann::json result;
            result["items"] = items;
            res->set_result(result);
        });
    }

    void obs_scene::_item_visible_get(
        std::shared_ptr<streamdock::jsonrpc::request> req,
        std::shared_ptr<streamdock::jsonrpc::response> res)
    {
        nlohmann::json params;
        if (!req->get_params(params)) {
            throw streamdock::jsonrpc::invalid_params_error("Missing parameters");
        }

        if (!params.contains("scene") || !params["scene"].is_string()) {
            throw streamdock::jsonrpc::invalid_params_error("Missing or invalid 'scene' parameter");
        }

        std::string scene_name = params["scene"].get<std::string>();
        obs_scene_t* scene = get_scene_by_name(scene_name);

        if (!scene) {
            res->set_error(static_cast<int64_t>(streamdock::jsonrpc::error_codes::INVALID_PARAMS),
                          "Scene not found");
            return;
        }

        // 获取场景项（支持递归搜索嵌套项）
        obs_sceneitem_t* item = nullptr;
        if (params.contains("id") && params["id"].is_number_integer()) {
            int64_t id = params["id"].get<int64_t>();
            item = get_sceneitem_by_id_recursive(scene, id);
        } else if (params.contains("name") && params["name"].is_string()) {
            std::string item_name = params["name"].get<std::string>();
            item = obs_scene_find_source_recursive(scene, item_name.c_str());
        }

        if (!item) {
            res->set_error(static_cast<int64_t>(streamdock::jsonrpc::error_codes::INVALID_PARAMS),
                          "Scene item not found");
            return;
        }

        bool visible = obs_sceneitem_visible(item);

        nlohmann::json result;
        result["visible"] = visible;
        res->set_result(result);
    }

    void obs_scene::_item_visible_set(
        std::shared_ptr<streamdock::jsonrpc::request> req,
        std::shared_ptr<streamdock::jsonrpc::response> res)
    {
        nlohmann::json params;
        if (!req->get_params(params)) {
            throw streamdock::jsonrpc::invalid_params_error("Missing parameters");
        }

        if (!params.contains("scene") || !params["scene"].is_string()) {
            throw streamdock::jsonrpc::invalid_params_error("Missing or invalid 'scene' parameter");
        }
        if (!params.contains("visible") || !params["visible"].is_boolean()) {
            throw streamdock::jsonrpc::invalid_params_error("Missing or invalid 'visible' parameter");
        }

        std::string scene_name = params["scene"].get<std::string>();
        bool visible = params["visible"].get<bool>();

        obs_scene_t* scene = get_scene_by_name(scene_name);

        if (!scene) {
            res->set_error(static_cast<int64_t>(streamdock::jsonrpc::error_codes::INVALID_PARAMS),
                          "Scene not found");
            return;
        }

        // 获取场景项（支持递归搜索嵌套项）
        obs_sceneitem_t* item = nullptr;
        if (params.contains("id") && params["id"].is_number_integer()) {
            int64_t id = params["id"].get<int64_t>();
            item = get_sceneitem_by_id_recursive(scene, id);
        } else if (params.contains("name") && params["name"].is_string()) {
            std::string item_name = params["name"].get<std::string>();
            item = obs_scene_find_source_recursive(scene, item_name.c_str());
        }

        if (!item) {
            res->set_error(static_cast<int64_t>(streamdock::jsonrpc::error_codes::INVALID_PARAMS),
                          "Scene item not found");
            return;
        }

        obs_sceneitem_set_visible(item, visible);

        nlohmann::json result;
        result["visible"] = obs_sceneitem_visible(item);
        res->set_result(result);
    }

    void obs_scene::_item_locked_get(
        std::shared_ptr<streamdock::jsonrpc::request> req,
        std::shared_ptr<streamdock::jsonrpc::response> res)
    {
        nlohmann::json params;
        if (!req->get_params(params)) {
            throw streamdock::jsonrpc::invalid_params_error("Missing parameters");
        }

        if (!params.contains("scene") || !params["scene"].is_string()) {
            throw streamdock::jsonrpc::invalid_params_error("Missing or invalid 'scene' parameter");
        }

        std::string scene_name = params["scene"].get<std::string>();
        obs_scene_t* scene = get_scene_by_name(scene_name);

        if (!scene) {
            res->set_error(static_cast<int64_t>(streamdock::jsonrpc::error_codes::INVALID_PARAMS),
                          "Scene not found");
            return;
        }

        // 获取场景项（支持递归搜索嵌套项）
        obs_sceneitem_t* item = nullptr;
        if (params.contains("id") && params["id"].is_number_integer()) {
            int64_t id = params["id"].get<int64_t>();
            item = get_sceneitem_by_id_recursive(scene, id);
        } else if (params.contains("name") && params["name"].is_string()) {
            std::string item_name = params["name"].get<std::string>();
            item = obs_scene_find_source_recursive(scene, item_name.c_str());
        }

        if (!item) {
            res->set_error(static_cast<int64_t>(streamdock::jsonrpc::error_codes::INVALID_PARAMS),
                          "Scene item not found");
            return;
        }

        bool locked = obs_sceneitem_locked(item);

        nlohmann::json result;
        result["locked"] = locked;
        res->set_result(result);
    }

    void obs_scene::_item_locked_set(
        std::shared_ptr<streamdock::jsonrpc::request> req,
        std::shared_ptr<streamdock::jsonrpc::response> res)
    {
        nlohmann::json params;
        if (!req->get_params(params)) {
            throw streamdock::jsonrpc::invalid_params_error("Missing parameters");
        }

        if (!params.contains("scene") || !params["scene"].is_string()) {
            throw streamdock::jsonrpc::invalid_params_error("Missing or invalid 'scene' parameter");
        }
        if (!params.contains("locked") || !params["locked"].is_boolean()) {
            throw streamdock::jsonrpc::invalid_params_error("Missing or invalid 'locked' parameter");
        }

        std::string scene_name = params["scene"].get<std::string>();
        bool locked = params["locked"].get<bool>();

        obs_scene_t* scene = get_scene_by_name(scene_name);

        if (!scene) {
            res->set_error(static_cast<int64_t>(streamdock::jsonrpc::error_codes::INVALID_PARAMS),
                          "Scene not found");
            return;
        }

        // 获取场景项（支持递归搜索嵌套项）
        obs_sceneitem_t* item = nullptr;
        if (params.contains("id") && params["id"].is_number_integer()) {
            int64_t id = params["id"].get<int64_t>();
            item = get_sceneitem_by_id_recursive(scene, id);
        } else if (params.contains("name") && params["name"].is_string()) {
            std::string item_name = params["name"].get<std::string>();
            item = obs_scene_find_source_recursive(scene, item_name.c_str());
        }

        if (!item) {
            res->set_error(static_cast<int64_t>(streamdock::jsonrpc::error_codes::INVALID_PARAMS),
                          "Scene item not found");
            return;
        }

        obs_sceneitem_set_locked(item, locked);

        nlohmann::json result;
        result["locked"] = obs_sceneitem_locked(item);
        res->set_result(result);
    }

    void obs_scene::_item_transform_get(
        std::shared_ptr<streamdock::jsonrpc::request> req,
        std::shared_ptr<streamdock::jsonrpc::response> res)
    {
        nlohmann::json params;
        if (!req->get_params(params)) {
            throw streamdock::jsonrpc::invalid_params_error("Missing parameters");
        }

        if (!params.contains("scene") || !params["scene"].is_string()) {
            throw streamdock::jsonrpc::invalid_params_error("Missing or invalid 'scene' parameter");
        }

        std::string scene_name = params["scene"].get<std::string>();
        obs_scene_t* scene = get_scene_by_name(scene_name);

        if (!scene) {
            res->set_error(static_cast<int64_t>(streamdock::jsonrpc::error_codes::INVALID_PARAMS),
                          "Scene not found");
            return;
        }

        // 获取场景项（支持递归搜索嵌套项）
        obs_sceneitem_t* item = nullptr;
        if (params.contains("id") && params["id"].is_number_integer()) {
            int64_t id = params["id"].get<int64_t>();
            item = get_sceneitem_by_id_recursive(scene, id);
        } else if (params.contains("name") && params["name"].is_string()) {
            std::string item_name = params["name"].get<std::string>();
            item = obs_scene_find_source_recursive(scene, item_name.c_str());
        }

        if (!item) {
            res->set_error(static_cast<int64_t>(streamdock::jsonrpc::error_codes::INVALID_PARAMS),
                          "Scene item not found");
            return;
        }

        // 获取变换信息
        struct obs_transform_info info;
        obs_sceneitem_get_info2(item, &info);

        nlohmann::json result;
        result["transform"] = nlohmann::json::object();
        result["transform"]["pos"] = {info.pos.x, info.pos.y};
        result["transform"]["scale"] = {info.scale.x, info.scale.y};
        result["transform"]["rotation"] = info.rot;
        result["transform"]["alignment"] = info.alignment;
        result["transform"]["bounds_type"] = static_cast<int>(info.bounds_type);
        result["transform"]["bounds_alignment"] = info.bounds_alignment;
        result["transform"]["bounds"] = {info.bounds.x, info.bounds.y};
        res->set_result(result);
    }

    void obs_scene::_item_transform_set(
        std::shared_ptr<streamdock::jsonrpc::request> req,
        std::shared_ptr<streamdock::jsonrpc::response> res)
    {
        nlohmann::json params;
        if (!req->get_params(params)) {
            throw streamdock::jsonrpc::invalid_params_error("Missing parameters");
        }

        if (!params.contains("scene") || !params["scene"].is_string()) {
            throw streamdock::jsonrpc::invalid_params_error("Missing or invalid 'scene' parameter");
        }

        std::string scene_name = params["scene"].get<std::string>();
        obs_scene_t* scene = get_scene_by_name(scene_name);

        if (!scene) {
            res->set_error(static_cast<int64_t>(streamdock::jsonrpc::error_codes::INVALID_PARAMS),
                          "Scene not found");
            return;
        }

        // 获取场景项（支持递归搜索嵌套项）
        obs_sceneitem_t* item = nullptr;
        if (params.contains("id") && params["id"].is_number_integer()) {
            int64_t id = params["id"].get<int64_t>();
            item = get_sceneitem_by_id_recursive(scene, id);
        } else if (params.contains("name") && params["name"].is_string()) {
            std::string item_name = params["name"].get<std::string>();
            item = obs_scene_find_source_recursive(scene, item_name.c_str());
        }

        if (!item) {
            res->set_error(static_cast<int64_t>(streamdock::jsonrpc::error_codes::INVALID_PARAMS),
                          "Scene item not found");
            return;
        }

        // 获取当前变换
        struct obs_transform_info info;
        obs_sceneitem_get_info2(item, &info);

        // 应用新变换参数
        if (params.contains("transform") && params["transform"].is_object()) {
            nlohmann::json transform = params["transform"];

            if (transform.contains("pos") && transform["pos"].is_array()) {
                auto pos = transform["pos"];
                if (pos.size() >= 2) {
                    info.pos.x = pos[0].get<float>();
                    info.pos.y = pos[1].get<float>();
                }
            }

            if (transform.contains("scale") && transform["scale"].is_array()) {
                auto scale = transform["scale"];
                if (scale.size() >= 2) {
                    info.scale.x = scale[0].get<float>();
                    info.scale.y = scale[1].get<float>();
                }
            }

            if (transform.contains("rotation") && transform["rotation"].is_number()) {
                info.rot = transform["rotation"].get<float>();
            }

            if (transform.contains("alignment") && transform["alignment"].is_number()) {
                info.alignment = transform["alignment"].get<uint32_t>();
            }

            if (transform.contains("bounds_type") && transform["bounds_type"].is_number()) {
                info.bounds_type = static_cast<obs_bounds_type>(transform["bounds_type"].get<int>());
            }

            if (transform.contains("bounds_alignment") && transform["bounds_alignment"].is_number()) {
                info.bounds_alignment = transform["bounds_alignment"].get<uint32_t>();
            }

            if (transform.contains("bounds") && transform["bounds"].is_array()) {
                auto bounds = transform["bounds"];
                if (bounds.size() >= 2) {
                    info.bounds.x = bounds[0].get<float>();
                    info.bounds.y = bounds[1].get<float>();
                }
            }
        }

        obs_sceneitem_set_info2(item, &info);

        // 返回当前状态
        obs_sceneitem_get_info2(item, &info);

        nlohmann::json result;
        result["transform"] = nlohmann::json::object();
        result["transform"]["pos"] = {info.pos.x, info.pos.y};
        result["transform"]["scale"] = {info.scale.x, info.scale.y};
        result["transform"]["rotation"] = info.rot;
        result["transform"]["alignment"] = info.alignment;
        result["transform"]["bounds_type"] = static_cast<int>(info.bounds_type);
        result["transform"]["bounds_alignment"] = info.bounds_alignment;
        result["transform"]["bounds"] = {info.bounds.x, info.bounds.y};
        res->set_result(result);
    }

} // namespace handlers
} // namespace streamdock
