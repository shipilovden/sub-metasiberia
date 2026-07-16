/*=====================================================================
AboutDialog.cpp
-------------------
Copyright Glare Technologies Limited 2013 -
Generated at Fri Apr 05 15:18:57 +0200 2013
=====================================================================*/
#include "AboutDialog.h"


#include "LucideIconUtils.h"
#include "../shared/Version.h"
#include <qt/QtUtils.h>
#include <utils/ConPrint.h>
#include <QtGui/QPalette>


namespace
{
static QString externalLink(const QString& url, const QString& label, const QColor& colour)
{
	return QStringLiteral("<a href=\"%1\" style=\"color:%2; text-decoration: underline;\">%3</a>")
		.arg(url.toHtmlEscaped(), colour.name(QColor::HexRgb), label.toHtmlEscaped());
}
}


AboutDialog::AboutDialog(QWidget* parent, const std::string& appdata_path)
:	QDialog(parent)
{
	setupUi(this);

	// Resolve links through the active palette.  Hard-coded dark link colours are
	// unreadable in themes such as Monokai.
	const QPalette text_palette = this->text->palette();
	const QColor link_colour = LucideIconUtils::themeAwareColour(
		text_palette.color(QPalette::Link), text_palette, QPalette::WindowText, QPalette::Window);
	const QString glare_link = externalLink(QStringLiteral("https://www.glaretechnologies.com/"), QStringLiteral("Glare-core"), link_colour);
	const QString denis_link = externalLink(QStringLiteral("https://x.com/denshipilovart"), QStringLiteral("Denis Shipilov"), link_colour);
	const QString ez_tree_link = externalLink(QStringLiteral("https://github.com/dgreenheck/ez-tree"), QStringLiteral("dgreenheck/ez-tree"), link_colour);
	const QString dan_link = externalLink(QStringLiteral("https://x.com/dangreenheck"), QStringLiteral("https://x.com/dangreenheck"), link_colour);
	const QString goxel_link = externalLink(QStringLiteral("https://github.com/guillaumechereau/goxel"), QStringLiteral("guillaumechereau/goxel"), link_colour);
	const QString guillaume_link = externalLink(QStringLiteral("https://github.com/guillaumechereau"), QStringLiteral("https://github.com/guillaumechereau"), link_colour);
	const QString pubchem_link = externalLink(QStringLiteral("https://pubchem.ncbi.nlm.nih.gov/"), QStringLiteral("https://pubchem.ncbi.nlm.nih.gov/"), link_colour);

	QString display_str = "<h2>" + tr("Metasiberia v%1").arg(QString::fromStdString(::cyberspace_version)) + "</h2>";
	display_str += "<p>";
	display_str += tr("Metasiberia is inspired by and based on %1.").arg(glare_link) + "<br/>";
	display_str += tr("Author: %1").arg(denis_link);
	display_str += "</p>";

	display_str += "<p>";
	display_str += tr("The Tree Editor is based on the %1 project.").arg(ez_tree_link) + "<br/>";
	display_str += tr("Author: <b>%1</b> — %2").arg(QStringLiteral("Dan Greenheck"), dan_link) + "<br/><br/>";
	display_str += tr("The Voxel Editor is based on the %1 project.").arg(goxel_link) + "<br/>";
	display_str += tr("Author: <b>%1</b> — %2").arg(QStringLiteral("Guillaume Chereau"), guillaume_link);
	display_str += "</p>";

	display_str += "<p>";
	display_str += tr("Data for the Scientific Object Editor comes from PubChem: %1.").arg(pubchem_link) + "<br/>";
	display_str += tr("The editor will gradually be expanded with new data.");
	display_str += "</p>";

	this->text->setText(display_str);
	this->text->setTextInteractionFlags(Qt::TextBrowserInteraction);
	this->text->setOpenExternalLinks(true);
	this->text->setWordWrap(true);
	this->text->setMinimumWidth(620);
	this->resize(700, 420);

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
