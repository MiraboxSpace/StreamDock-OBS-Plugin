#pragma once

#include <QDialog>
#include <QTimer>

QT_BEGIN_NAMESPACE
namespace Ui {
class AboutDialog;
}
QT_END_NAMESPACE

class AboutDialog : public QDialog {
	Q_OBJECT

public:
	explicit AboutDialog(QWidget *parent = nullptr);
	~AboutDialog() override;

private slots:
	void update_connection_status();

private:
	Ui::AboutDialog *ui;
	QTimer m_status_timer;
};
