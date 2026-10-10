/*=====================================================================
AIConnectionDialog.h
--------------------
The in-client control panel for the external Codex MCP connection.
=====================================================================*/
#pragma once


#include "ui_AIConnectionDialog.h"
#include <QtWidgets/QDialog>


class AIConnectionDialog : public QDialog, public Ui_AIConnectionDialog
{
	Q_OBJECT
public:
	AIConnectionDialog(const QString& endpoint, const QString& status, QWidget* parent = nullptr);

private slots:
	void copyEndpoint();
};
