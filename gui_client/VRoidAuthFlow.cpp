/*=====================================================================
VRoidAuthFlow.cpp
-----------------
See VRoidAuthFlow.h
=====================================================================*/
#include "VRoidAuthFlow.h"


#include "../utils/ConPrint.h"
#include "../utils/CryptoRNG.h"
#include "../utils/Exception.h"
#include "../utils/JSONParser.h"
#include "../utils/PlatformUtils.h"
#include "../utils/StringUtils.h"
#include <utils/Base64.h>
#include <utils/SHA256.h>
#include <webserver/Escaping.h>
#include "HTTPClient.h"

#include <QtCore/QDateTime>
#include <QtCore/QSettings>
#include <QtCore/QUrl>
#include <QtCore/QUrlQuery>
#include <QtGui/QDesktopServices>
#include <QtNetwork/QTcpSocket>
#include <QtCore/QThread>


static const int kVRoidCallbackPort = 51234;
static const char* kVRoidCallbackPath = "/auth/vroid/callback";
static const char* kVRoidAPIVersionHeader = "X-Api-Version: 11";


static std::string base64URLNoPad(const void* data, size_t len)
{
	std::string b64;
	Base64::encode(data, len, b64);

	// Base64URL: '+'->'-', '/'->'_', strip '=' padding.
	for(char& c : b64)
	{
		if(c == '+') c = '-';
		else if(c == '/') c = '_';
	}
	while(!b64.empty() && b64.back() == '=')
		b64.pop_back();
	return b64;
}


static std::string makeFormBody(const std::vector<std::pair<std::string, std::string>>& params)
{
	std::string body;
	body.reserve(params.size() * 32);
	for(size_t i = 0; i < params.size(); ++i)
	{
		if(i > 0) body += "&";
		body += web::Escaping::URLEscape(params[i].first);
		body += "=";
		body += web::Escaping::URLEscape(params[i].second);
	}
	return body;
}


static QString getEnvQString(const char* name)
{
	try
	{
		return QString::fromStdString(PlatformUtils::getEnvironmentVariable(name));
	}
	catch(glare::Exception&)
	{
		return QString();
	}
}


static QString vroidClientId()
{
	// Keep secrets out of the repo; devs can set env vars via a launcher script.
	return getEnvQString("METASIBERIA_VROID_CLIENT_ID");
}

static QString vroidClientSecret()
{
	return getEnvQString("METASIBERIA_VROID_CLIENT_SECRET");
}


static QString vroidRedirectURI()
{
	// Loopback redirect for in-app auth.
	return QString("http://127.0.0.1:%1%2").arg(kVRoidCallbackPort).arg(kVRoidCallbackPath);
}

class VRoidTokenExchangeThread final : public QThread
{
public:
	QString code;
	QString code_verifier;
	QString client_id;
	QString client_secret;
	QString redirect_uri;

	QString access_token;
	QString refresh_token;
	qint64 expires_in = 0;
	qint64 created_at = 0;
	QString error;

protected:
	void run() override
	{
		try
		{
			HTTPClient client;
			client.additional_headers.push_back("User-Agent: Metasiberia client");
			client.additional_headers.push_back(kVRoidAPIVersionHeader);

			const std::string body = makeFormBody({
				{"grant_type", "authorization_code"},
				{"code", code.toStdString()},
				{"redirect_uri", redirect_uri.toStdString()},
				{"client_id", client_id.toStdString()},
				{"client_secret", client_secret.toStdString()},
				{"code_verifier", code_verifier.toStdString()},
			});

			std::string response_body;
			HTTPClient::ResponseInfo resp = client.sendPost(
				"https://hub.vroid.com/oauth/token",
				body,
				"application/x-www-form-urlencoded",
				response_body
			);

			if(resp.response_code != 200)
				throw glare::Exception("VRoid OAuth token endpoint returned HTTP " + toString(resp.response_code) + ". Response: " + response_body);

			JSONParser parser;
			parser.parseBuffer(response_body.c_str(), response_body.size());
			const JSONNode& root = parser.nodes[0];

			access_token = QString::fromStdString(root.getChildStringValue(parser, "access_token"));
			refresh_token = QString::fromStdString(root.getChildStringValueWithDefaultVal(parser, "refresh_token", ""));
			expires_in = (qint64)root.getChildIntValueWithDefaultVal(parser, "expires_in", 0);
			created_at = (qint64)root.getChildIntValueWithDefaultVal(parser, "created_at", 0);
		}
		catch(glare::Exception& e)
		{
			error = QString::fromStdString(e.what());
		}
	}
};


class VRoidFetchModelsThread final : public QThread
{
public:
	QString access_token;

	QStringList items;
	QString error;

protected:
	void run() override
	{
		try
		{
			HTTPClient client;
			client.additional_headers.push_back("User-Agent: Metasiberia client");
			client.additional_headers.push_back(kVRoidAPIVersionHeader);
			client.additional_headers.push_back(std::string("Authorization: Bearer ") + access_token.toStdString());

			std::string response_body;
			HTTPClient::ResponseInfo resp = client.downloadFile(
				"https://hub.vroid.com/api/account/character_models",
				response_body
			);

			if(resp.response_code != 200)
				throw glare::Exception("VRoid API returned HTTP " + toString(resp.response_code) + ". Response: " + response_body);

			JSONParser parser;
			parser.parseBuffer(response_body.c_str(), response_body.size());
			const JSONNode& root = parser.nodes[0];

			// Expected: {"data":[{...}, ...]}
			const JSONNode& data_arr = root.getChildArray(parser, "data");
			items.reserve((int)data_arr.child_indices.size());
			for(size_t i = 0; i < data_arr.child_indices.size(); ++i)
			{
				const JSONNode& obj = parser.nodes[data_arr.child_indices[i]];
				const std::string title = obj.getChildStringValueWithDefaultVal(parser, "name", "");
				const std::string id    = obj.getChildStringValueWithDefaultVal(parser, "id", "");

				QString line = QString::fromStdString(title);
				if(!id.empty())
					line += QString(" (") + QString::fromStdString(id) + ")";
				items.push_back(line);
			}
		}
		catch(glare::Exception& e)
		{
			error = QString::fromStdString(e.what());
		}
	}
};


VRoidAuthFlow::VRoidAuthFlow(QSettings* settings, QObject* parent)
:	QObject(parent),
	settings_(settings)
{
	connect(&callback_server_, SIGNAL(newConnection()), this, SLOT(onNewConnection()));
}


VRoidAuthFlow::~VRoidAuthFlow()
{
	stopListening();
}


bool VRoidAuthFlow::hasAccessToken() const
{
	if(!settings_)
		return false;
	return !settings_->value("vroid/access_token").toString().isEmpty();
}


void VRoidAuthFlow::setStatus(const QString& s)
{
	emit statusChanged(s);
}


void VRoidAuthFlow::stopListening()
{
	if(callback_server_.isListening())
		callback_server_.close();
}


void VRoidAuthFlow::startLogin()
{
	if(login_in_progress_)
		return;

	if(!settings_)
	{
		emit loginFailed("VRoid: settings is null");
		return;
	}

	const QString client_id = vroidClientId();
	const QString client_secret = vroidClientSecret();
	if(client_id.isEmpty() || client_secret.isEmpty())
	{
		emit loginFailed("VRoid OAuth is not configured. Set env vars METASIBERIA_VROID_CLIENT_ID and METASIBERIA_VROID_CLIENT_SECRET.");
		return;
	}

	// Generate state + PKCE verifier.
	uint8 state_bytes[16];
	uint8 verifier_bytes[32];
	try
	{
		CryptoRNG::getRandomBytes(state_bytes, sizeof(state_bytes));
		CryptoRNG::getRandomBytes(verifier_bytes, sizeof(verifier_bytes));
	}
	catch(glare::Exception& e)
	{
		emit loginFailed(QString("VRoid RNG failed: ") + QString::fromStdString(e.what()));
		return;
	}

	pending_state_ = QString::fromStdString(base64URLNoPad(state_bytes, sizeof(state_bytes)));
	pending_code_verifier_ = base64URLNoPad(verifier_bytes, sizeof(verifier_bytes)); // 43 chars.

	const std::vector<unsigned char> digest = SHA256::hash(pending_code_verifier_);
	const std::string code_challenge = base64URLNoPad(digest.data(), digest.size());

	// Start local callback server.
	stopListening();
	if(!callback_server_.listen(QHostAddress::LocalHost, (quint16)kVRoidCallbackPort))
	{
		emit loginFailed(QString("VRoid callback server listen failed on 127.0.0.1:%1: %2")
			.arg(kVRoidCallbackPort)
			.arg(callback_server_.errorString()));
		return;
	}

	login_in_progress_ = true;
	setStatus("VRoid: waiting for browser login...");

	// Build authorize URL.
	QUrl url("https://hub.vroid.com/oauth/authorize");
	QUrlQuery q;
	q.addQueryItem("response_type", "code");
	q.addQueryItem("client_id", client_id);
	q.addQueryItem("redirect_uri", vroidRedirectURI());
	q.addQueryItem("scope", "default");
	q.addQueryItem("state", pending_state_);
	q.addQueryItem("code_challenge", QString::fromStdString(code_challenge));
	q.addQueryItem("code_challenge_method", "S256");
	url.setQuery(q);

	QDesktopServices::openUrl(url);
}


void VRoidAuthFlow::logout()
{
	if(!settings_)
		return;

	settings_->remove("vroid/access_token");
	settings_->remove("vroid/refresh_token");
	settings_->remove("vroid/expires_in");
	settings_->remove("vroid/created_at");
	settings_->sync();

	setStatus("VRoid: logged out.");
}


void VRoidAuthFlow::fetchMyCharacterModels()
{
	if(!settings_)
		return;

	const QString access_token = settings_->value("vroid/access_token").toString();
	if(access_token.isEmpty())
	{
		emit modelsFailed("VRoid: not logged in.");
		return;
	}

	VRoidFetchModelsThread* t = new VRoidFetchModelsThread();
	t->access_token = access_token;
	connect(t, &QThread::finished, this, [this, t]()
	{
		if(!t->error.isEmpty())
			emit modelsFailed(t->error);
		else
			emit modelsUpdated(t->items);
		t->deleteLater();
	});
	t->start();
}


static QString httpOKPage()
{
	return "<html><body><h3>VRoid OAuth завершен</h3>"
		"<p>Можете закрыть это окно и вернуться в Metasiberia.</p>"
		"</body></html>";
}


void VRoidAuthFlow::onNewConnection()
{
	QTcpSocket* sock = callback_server_.nextPendingConnection();
	if(!sock)
		return;

	// Read the request (best-effort).
	if(!sock->waitForReadyRead(2000))
	{
		sock->disconnectFromHost();
		sock->deleteLater();
		return;
	}

	QByteArray req = sock->readAll();
	const int line_end = req.indexOf("\r\n");
	const QByteArray first_line = (line_end >= 0) ? req.left(line_end) : req;

	QString path_qs;
	{
		// "GET /path?qs HTTP/1.1"
		const QList<QByteArray> parts = first_line.split(' ');
		if(parts.size() >= 2)
			path_qs = QString::fromUtf8(parts[1]);
	}

	QUrl url(QString("http://127.0.0.1") + path_qs);
	QUrlQuery q(url);
	const QString path = url.path();
	const QString code = q.queryItemValue("code");
	const QString state = q.queryItemValue("state");
	const QString error = q.queryItemValue("error");
	const QString error_desc = q.queryItemValue("error_description");

	// Respond to browser immediately.
	const QByteArray body = httpOKPage().toUtf8();
	QByteArray resp;
	resp += "HTTP/1.1 200 OK\r\n";
	resp += "Content-Type: text/html; charset=utf-8\r\n";
	resp += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
	resp += "Connection: close\r\n\r\n";
	resp += body;
	sock->write(resp);
	sock->flush();
	sock->disconnectFromHost();
	sock->deleteLater();

	stopListening();

	if(path != kVRoidCallbackPath)
	{
		login_in_progress_ = false;
		emit loginFailed(QString("VRoid callback received unexpected path: ") + path);
		return;
	}

	if(!error.isEmpty())
	{
		login_in_progress_ = false;
		emit loginFailed(QString("VRoid OAuth error: ") + error + (error_desc.isEmpty() ? "" : (QString(" (") + error_desc + ")")));
		return;
	}

	if(code.isEmpty())
	{
		login_in_progress_ = false;
		emit loginFailed("VRoid OAuth callback missing code.");
		return;
	}

	if(state.isEmpty() || state != pending_state_)
	{
		login_in_progress_ = false;
		emit loginFailed("VRoid OAuth state mismatch.");
		return;
	}

	// Exchange code for token in a background thread.
	const QString client_id = vroidClientId();
	const QString client_secret = vroidClientSecret();
	if(client_id.isEmpty() || client_secret.isEmpty())
	{
		login_in_progress_ = false;
		emit loginFailed("VRoid OAuth is not configured (client_id/client_secret missing).");
		return;
	}

	setStatus("VRoid: exchanging token...");

	VRoidTokenExchangeThread* t = new VRoidTokenExchangeThread();
	t->code = code;
	t->code_verifier = QString::fromStdString(pending_code_verifier_);
	t->client_id = client_id;
	t->client_secret = client_secret;
	t->redirect_uri = vroidRedirectURI();
	connect(t, &QThread::finished, this, [this, t]()
	{
		if(!t->error.isEmpty())
		{
			login_in_progress_ = false;
			emit loginFailed(t->error);
			t->deleteLater();
			return;
		}

		if(settings_)
		{
			settings_->setValue("vroid/access_token", t->access_token);
			if(!t->refresh_token.isEmpty())
				settings_->setValue("vroid/refresh_token", t->refresh_token);
			if(t->expires_in > 0)
				settings_->setValue("vroid/expires_in", (qlonglong)t->expires_in);
			if(t->created_at > 0)
				settings_->setValue("vroid/created_at", (qlonglong)t->created_at);
			settings_->sync();
		}

		login_in_progress_ = false;
		setStatus("VRoid: logged in.");
		emit loginSucceeded();

		t->deleteLater();
	});
	t->start();
}
