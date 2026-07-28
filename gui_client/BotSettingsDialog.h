/*=====================================================================
BotSettingsDialog.h
-------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once

#include <QtWidgets/QDialog>
#include "../shared/Avatar.h"
#include <stdint.h>

namespace Ui { class BotSettingsDialog; }
class GUIClient;


/*=====================================================================
BotSettingsDialog
-----------------
Dialog for configuring a chatbot: name, avatar, prompt, animations,
reaction distances.  Opens after the server confirms bot creation.
=====================================================================*/
class BotSettingsDialog : public QDialog
{
	Q_OBJECT
public:
	BotSettingsDialog(QWidget* parent, GUIClient* gui_client_, uint64_t bot_id_);
	~BotSettingsDialog();

private slots:
	void on_saveButton_clicked();
	void on_deleteButton_clicked();
	void on_cancelButton_clicked();
	void on_browseAvatarButton_clicked();
	void on_testGreetingButton_clicked();
	void on_testIdleButton_clicked();
	void on_testReactiveButton_clicked();

private:
	Ui::BotSettingsDialog* ui;
	GUIClient* gui_client;
	uint64_t bot_id;
};
