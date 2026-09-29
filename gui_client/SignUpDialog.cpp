/*=====================================================================
SignUpDialog.cpp
----------------
=====================================================================*/
#include "SignUpDialog.h"


#include "CredentialManager.h"
#include "../qt/QtUtils.h"
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QErrorMessage>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QButtonGroup>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QFrame>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QVBoxLayout>
#include <QtGui/QPixmap>
#include <QtCore/QCoreApplication>
#include <QtCore/QSettings>


SignUpDialog::SignUpDialog(QSettings* settings_, CredentialManager* credential_manager_, const std::string& server_hostname_)
:	settings(settings_),
	server_hostname(server_hostname_),
	credential_manager(credential_manager_),
	xbotCheckBox(NULL),
	ybotCheckBox(NULL),
	termsAcceptedCheckBox(NULL),
	privacyAcceptedCheckBox(NULL)
{
	setupUi(this);

	// Remove question mark from the title bar (see https://stackoverflow.com/questions/81627/how-can-i-hide-delete-the-help-button-on-the-title-bar-of-a-qt-dialog)
	setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);


	// Load main window geometry and state
	this->restoreGeometry(settings->value("SignUpDialog/geometry").toByteArray());

	this->usernameLineEdit->setText(settings->value("SignUpDialog/username").toString());
	this->emailLineEdit   ->setText(settings->value("SignUpDialog/email"   ).toString());
	//this->passwordLineEdit->setText(settings->value("SignUpDialog/password").toString());

	QButtonGroup* avatar_button_group = new QButtonGroup(this);
	avatar_button_group->setExclusive(true);
	QGroupBox* avatar_group = new QGroupBox("Choose your avatar", this);
	QHBoxLayout* avatar_layout = new QHBoxLayout(avatar_group);
	avatar_layout->setSpacing(14);

	auto add_avatar_choice = [this, avatar_button_group, avatar_layout](const QString& name, const QString& model_choice, const QString& preview_filename, QCheckBox*& checkbox_out)
	{
		QFrame* card = new QFrame(this);
		card->setFrameShape(QFrame::StyledPanel);
		QVBoxLayout* card_layout = new QVBoxLayout(card);
		card_layout->setContentsMargins(8, 8, 8, 8);

		QLabel* preview = new QLabel(card);
		preview->setAlignment(Qt::AlignCenter);
		preview->setMinimumSize(150, 150);
		preview->setMaximumSize(220, 220);
		const QString preview_path = QCoreApplication::applicationDirPath() + "/data/resources/" + preview_filename;
		QPixmap preview_pixmap;
		if(preview_pixmap.load(preview_path))
			preview->setPixmap(preview_pixmap.scaled(190, 190, Qt::KeepAspectRatio, Qt::SmoothTransformation));
		else
			preview->setText(name);
		card_layout->addWidget(preview);

		checkbox_out = new QCheckBox(name, card);
		checkbox_out->setProperty("avatarChoice", model_choice);
		checkbox_out->setLayoutDirection(Qt::LeftToRight);
		card_layout->addWidget(checkbox_out, 0, Qt::AlignHCenter);
		avatar_button_group->addButton(checkbox_out);
		avatar_layout->addWidget(card);
	};

	add_avatar_choice("Xbot", "xbot", "xbot.png", xbotCheckBox);
	add_avatar_choice("Ybot", "ybot", "ybot.png", ybotCheckBox);
	this->verticalLayout->insertWidget(1, avatar_group);

	QGroupBox* consent_group = new QGroupBox("Required consents", this);
	QVBoxLayout* consent_layout = new QVBoxLayout(consent_group);
	auto add_consent = [this, consent_layout](const QString& prefix, const QString& link_text, const QString& link_url, QCheckBox*& checkbox_out)
	{
		QHBoxLayout* row = new QHBoxLayout();
		checkbox_out = new QCheckBox(this);
		row->addWidget(checkbox_out);
		QLabel* label = new QLabel(prefix + " <a href=\"" + link_url + "\">" + link_text + "</a>.", this);
		label->setTextFormat(Qt::RichText);
		label->setOpenExternalLinks(true);
		label->setWordWrap(true);
		row->addWidget(label, 1);
		consent_layout->addLayout(row);
	};
	add_consent("I have read and accept", "Terms of Use", "https://vr.metasiberia.com/terms", termsAcceptedCheckBox);
	add_consent("I consent to the processing of my personal data in accordance with", "Privacy Policy", "https://vr.metasiberia.com/privacy", privacyAcceptedCheckBox);
	this->verticalLayout->insertWidget(2, consent_group);

	auto update_submit_button = [this]()
	{
		const bool avatar_selected = xbotCheckBox->isChecked() != ybotCheckBox->isChecked();
		const bool required_consents_accepted = termsAcceptedCheckBox->isChecked() && privacyAcceptedCheckBox->isChecked();
		const bool required_fields_entered = !usernameLineEdit->text().trimmed().isEmpty() &&
			!emailLineEdit->text().trimmed().isEmpty() && !passwordLineEdit->text().isEmpty();
		this->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(required_fields_entered && avatar_selected && required_consents_accepted);
	};
	connect(xbotCheckBox, &QCheckBox::toggled, this, update_submit_button);
	connect(ybotCheckBox, &QCheckBox::toggled, this, update_submit_button);
	connect(termsAcceptedCheckBox, &QCheckBox::toggled, this, update_submit_button);
	connect(privacyAcceptedCheckBox, &QCheckBox::toggled, this, update_submit_button);
	connect(this->usernameLineEdit, &QLineEdit::textChanged, this, update_submit_button);
	connect(this->emailLineEdit, &QLineEdit::textChanged, this, update_submit_button);
	connect(this->passwordLineEdit, &QLineEdit::textChanged, this, update_submit_button);

	this->buttonBox->button(QDialogButtonBox::Ok)->setText("Sign up");

	// setupUi() connects accepted() directly to QDialog::accept(). Remove that
	// connection so the mandatory fields are checked before closing the dialog.
	disconnect(this->buttonBox, SIGNAL(accepted()), this, SLOT(accept()));
	connect(this->buttonBox, SIGNAL(accepted()), this, SLOT(accepted()));
	update_submit_button();
}


SignUpDialog::~SignUpDialog()
{
	settings->setValue("SignUpDialog/geometry", saveGeometry());
}


void SignUpDialog::accepted()
{
	if(selectedAvatarChoice().isEmpty() || !termsAccepted() || !privacyAccepted())
	{
		QMessageBox::warning(this, "Sign up", "Choose exactly one avatar and accept both required documents.");
		return;
	}

	settings->setValue("SignUpDialog/username", this->usernameLineEdit->text());
	settings->setValue("SignUpDialog/email",    this->emailLineEdit->text());
	//settings->setValue("SignUpDialog/password", this->passwordLineEdit->text());

	// Save these credentials as well, so that next time the user starts Substrata it can log them in.
	credential_manager->setDomainCredentials(server_hostname, QtUtils::toStdString(this->usernameLineEdit->text()), QtUtils::toStdString(this->passwordLineEdit->text()));

	credential_manager->saveToSettings(*settings);
	accept();
}


QString SignUpDialog::selectedAvatarChoice() const
{
	if(xbotCheckBox && xbotCheckBox->isChecked())
		return QStringLiteral("xbot");
	if(ybotCheckBox && ybotCheckBox->isChecked())
		return QStringLiteral("ybot");
	return QString();
}


bool SignUpDialog::termsAccepted() const
{
	return termsAcceptedCheckBox && termsAcceptedCheckBox->isChecked();
}


bool SignUpDialog::privacyAccepted() const
{
	return privacyAcceptedCheckBox && privacyAcceptedCheckBox->isChecked();
}
