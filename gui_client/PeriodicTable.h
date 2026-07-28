/*=====================================================================
PeriodicTable.h
----------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


#include <QtCore/QAbstractTableModel>
#include <QtCore/QString>
#include <QtCore/QVector>
#include <QtWidgets/QWidget>
#include <functional>


struct PeriodicElementRecord
{
	int atomic_number;
	QString symbol;
	QString name;
	QString atomic_mass;
	QString cpk_hex_colour;
	int group;
	int period;
	QString category;
	QString electron_configuration;
	QString density;
	QString melting_point;
	QString boiling_point;
	QString electronegativity;
	QString atomic_radius;
	QString ionization_energy;
	QString electron_affinity;
	QString oxidation_states;
	QString standard_state;
	QString year_discovered;
	QString source;
};


class PeriodicTableModel : public QAbstractTableModel
{
public:
	explicit PeriodicTableModel(QObject* parent = 0);

	int rowCount(const QModelIndex& parent = QModelIndex()) const override;
	int columnCount(const QModelIndex& parent = QModelIndex()) const override;
	QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
	QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

	const PeriodicElementRecord* elementAt(int row) const;
	static const QVector<PeriodicElementRecord>& allElements();
	static const PeriodicElementRecord* elementBySymbol(const QString& symbol);
};


class PeriodicTable3DVisualizer : public QWidget
{
public:
	explicit PeriodicTable3DVisualizer(QWidget* parent = 0);
	void setHighlightedSymbols(const QStringList& symbols);
	void setPropertyName(const QString& property_name);

protected:
	void paintEvent(QPaintEvent* event) override;

private:
	QStringList highlighted_symbols;
	QString property_name;
};


class PeriodicTableLayoutWidget : public QWidget
{
public:
	explicit PeriodicTableLayoutWidget(QWidget* parent = 0);
	void setHighlightedSymbols(const QStringList& symbols);
	void selectElement(const QString& symbol);

	std::function<void(const QString&)> elementActivated;

protected:
	void paintEvent(QPaintEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;

private:
	QRectF cellRectForElement(const PeriodicElementRecord& element) const;
	const PeriodicElementRecord* elementAtPoint(const QPoint& point) const;

	QStringList highlighted_symbols;
	QString selected_symbol;
};


class PeriodicTableWidget : public QWidget
{
public:
	explicit PeriodicTableWidget(QWidget* parent = 0);
	void setHighlightedSymbols(const QStringList& symbols);
	void selectElement(const QString& symbol);

	std::function<void(const QString&)> elementActivated;

private:
	PeriodicTableModel* model;
	PeriodicTableLayoutWidget* layout_widget;
	class QTableView* table_view;
	class QListWidget* list_widget;
	PeriodicTable3DVisualizer* graph_widget;
	class QLabel* element_card_label;
};
