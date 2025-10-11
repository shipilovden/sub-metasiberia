/*=====================================================================
AboutDialog.cpp
-------------------
Copyright Glare Technologies Limited 2013 -
Generated at Fri Apr 05 15:18:57 +0200 2013
=====================================================================*/
#include "AboutDialog.h"


#include "../shared/Version.h"
#include <qt/QtUtils.h>
#include <utils/ConPrint.h>


AboutDialog::AboutDialog(QWidget* parent, const std::string& appdata_path)
:	QDialog(parent)
{
	setupUi(this);

	// Use tr() for translatable strings
	QString display_str = "";

	// Add Substrata info with bold link first
	display_str += "<p>";
	display_str += tr("Metasiberia is inspired and based on <a href=\"https://substrata.info\"><span style=\" text-decoration: underline; color:#222222; font-weight: bold;\">Substrata</span></a>.");
	display_str += "</p>";

	// Add separator line
	display_str += "<hr style=\"border: 1px solid #ccc; margin: 10px 0;\">";

	// Add Metasiberia title and social media links
	display_str += "<h2>" + tr("Metasiberia v%1").arg(QString::fromStdString(::cyberspace_version)) + "</h2>";

	display_str += "<p>";
	display_str += "<a href=\"https://github.com/shipilovden/substrata-metasiberia\"><span style=\" text-decoration: underline; color:#222222; font-weight: bold;\">Github</span></a> ";
	display_str += "<a href=\"https://vk.com/metasiberia_official\"><span style=\" text-decoration: underline; color:#222222; font-weight: bold;\">Vk</span></a> ";
	display_str += "<a href=\"https://t.me/metasiberia_channel\"><span style=\" text-decoration: underline; color:#222222; font-weight: bold;\">Telegram</span></a>";
	display_str += "</p>";

	// Add separator line
	display_str += "<hr style=\"border: 1px solid #ccc; margin: 10px 0;\">";

	// Add author info with social links below
	display_str += "<p>";
	display_str += tr("Forked by Denis Shipilov<br>");
	display_str += "<a href=\"https://x.com/denshipilovart\"><span style=\" text-decoration: underline; color:#222222; font-weight: bold;\">X/Twitter</span></a> ";
	display_str += "<a href=\"https://vk.com/denshipilovart\"><span style=\" text-decoration: underline; color:#222222; font-weight: bold;\">Vk</span></a> ";
	display_str += "<a href=\"https://t.me/denshipilovart\"><span style=\" text-decoration: underline; color:#222222; font-weight: bold;\">Telegram</span></a>";
	display_str += "</p>";

	this->text->setText(display_str);
	this->text->setOpenExternalLinks(true);

#if BUILD_TESTS
	this->generateCrashLabel->setText("<p><a href=\"#\">" + tr("Generate Crash") + "</a></p>");
#else
	this->generateCrashLabel->hide();
#endif
}


AboutDialog::~AboutDialog()
{

}


void AboutDialog::on_generateCrashLabel_linkActivated(const QString& link)
{
	conPrint("Generating crash...");
#if defined(_MSC_VER) && !defined(__clang__)
	(*(int*)NULL) = 0;
#else
	__builtin_trap();
#endif
}
