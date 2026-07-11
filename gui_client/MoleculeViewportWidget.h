/*=====================================================================
MoleculeViewportWidget.h
-------------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


#include "ScientificObjectSettings.h"
#include <maths/Vec4f.h>
#include <QtCore/QPoint>
#include <QtCore/QString>
#include <QtCore/QVector>
#include <QtWidgets/QWidget>
#include <functional>


class MoleculeViewportWidget : public QWidget
{
public:
	explicit MoleculeViewportWidget(QWidget* parent = 0);
	~MoleculeViewportWidget();

	void setMolecule(const QString& atom_table, const QString& bond_table);
	void setScientificSettings(const ScientificObjectSettings& settings);
	void setSelectionMode(const QString& mode);
	QString selectionMode() const;
	QString selectionState() const;
	QString selectedAtomsText() const;
	int selectedBondIndex() const;
	void setSelectionState(const QString& selected_atoms, int selected_bond, const QString& state);
	QString measurementsJson() const;
	void setMeasurementsJson(const QString& json);
	QString moleculeMetricsText() const;
	void resetView();
	void beginMeasurement(const QString& kind);
	void clearMeasurements();
	void setSpinEnabled(bool enabled, float speed);
	void selectAtomBySourceID(int source_id, bool additive = false);
	bool handleSceneRay(const Vec4f& ray_origin_os, const Vec4f& ray_dir_os, bool show_context_menu, bool additive, const QPoint& global_pos);

	std::function<void()> stateChanged;
	std::function<void(const QString&, int)> actionRequested;

protected:
	void paintEvent(QPaintEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;
	void wheelEvent(QWheelEvent* event) override;
	void contextMenuEvent(QContextMenuEvent* event) override;

private:
	struct Atom;
	struct Bond;
	struct Measurement;
	struct ProjectedAtom;

	QVector<ProjectedAtom> projectAtoms() const;
	int pickAtom(const QPoint& pos, const QVector<ProjectedAtom>& projected) const;
	int pickBond(const QPoint& pos, const QVector<ProjectedAtom>& projected) const;
	void selectAtom(int atom_index, bool additive);
	void selectElement(const QString& symbol);
	void selectBond(int bond_index);
	void clearSelection();
	void completeMeasurementIfReady();
	void startMeasurement(const QString& kind);
	QString atomLabel(const Atom& atom) const;
	QString atomDetails(int atom_index) const;
	QString bondDetails(int bond_index) const;
	void openPeriodicTable(const QString& symbol);
	void notifyChanged();

	QVector<Atom> atoms;
	QVector<Bond> bonds;
	QString source_atom_table;
	QString source_bond_table;
	QVector<Measurement> measurements;
	QVector<int> selected_atoms;
	int selected_bond;
	bool molecule_selected;
	QString selection_mode;
	QString measurement_mode;
	ScientificObjectSettings options;
	float yaw;
	float pitch;
	float zoom;
	QPointF pan;
	QPoint last_mouse_pos;
	bool rotating;
	bool panning;
	class QTimer* spin_timer;
};
