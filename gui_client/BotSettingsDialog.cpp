/*=====================================================================
BotSettingsDialog.cpp
---------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "BotSettingsDialog.h"
#include "ui_BotSettingsDialog.h"
#include "GUIClient.h"
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QMessageBox>
#include "../shared/Avatar.h"


BotSettingsDialog::BotSettingsDialog(QWidget* parent, GUIClient* gui_client_, uint64_t bot_id_)
:	QDialog(parent),
	ui(new Ui::BotSettingsDialog),
	gui_client(gui_client_),
	bot_id(bot_id_)
{
	ui->setupUi(this);
	setWindowTitle(QString("Bot Settings (id=%1)").arg(bot_id));
}


BotSettingsDialog::~BotSettingsDialog()
{
	delete ui;
}


void BotSettingsDialog::on_saveButton_clicked()
{
	AvatarSettings avatar_settings;
	avatar_settings.model_url = toURLString(ui->avatarURLEdit->text().toStdString());

	gui_client->updateBot(
		bot_id,
		ui->nameEdit->text().toStdString(),
		ui->promptEdit->toPlainText().toStdString(),
		avatar_settings,
		ui->greetingNameEdit->text().toStdString(),
		ui->greetingURLEdit->text().toStdString(),
		(float)ui->greetingCooldownSpin->value(),
		ui->idleNameEdit->text().toStdString(),
		ui->idleURLEdit->text().toStdString(),
		(float)ui->idleIntervalSpin->value(),
		ui->reactiveNameEdit->text().toStdString(),
		ui->reactiveURLEdit->text().toStdString(),
		(float)ui->reactiveCooldownSpin->value(),
		/*flags=*/0u, /*greeting_dist=*/6.f, /*farewell_dist=*/10.f, /*chat_radius=*/8.f,
		Vec3f(1.f),
		/*ai_model_id=*/"", /*ai_preset=*/"assistant", /*ai_knowledge=*/"", /*ai_temp=*/0.7f, /*ai_max_tokens=*/0u,
		/*audio_url=*/"", /*audio_vol=*/1.f, /*audio_radius=*/10.f, /*audio_activation=*/12.f, /*audio_cooldown=*/0.f,
		/*trigger_flags=*/3u, /*trigger_keywords=*/"", /*trigger_cooldown=*/3.f,
		/*greet_gesture_flags=*/0u, /*idle_gesture_flags=*/0u, /*react_gesture_flags=*/0u,
		/*fallback=*/"",
		/*surprise_name=*/"", /*surprise_url=*/"", /*surprise_flags=*/0u, /*surprise_cooldown=*/15.f,
		/*ack_name=*/"", /*ack_url=*/"", /*ack_flags=*/0u, /*ack_cooldown=*/10.f,
		/*use_action_type=*/0u, /*use_action_param=*/"",
		/*api_key=*/"", /*api_endpoint=*/"",
		/*movement_type=*/0u, /*walk_speed=*/1.4f, /*wander_radius=*/5.f,
		/*waypoints=*/{}, /*use_actions=*/{},
		/*farewell_gesture_name=*/"", /*farewell_gesture_url=*/"", /*farewell_gesture_flags=*/0u, /*farewell_gesture_cooldown=*/8.f,
		/*walk_gesture_name=*/"", /*walk_gesture_url=*/"", /*walk_gesture_flags=*/0u,
		/*talk_gesture_name=*/"", /*talk_gesture_url=*/"", /*talk_gesture_flags=*/0u,
		/*interaction_gesture_name=*/"", /*interaction_gesture_url=*/"", /*interaction_gesture_flags=*/0u, /*interaction_gesture_cooldown=*/3.f,
		/*audio_min_distance=*/1.f, /*audio_start_delay=*/0.f,
		/*greeting_audio_url=*/"", /*farewell_audio_url=*/"", /*interaction_audio_url=*/""
	);

	accept();
}


void BotSettingsDialog::on_deleteButton_clicked()
{
	const int ret = QMessageBox::question(this, "Delete Bot",
		"Are you sure you want to permanently delete this bot?",
		QMessageBox::Yes | QMessageBox::No);
	if(ret == QMessageBox::Yes)
	{
		gui_client->deleteBot(bot_id);
		accept();
	}
}


void BotSettingsDialog::on_cancelButton_clicked()
{
	reject();
}


void BotSettingsDialog::on_browseAvatarButton_clicked()
{
	const QString path = QFileDialog::getOpenFileName(this, "Select Avatar Model",
		QString(), "3D Models (*.glb *.gltf *.obj *.vox)");
	if(!path.isEmpty())
		ui->avatarURLEdit->setText(path);
}


void BotSettingsDialog::on_testGreetingButton_clicked()
{
	// TODO: send test animation request to server
	QMessageBox::information(this, "Test", "Test animation not yet implemented.");
}


void BotSettingsDialog::on_testIdleButton_clicked()
{
	QMessageBox::information(this, "Test", "Test animation not yet implemented.");
}


void BotSettingsDialog::on_testReactiveButton_clicked()
{
	QMessageBox::information(this, "Test", "Test animation not yet implemented.");
}
