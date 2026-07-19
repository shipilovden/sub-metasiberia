/*=====================================================================
UserDetailsWidget.cpp
---------------------
=====================================================================*/
#include "UserDetailsWidget.h"


#include "LucideIconUtils.h"
#include "../qt/QtUtils.h"
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QErrorMessage>
#include <QtWidgets/QPushButton>
#include <QtCore/QSettings>
#include <QtCore/QVariant>
#include <QtCore/QCoreApplication>
#include "../utils/ConPrint.h"


UserDetailsWidget::UserDetailsWidget(QWidget* parent)
:	QWidget(parent)
{
	setupUi(this);
	const QString icon_directory = LucideIconUtils::directoryForBasePath(QtUtils::toStdString(QCoreApplication::applicationDirPath()));
	const QColor icon_colour = LucideIconUtils::themeAwareColour(
		QColor(QStringLiteral("#F59E0B")), palette(), QPalette::ButtonText, QPalette::Window);
	LucideIconUtils::setButtonIcon(this->logoutButton, icon_directory, QStringLiteral("door-open"), icon_colour, 18);
	this->logoutButton->setToolTip(tr("Log out"));
	this->logoutButton->setAccessibleName(tr("Log out"));

	this->setTextAsNotLoggedIn();
}


UserDetailsWidget::~UserDetailsWidget()
{

}


void UserDetailsWidget::setTextAsNotLoggedIn()
{
	this->userDetailsLabel->setText("<a href=\"#login\">Log in</a> or <a href=\"#signup\">Sign up</a>");
	this->logoutButton->hide();
}


void UserDetailsWidget::setTextAsLoggedIn(const std::string& username)
{
	this->userDetailsLabel->setText("Logged in as " + QtUtils::toQString(username).toHtmlEscaped() + ".");
	this->logoutButton->show();
}


void UserDetailsWidget::on_userDetailsLabel_linkActivated(const QString& link)
{
	if(link == "#login")
	{
		emit logInClicked();
	}
	else if(link == "#logout")
	{
		emit logOutClicked();
	}
	else if(link == "#signup")
	{
		emit signUpClicked();
	}
}


void UserDetailsWidget::on_logoutButton_clicked()
{
	emit logOutClicked();
}
