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
#include <QtCore/QCoreApplication>
#include <QtWidgets/QFrame>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QVBoxLayout>


namespace
{
static QString externalLink(const QString& url, const QString& label, const QColor& colour)
{
	return QStringLiteral("<a href=\"%1\" style=\"color:%2; text-decoration: underline;\">%3</a>")
		.arg(url.toHtmlEscaped(), colour.name(QColor::HexRgb), label.toHtmlEscaped());
}


static QWidget* makeAboutSection(QWidget* parent, const QString& icon_directory, const QString& icon_name,
	const QString& title, const QString& body_html, const QColor& accent_colour)
{
	QFrame* frame = new QFrame(parent);
	frame->setFrameShape(QFrame::StyledPanel);
	frame->setFrameShadow(QFrame::Plain);
	QVBoxLayout* frame_layout = new QVBoxLayout(frame);
	frame_layout->setContentsMargins(12, 10, 12, 10);
	frame_layout->setSpacing(7);

	QWidget* heading = new QWidget(frame);
	QHBoxLayout* heading_layout = new QHBoxLayout(heading);
	heading_layout->setContentsMargins(0, 0, 0, 0);
	heading_layout->setSpacing(8);
	QLabel* icon_label = new QLabel(heading);
	const QIcon icon = LucideIconUtils::tintedIcon(icon_directory, icon_name, accent_colour, 20);
	if(!icon.isNull())
		icon_label->setPixmap(icon.pixmap(QSize(20, 20)));
	else
		icon_label->hide();
	heading_layout->addWidget(icon_label, 0, Qt::AlignTop);
	QLabel* title_label = new QLabel(title, heading);
	QFont title_font = title_label->font();
	title_font.setBold(true);
	title_font.setPointSizeF(title_font.pointSizeF() + 1.0);
	title_label->setFont(title_font);
	heading_layout->addWidget(title_label, 1);
	frame_layout->addWidget(heading);

	QLabel* body = new QLabel(body_html, frame);
	body->setTextFormat(Qt::RichText);
	body->setTextInteractionFlags(Qt::TextBrowserInteraction);
	body->setOpenExternalLinks(true);
	body->setWordWrap(true);
	body->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	frame_layout->addWidget(body);
	return frame;
}
}


AboutDialog::AboutDialog(QWidget* parent, const std::string& appdata_path)
:	QDialog(parent)
{
	(void)appdata_path;
	setupUi(this);

	// Resolve links through the active palette.  Hard-coded dark link colours are
	// unreadable in themes such as Monokai.
	const QPalette text_palette = this->text->palette();
	const QColor link_colour = LucideIconUtils::themeAwareColour(
		text_palette.color(QPalette::Link), text_palette, QPalette::WindowText, QPalette::Window);
	const QString glare_link = externalLink(QStringLiteral("https://www.glaretechnologies.com/"), QStringLiteral("Glare-core"), link_colour);
	const QString ez_tree_link = externalLink(QStringLiteral("https://github.com/dgreenheck/ez-tree"), QStringLiteral("dgreenheck/ez-tree"), link_colour);
	const QString dan_link = externalLink(QStringLiteral("https://x.com/dangreenheck"), QStringLiteral("https://x.com/dangreenheck"), link_colour);
	const QString goxel_link = externalLink(QStringLiteral("https://github.com/guillaumechereau/goxel"), QStringLiteral("guillaumechereau/goxel"), link_colour);
	const QString guillaume_link = externalLink(QStringLiteral("https://github.com/guillaumechereau"), QStringLiteral("https://github.com/guillaumechereau"), link_colour);
	const QString pubchem_link = externalLink(QStringLiteral("https://pubchem.ncbi.nlm.nih.gov/"), QStringLiteral("https://pubchem.ncbi.nlm.nih.gov/"), link_colour);
	const QString lucide_link = externalLink(QStringLiteral("https://github.com/lucide-icons/lucide"), QStringLiteral("lucide-icons/lucide"), link_colour);
	const QString main_site_link = externalLink(QStringLiteral("https://metasiberia.com/"), QStringLiteral("metasiberia.com"), link_colour);
	const QString admin_link = externalLink(QStringLiteral("https://vr.metasiberia.com/"), QStringLiteral("vr.metasiberia.com"), link_colour);
	const QString avatars_link = externalLink(QStringLiteral("https://avatars.metasiberia.com/"), QStringLiteral("avatars.metasiberia.com"), link_colour);
	const QString telegram_link = externalLink(QStringLiteral("https://t.me/metasiberia_metaverse"), QStringLiteral("t.me/metasiberia_metaverse"), link_colour);
	const QString instagram_link = externalLink(QStringLiteral("https://www.instagram.com/metasiberia_official"), QStringLiteral("instagram.com/metasiberia_official"), link_colour);
	const QString vk_link = externalLink(QStringLiteral("https://vk.com/metasiberia_official"), QStringLiteral("vk.com/metasiberia_official"), link_colour);
	const QString denis_x_link = externalLink(QStringLiteral("https://x.com/denshipilovart"), QStringLiteral("x.com/denshipilovart"), link_colour);
	const QString denis_telegram_link = externalLink(QStringLiteral("https://t.me/denshipilov_metasiberia"), QStringLiteral("t.me/denshipilov_metasiberia"), link_colour);

	this->label->hide();
	this->text->hide();
	QScrollArea* scroll_area = new QScrollArea(this);
	scroll_area->setWidgetResizable(true);
	scroll_area->setFrameShape(QFrame::NoFrame);
	QWidget* content_widget = new QWidget(scroll_area);
	QVBoxLayout* content_layout = new QVBoxLayout(content_widget);
	content_layout->setContentsMargins(4, 4, 4, 4);
	content_layout->setSpacing(10);

	QLabel* heading = new QLabel(tr("Metasiberia v%1").arg(QString::fromStdString(::cyberspace_version)), content_widget);
	QFont heading_font = heading->font();
	heading_font.setBold(true);
	heading_font.setPointSizeF(heading_font.pointSizeF() + 5.0);
	heading->setFont(heading_font);
	content_layout->addWidget(heading);

	const QString icon_directory = LucideIconUtils::directoryForBasePath(QtUtils::toStdString(QCoreApplication::applicationDirPath()));
	const QColor accent_colour = LucideIconUtils::themeAwareColour(QColor(QStringLiteral("#22D3EE")), palette(), QPalette::WindowText, QPalette::Window);
	const QString official_links =
		tr("Main website: %1").arg(main_site_link) + QStringLiteral("<br/>") +
		tr("Administration: %1").arg(admin_link) + QStringLiteral("<br/>") +
		tr("Avatars: %1").arg(avatars_link) + QStringLiteral("<br/>") +
		tr("Telegram: %1").arg(telegram_link) + QStringLiteral("<br/>") +
		tr("Instagram: %1").arg(instagram_link) + QStringLiteral("<br/>") +
		tr("VK: %1").arg(vk_link);
	content_layout->addWidget(makeAboutSection(content_widget, icon_directory, QStringLiteral("globe"), tr("Metasiberia"), official_links, accent_colour));

	const QString author_info =
		tr("Author: <b>Denis Shipilov</b>") + QStringLiteral("<br/>") +
		tr("X: %1").arg(denis_x_link) + QStringLiteral("<br/>") +
		tr("Telegram: %1").arg(denis_telegram_link);
	content_layout->addWidget(makeAboutSection(content_widget, icon_directory, QStringLiteral("user-round"), tr("Author of Metasiberia"), author_info, accent_colour));

	QString foundations;
	foundations += tr("Metasiberia is inspired by and based on %1.").arg(glare_link) + QStringLiteral("<br/><br/>");
	foundations += tr("The Tree Editor is based on the %1 project.").arg(ez_tree_link) + QStringLiteral("<br/>");
	foundations += tr("Author: <b>%1</b> — %2").arg(QStringLiteral("Dan Greenheck"), dan_link) + QStringLiteral("<br/><br/>");
	foundations += tr("The Voxel Editor is based on the %1 project.").arg(goxel_link) + QStringLiteral("<br/>");
	foundations += tr("Author: <b>%1</b> — %2").arg(QStringLiteral("Guillaume Chereau"), guillaume_link) + QStringLiteral("<br/><br/>");
	foundations += tr("Interface icons are provided by %1.").arg(lucide_link);
	content_layout->addWidget(makeAboutSection(content_widget, icon_directory, QStringLiteral("boxes"), tr("Open-source foundations"), foundations, accent_colour));

	const QString scientific_info =
		tr("Data for the Scientific Object Editor comes from PubChem: %1.").arg(pubchem_link) + QStringLiteral("<br/>") +
		tr("The editor will gradually be expanded with new data.");
	content_layout->addWidget(makeAboutSection(content_widget, icon_directory, QStringLiteral("atom"), tr("Scientific data"), scientific_info, accent_colour));
	content_layout->addStretch(1);
	scroll_area->setWidget(content_widget);
	this->verticalLayout->insertWidget(0, scroll_area, 1);
	this->setMinimumSize(720, 560);
	this->resize(760, 650);

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
