#pragma once

#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>
#include <QtWidgets/QWidget>


class QVBoxLayout;


// Renders the bounded Plugin API panel DTO using host-owned Qt widgets.
// No plugin code receives a widget pointer or executes on the UI thread.
class PluginPanelWidget final : public QWidget
{
	Q_OBJECT
public:
	explicit PluginPanelWidget(const QJsonObject& layout, QWidget* parent = nullptr);
	bool isValid() const { return valid; }
	QString validationError() const { return validation_error; }

signals:
	void actionTriggered(const QString& actionId);

private:
	bool buildChildren(const QJsonArray& children, QVBoxLayout* layout);
	QWidget* buildElement(const QJsonObject& element, int depth);
	bool valid = false;
	QString validation_error;
};
