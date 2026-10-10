/*=====================================================================
AIConnectionDialog.cpp
----------------------
=====================================================================*/
#include "AIConnectionDialog.h"


#include <QtGui/QClipboard>
#include <QtWidgets/QApplication>


AIConnectionDialog::AIConnectionDialog(const QString& endpoint, const QString& status, QWidget* parent)
: QDialog(parent)
{
	setupUi(this);
	setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
	endpointLineEdit->setText(endpoint);
	statusLabel->setText(status);
	instructionsLabel->setText(tr("Codex CLI: codex mcp add metasiberia --url %1\nVS Code: add an HTTP MCP server with this URL to .vscode/mcp.json.").arg(endpoint));
	connect(copyEndpointPushButton, &QPushButton::clicked, this, &AIConnectionDialog::copyEndpoint);
}


void AIConnectionDialog::copyEndpoint()
{
	QApplication::clipboard()->setText(endpointLineEdit->text());
	copyStatusLabel->setText(tr("Endpoint copied"));
}
