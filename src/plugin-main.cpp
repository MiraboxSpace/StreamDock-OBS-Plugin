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

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <plugin-support.h>

#include <QMainWindow>
#include "ui/about-dialog.h"
#include "include/websocket-server.h"
#include "include/event-manager.h"
#include "include/handlers/handler-system.h"
#include "include/handlers/handler-obs-frontend.h"
#include "include/handlers/handler-obs-source.h"
#include "include/handlers/handler-obs-scene.h"
#include "include/handlers/handler-events.h"

using namespace streamdock::handlers;

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

// 全局 WebSocket 服务器实例
static std::unique_ptr<WebSocketServer> g_websocket_server;

// 全局事件管理器实例
static std::shared_ptr<streamdock::events::EventManager> g_event_manager;

// 获取 WebSocket 服务器实例（供 UI 使用）
WebSocketServer* get_websocket_server()
{
	return g_websocket_server.get();
}

static void show_streamdock_dialog(void *)
{
	QMainWindow *main_window = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	AboutDialog dialog(main_window);
	dialog.exec();
}

bool obs_module_load(void)
{
	// obs_log(LOG_INFO, "plugin loaded successfully (version %s)", PLUGIN_VERSION);

	obs_frontend_push_ui_translation(obs_module_get_string);
	obs_frontend_add_tools_menu_item(obs_module_text("MiraBox.StreamDock"), show_streamdock_dialog, nullptr);
	obs_frontend_pop_ui_translation();

	// 启动 WebSocket 服务器
	g_websocket_server = std::make_unique<WebSocketServer>();
	if (g_websocket_server->start(8765)) {
		// obs_log(LOG_INFO, "WebSocket server started on port 8765");
	} else {
		obs_log(LOG_ERROR, "Failed to start WebSocket server");
	}

	// 初始化事件管理器
	g_event_manager = streamdock::events::EventManager::instance();
	g_event_manager->initialize(g_websocket_server.get());
	g_websocket_server->set_event_manager(g_event_manager);
	// obs_log(LOG_INFO, "Event manager initialized");

	// 初始化并注册 handlers
	auto system_handler = streamdock::handlers::system::instance();
	system_handler->register_handlers(g_websocket_server.get());
	// obs_log(LOG_INFO, "System handlers registered");

	auto obs_frontend_handler = streamdock::handlers::obs_frontend::instance();
	obs_frontend_handler->register_handlers(g_websocket_server.get());
	// obs_log(LOG_INFO, "OBS frontend handlers registered");

	auto obs_source_handler = streamdock::handlers::obs_source::instance();
	obs_source_handler->register_handlers(g_websocket_server.get());
	// obs_log(LOG_INFO, "OBS source handlers registered");

	auto obs_scene_handler = streamdock::handlers::obs_scene::instance();
	obs_scene_handler->register_handlers(g_websocket_server.get());
	// obs_log(LOG_INFO, "OBS scene handlers registered");

	auto events_handler = streamdock::events::handlers::events::instance();
	events_handler->set_event_manager(g_event_manager);
	events_handler->register_handlers(g_websocket_server.get());
	// obs_log(LOG_INFO, "Event handlers registered");

	return true;
}

void obs_module_unload(void)
{
	// 清理事件管理器
	if (g_event_manager) {
		g_event_manager->shutdown();
		g_event_manager.reset();
	}

	// 停止 WebSocket 服务器
	if (g_websocket_server) {
		g_websocket_server->stop();
		g_websocket_server.reset();
	}

	// obs_log(LOG_INFO, "plugin unloaded");
}
