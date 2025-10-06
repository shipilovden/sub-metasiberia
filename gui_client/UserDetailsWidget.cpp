/*=====================================================================
UserDetailsWidget.cpp
---------------------
=====================================================================*/
#include "UserDetailsWidget.h"


#include "../qt/QtUtils.h"
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QErrorMessage>
#include <QtWidgets/QPushButton>
#include <QtCore/QSettings>
#include <QtCore/QVariant>
#include "../utils/ConPrint.h"


UserDetailsWidget::UserDetailsWidget(QWidget* parent)
:	QWidget(parent)
{
	setupUi(this);

	this->setTextAsNotLoggedIn();
}


UserDetailsWidget::~UserDetailsWidget()
{

}


void UserDetailsWidget::setTextAsNotLoggedIn()
{
    // Use Qt translation for auth strings
    this->userDetailsLabel->setText(
        QString("<a href=\"#login\">%1</a> %2 <a href=\"#signup\">%3</a>")
            .arg(tr("Log in"))
            .arg(tr("or"))
            .arg(tr("Sign up"))
    );
}


void UserDetailsWidget::setTextAsLoggedIn(const std::string& username)
{
    // "Logged in as <username>.  logout"
    const QString logged_in_as = tr("Logged in as ") + QtUtils::toQString(username).toHtmlEscaped();
    this->userDetailsLabel->setText(logged_in_as + ".   <a href=\"#logout\">" + tr("logout") + "</a>");
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
