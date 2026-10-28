#include "PluginPanelWidget.h"

#include "PluginHostInterface.h"

#include <QtGui/QFont>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QVBoxLayout>


PluginPanelWidget::PluginPanelWidget(const QJsonObject& layout, QWidget* parent)
: QWidget(parent)
{
	QVBoxLayout* const root = new QVBoxLayout(this);
	root->setContentsMargins(12, 12, 12, 12);
	root->setSpacing(10);
	if(!validatePluginPanelLayout(layout, validation_error))
		return;
	valid = buildChildren(layout.value(QStringLiteral("children")).toArray(), root);
	if(!valid && validation_error.isEmpty())
		validation_error = tr("The plugin panel layout could not be rendered.");
	root->addStretch(1);
}


bool PluginPanelWidget::buildChildren(const QJsonArray& children, QVBoxLayout* layout)
{
	for(const QJsonValue& child : children)
	{
		QWidget* const widget = buildElement(child.toObject(), 1);
		if(!widget) return false;
		layout->addWidget(widget);
	}
	return true;
}


QWidget* PluginPanelWidget::buildElement(const QJsonObject& element, const int depth)
{
	if(depth > 4) return nullptr;
	const QString type = element.value(QStringLiteral("type")).toString();
	if(type == QStringLiteral("text"))
	{
		QLabel* const label = new QLabel(element.value(QStringLiteral("text")).toString(), this);
		label->setWordWrap(true);
		label->setTextFormat(Qt::PlainText);
		label->setTextInteractionFlags(Qt::TextSelectableByMouse);
		const QString style = element.value(QStringLiteral("style")).toString(QStringLiteral("body"));
		if(style == QStringLiteral("heading"))
		{
			QFont font = label->font();
			font.setPointSize(font.pointSize() + 4);
			font.setWeight(QFont::DemiBold);
			label->setFont(font);
		}
		else if(style == QStringLiteral("muted"))
			label->setStyleSheet(QStringLiteral("color: palette(mid);"));
		return label;
	}
	if(type == QStringLiteral("button"))
	{
		QPushButton* const button = new QPushButton(element.value(QStringLiteral("label")).toString(), this);
		const QString action_id = element.value(QStringLiteral("id")).toString();
		button->setObjectName(QStringLiteral("pluginViewAction_%1").arg(action_id));
		button->setToolTip(tr("Run the %1 action provided by this plugin.").arg(button->text()));
		connect(button, &QPushButton::clicked, this, [this, action_id]() { emit actionTriggered(action_id); });
		return button;
	}
	if(type == QStringLiteral("column"))
	{
		QWidget* const container = new QWidget(this);
		QVBoxLayout* const column = new QVBoxLayout(container);
		column->setContentsMargins(0, 0, 0, 0);
		column->setSpacing(8);
		for(const QJsonValue& child : element.value(QStringLiteral("children")).toArray())
		{
			QWidget* const widget = buildElement(child.toObject(), depth + 1);
			if(!widget)
			{
				delete container;
				return nullptr;
			}
			column->addWidget(widget);
		}
		return container;
	}
	return nullptr;
}
