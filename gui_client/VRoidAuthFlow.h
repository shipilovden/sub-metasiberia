/*=====================================================================
VRoidAuthFlow.h
---------------
Metasiberia: VRoid Hub OAuth + basic API calls (Qt client).
This is a minimal in-app flow:
- Opens system browser to VRoid Hub OAuth authorize endpoint.
- Receives callback on loopback http://127.0.0.1:51234/auth/vroid/callback
- Exchanges code for access token (requires client_id + client_secret).
=====================================================================*/
#pragma once


#include <QtCore/QObject>
#include <QtCore/QStringList>
#include <QtNetwork/QTcpServer>

class QSettings;


class VRoidAuthFlow final : public QObject
{
	Q_OBJECT
public:
	explicit VRoidAuthFlow(QSettings* settings, QObject* parent = nullptr);
	~VRoidAuthFlow() override;

	void startLogin();
	void logout();
	void fetchMyCharacterModels(); // Test endpoint to confirm auth works.

	bool hasAccessToken() const;

signals:
	void statusChanged(const QString& status);
	void loginSucceeded();
	void loginFailed(const QString& error);
	void modelsUpdated(const QStringList& items);
	void modelsFailed(const QString& error);

private slots:
	void onNewConnection();

private:
	void setStatus(const QString& s);
	void stopListening();

	QSettings* settings_ = nullptr;

	QTcpServer callback_server_;
	QString pending_state_;
	std::string pending_code_verifier_;

	bool login_in_progress_ = false;
};

