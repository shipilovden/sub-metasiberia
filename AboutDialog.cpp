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

	std::string display_str = "<h2>Metasiberia v" + ::cyberspace_version + "</h2>";

	display_str += "<p>";
	display_str += "Metasiberia is inspired and based on <a href=\"https://substrata.info\"><span style=\" text-decoration: underline; color:#222222;\">Substrata</span></a>.<br>";
	display_str += "Forked by <a href=\"https://x.com/denshipilovart\"><span style=\" text-decoration: underline; color:#222222;\">Denis Shipilov</span></a>";
	display_str += "</p>";

	this->text->setText(QtUtils::toQString(display_str));
	this->text->setOpenExternalLinks(true);

#if BUILD_TESTS
	this->generateCrashLabel->setText("<p><a href=\"#\">Generate Crash</a></p>");
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
