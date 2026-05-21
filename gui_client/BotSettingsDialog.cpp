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
		(float)ui->reactiveCooldownSpin->value()
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
