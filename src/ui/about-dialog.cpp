#include "ui/about-dialog.h"
#include "ui_about.h"

#include <QPushButton>

#include <obs-module.h>
#include <plugin-support.h>
#include "websocket-server.h"

// 声明外部函数
extern WebSocketServer* get_websocket_server();

AboutDialog::AboutDialog(QWidget *parent) : QDialog(parent), ui(new Ui::AboutDialog)
{
	ui->setupUi(this);
	setWindowTitle(obs_module_text("StreamDock.Title"));
	ui->titleLabel->setText(obs_module_text("StreamDock.PluginTitle"));
	ui->description->setText(
		QString::fromUtf8(obs_module_text("StreamDock.ObsPluginVersion")).arg(OBS_PLUGIN_VERSION));
	ui->closeButton->setText(obs_module_text("StreamDock.Close"));
	connect(ui->closeButton, &QPushButton::clicked, this, &QDialog::accept);

	// 设置定时器以更新连接状态
	connect(&m_status_timer, &QTimer::timeout, this, &AboutDialog::update_connection_status);
	m_status_timer.start(1000); // 每1秒更新一次

	// 初始更新
	update_connection_status();
}

AboutDialog::~AboutDialog()
{
	m_status_timer.stop();
	delete ui;
}

void AboutDialog::update_connection_status()
{
	WebSocketServer* server = get_websocket_server();
	if (!server) {
		ui->serverStatus->setText(obs_module_text("StreamDock.ConnectionStatus.Disconnected"));
		return;
	}

	size_t connections = server->get_connection_count();
	if (connections > 0) {
		// 有客户端连接，显示已连接状态
		QString status = QString::fromUtf8(obs_module_text("StreamDock.ConnectionStatus.Connected"))
			.arg(QString::fromUtf8(PLUGIN_VERSION));
		ui->serverStatus->setText(status);
	} else {
		// 没有客户端连接
		ui->serverStatus->setText(obs_module_text("StreamDock.ConnectionStatus.Disconnected"));
	}
}
