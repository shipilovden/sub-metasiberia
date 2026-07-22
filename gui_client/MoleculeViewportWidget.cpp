/*=====================================================================
MoleculeViewportWidget.cpp
---------------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "MoleculeViewportWidget.h"
#include "PeriodicTable.h"


#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
#include <QtCore5Compat/QRegExp>
#else
#include <QtCore/QRegExp>
#endif
#include <QtCore/QTimer>
#include <QtGui/QClipboard>
#include <QtGui/QContextMenuEvent>
#include <QtGui/QDesktopServices>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainter>
#include <QtGui/QWheelEvent>
#include <QtGui/QVector3D>
#include <QtWidgets/QApplication>
#include <QtWidgets/QMenu>
#include <algorithm>
#include <cmath>

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
#define SUBSTRATA_SKIP_EMPTY_PARTS Qt::SkipEmptyParts
#else
#define SUBSTRATA_SKIP_EMPTY_PARTS QString::SkipEmptyParts
#endif


namespace
{
const double PI = 3.14159265358979323846;


QColor cpkColour(const QString& symbol)
{
	const QString s = symbol.trimmed();
	if(s == QStringLiteral("H")) return QColor(240, 240, 232);
	if(s == QStringLiteral("C")) return QColor(40, 40, 42);
	if(s == QStringLiteral("N")) return QColor(45, 80, 220);
	if(s == QStringLiteral("O")) return QColor(220, 40, 30);
	if(s == QStringLiteral("F") || s == QStringLiteral("Cl")) return QColor(45, 190, 65);
	if(s == QStringLiteral("P")) return QColor(245, 130, 35);
	if(s == QStringLiteral("S")) return QColor(235, 205, 35);
	if(s == QStringLiteral("Br")) return QColor(145, 45, 30);
	if(s == QStringLiteral("I")) return QColor(110, 55, 160);
	return QColor(55, 180, 225);
}


double pointSegmentDistance(const QPointF& p, const QPointF& a, const QPointF& b)
{
	const QPointF ab = b - a;
	const double denom = ab.x() * ab.x() + ab.y() * ab.y();
	if(denom <= 1.0e-9) return std::hypot(p.x() - a.x(), p.y() - a.y());
	const double t = std::max(0.0, std::min(1.0, ((p.x() - a.x()) * ab.x() + (p.y() - a.y()) * ab.y()) / denom));
	const QPointF q = a + ab * t;
	return std::hypot(p.x() - q.x(), p.y() - q.y());
}


double vecLength(const QVector3D& v) { return std::sqrt(QVector3D::dotProduct(v, v)); }


double angleDegrees(const QVector3D& a, const QVector3D& b)
{
	const double d = vecLength(a) * vecLength(b);
	if(d < 1.0e-12) return 0.0;
	return std::acos(std::max(-1.0, std::min(1.0, (double)QVector3D::dotProduct(a, b) / d))) * 180.0 / PI;
}


double torsionDegrees(const QVector3D& p1, const QVector3D& p2, const QVector3D& p3, const QVector3D& p4)
{
	const QVector3D b1 = p2 - p1, b2 = p3 - p2, b3 = p4 - p3;
	QVector3D n1 = QVector3D::crossProduct(b1, b2), n2 = QVector3D::crossProduct(b2, b3);
	if(n1.lengthSquared() < 1.0e-12f || n2.lengthSquared() < 1.0e-12f) return 0.0;
	n1.normalize(); n2.normalize(); QVector3D b2n = b2.normalized();
	const QVector3D m1 = QVector3D::crossProduct(n1, b2n);
	return std::atan2(QVector3D::dotProduct(m1, n2), QVector3D::dotProduct(n1, n2)) * 180.0 / PI;
}
}


struct MoleculeViewportWidget::Atom
{
	int source_id = 0;
	QString element;
	QVector3D pos;
	int formal_charge = 0;
	QString custom_attribute;
};


struct MoleculeViewportWidget::Bond
{
	int atom_a = -1;
	int atom_b = -1;
	int order = 1;
	bool aromatic = false;
	QString stereo;
};


struct MoleculeViewportWidget::Measurement
{
	QString kind;
	QVector<int> atom_indices;
	double value = 0.0;
	QString unit;
};


struct MoleculeViewportWidget::ProjectedAtom
{
	QPointF point;
	float depth = 0.f;
	float radius = 10.f;
};


MoleculeViewportWidget::MoleculeViewportWidget(QWidget* parent)
:	QWidget(parent), selected_bond(-1), molecule_selected(false), selection_mode(QStringLiteral("atom")), yaw(0.45f), pitch(-0.25f), zoom(1.f), pan(0, 0), rotating(false), panning(false), spin_timer(new QTimer(this))
{
	setMinimumHeight(360); setFocusPolicy(Qt::StrongFocus); setMouseTracking(true);
	setToolTip(QString::fromUtf8("ЛКМ: выбор атома/связи; Ctrl+ЛКМ: несколько атомов; ПКМ: контекстное меню; перетаскивание пустого места: вращение; средняя кнопка: панорама; колесо: масштаб."));
	connect(spin_timer, &QTimer::timeout, this, [this]() { yaw += spin_timer->property("step").toFloat(); update(); });
}


MoleculeViewportWidget::~MoleculeViewportWidget() {}


void MoleculeViewportWidget::setMolecule(const QString& atom_table, const QString& bond_table)
{
	if(atom_table == source_atom_table && bond_table == source_bond_table)
		return;
	source_atom_table = atom_table; source_bond_table = bond_table;
	atoms.clear(); bonds.clear(); measurements.clear(); measurement_mode.clear(); selected_atoms.clear(); selected_bond = -1; molecule_selected = false;
	const QStringList atom_lines = atom_table.split(QChar('\n'));
	for(const QString& raw : atom_lines)
	{
		const QStringList p = raw.trimmed().split(QRegularExpression(QStringLiteral("\\s+")), SUBSTRATA_SKIP_EMPTY_PARTS);
		if(p.size() < 5) continue;
		bool ok_id = false, ok_x = false, ok_y = false, ok_z = false;
		Atom a; a.source_id = p[0].toInt(&ok_id); a.element = p[1]; a.pos = QVector3D(p[2].toFloat(&ok_x), p[3].toFloat(&ok_y), p[4].toFloat(&ok_z));
		if(!ok_id || !ok_x || !ok_y || !ok_z) continue;
		if(p.size() > 5) { bool ok_charge = false; a.formal_charge = p[5].toInt(&ok_charge); if(!ok_charge) a.custom_attribute = p.mid(5).join(QStringLiteral(" ")); }
		if(p.size() > 6) a.custom_attribute = p.mid(6).join(QStringLiteral(" "));
		atoms.push_back(a);
	}
	const QStringList bond_lines = bond_table.split(QChar('\n'));
	for(QString raw : bond_lines)
	{
		raw.replace(QChar('-'), QChar(' ')); raw.replace(QChar(':'), QChar(' '));
		const QStringList p = raw.trimmed().split(QRegularExpression(QStringLiteral("\\s+")), SUBSTRATA_SKIP_EMPTY_PARTS); if(p.size() < 2) continue;
		const int source_a = p[0].toInt(), source_b = p[1].toInt(); Bond b;
		for(int i=0; i<atoms.size(); ++i) { if(atoms[i].source_id == source_a) b.atom_a = i; if(atoms[i].source_id == source_b) b.atom_b = i; }
		if(b.atom_a < 0 || b.atom_b < 0 || b.atom_a == b.atom_b) continue;
		if(p.size() > 2) { const QString order = p[2].toLower(); b.order = (order == QStringLiteral("double") || order == QStringLiteral("2")) ? 2 : ((order == QStringLiteral("triple") || order == QStringLiteral("3")) ? 3 : 1); b.aromatic = order == QStringLiteral("aromatic"); }
		if(p.size() > 3) b.stereo = p.mid(3).join(QStringLiteral(" "));
		bonds.push_back(b);
	}
	resetView(); notifyChanged();
}


void MoleculeViewportWidget::setScientificSettings(const ScientificObjectSettings& s) { options = s; update(); }
void MoleculeViewportWidget::setSelectionMode(const QString& mode) { selection_mode = mode; clearSelection(); update(); }
QString MoleculeViewportWidget::selectionMode() const { return selection_mode; }


QString MoleculeViewportWidget::selectionState() const
{
	if(molecule_selected) return QStringLiteral("molecule_selected");
	if(selected_bond >= 0) return QStringLiteral("bond_selected");
	if(selected_atoms.size() > 1) return QStringLiteral("multiple_atoms_selected");
	if(selected_atoms.size() == 1) return QStringLiteral("atom_selected");
	return QStringLiteral("no_selection");
}


QString MoleculeViewportWidget::selectedAtomsText() const
{
	QStringList ids; for(int index : selected_atoms) if(index >= 0 && index < atoms.size()) ids << QString::number(atoms[index].source_id); return ids.join(QChar(','));
}


int MoleculeViewportWidget::selectedBondIndex() const { return selected_bond; }


void MoleculeViewportWidget::setSelectionState(const QString& selected, int bond, const QString& state)
{
	selected_atoms.clear(); for(const QString& id_text : selected.split(QChar(','), SUBSTRATA_SKIP_EMPTY_PARTS)) { const int id = id_text.toInt(); for(int i=0; i<atoms.size(); ++i) if(atoms[i].source_id == id) selected_atoms.push_back(i); }
	selected_bond = bond >= 0 && bond < bonds.size() ? bond : -1; molecule_selected = state == QStringLiteral("molecule_selected"); update();
}


QVector<MoleculeViewportWidget::ProjectedAtom> MoleculeViewportWidget::projectAtoms() const
{
	QVector<ProjectedAtom> projected(atoms.size()); if(atoms.isEmpty()) return projected;
	QVector3D center; for(const Atom& a : atoms) center += a.pos; center /= (float)atoms.size();
	float extent = 0.f; for(const Atom& a : atoms) extent = std::max(extent, (a.pos - center).length()); if(extent < 0.1f) extent = 1.f;
	const float scale = std::min(width(), height()) * 0.36f * zoom / extent;
	const float cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch);
	for(int i=0; i<atoms.size(); ++i)
	{
		QVector3D v = atoms[i].pos - center; const float x1 = cy * v.x() + sy * v.z(), z1 = -sy * v.x() + cy * v.z(); const float y2 = cp * v.y() - sp * z1, z2 = sp * v.y() + cp * z1;
		projected[i].point = QPointF(width() * 0.5 + pan.x() + x1 * scale, height() * 0.5 + pan.y() - y2 * scale);
		projected[i].depth = z2; projected[i].radius = std::max(6.f, std::min(24.f, 10.f * options.atom_radius * std::sqrt(zoom)));
	}
	return projected;
}


int MoleculeViewportWidget::pickAtom(const QPoint& pos, const QVector<ProjectedAtom>& projected) const
{
	int best = -1; double best_d = 1.0e30;
	for(int i=0; i<projected.size(); ++i) { if(!options.show_hydrogen && atoms[i].element == QStringLiteral("H")) continue; const double d = std::hypot(pos.x()-projected[i].point.x(), pos.y()-projected[i].point.y()); if(d <= projected[i].radius + 6.f && d < best_d) best = i, best_d = d; }
	return best;
}


int MoleculeViewportWidget::pickBond(const QPoint& pos, const QVector<ProjectedAtom>& projected) const
{
	int best = -1; double best_d = 1.0e30;
	for(int i=0; i<bonds.size(); ++i) { const Bond& b = bonds[i]; if(!options.show_hydrogen && (atoms[b.atom_a].element == QStringLiteral("H") || atoms[b.atom_b].element == QStringLiteral("H"))) continue; const double d = pointSegmentDistance(pos, projected[b.atom_a].point, projected[b.atom_b].point); if(d < 9.0 && d < best_d) best = i, best_d = d; }
	return best;
}


void MoleculeViewportWidget::selectAtom(int index, bool additive)
{
	if(index < 0 || index >= atoms.size()) return; molecule_selected = false; selected_bond = -1;
	if(!additive && measurement_mode.isEmpty()) selected_atoms.clear();
	if(!selected_atoms.contains(index)) selected_atoms.push_back(index); else if(additive) selected_atoms.removeAll(index);
	completeMeasurementIfReady(); notifyChanged();
}


void MoleculeViewportWidget::selectElement(const QString& symbol) { selected_atoms.clear(); for(int i=0; i<atoms.size(); ++i) if(atoms[i].element.compare(symbol, Qt::CaseInsensitive)==0) selected_atoms.push_back(i); selected_bond=-1; molecule_selected=false; notifyChanged(); }
void MoleculeViewportWidget::selectBond(int index) { selected_atoms.clear(); selected_bond=index; molecule_selected=false; notifyChanged(); }
void MoleculeViewportWidget::clearSelection() { selected_atoms.clear(); selected_bond=-1; molecule_selected=false; notifyChanged(); }


void MoleculeViewportWidget::startMeasurement(const QString& kind) { measurement_mode=kind; selected_atoms.clear(); selected_bond=-1; molecule_selected=false; notifyChanged(); }
void MoleculeViewportWidget::beginMeasurement(const QString& kind) { startMeasurement(kind); }
void MoleculeViewportWidget::clearMeasurements() { measurements.clear(); measurement_mode.clear(); notifyChanged(); }
void MoleculeViewportWidget::setSpinEnabled(bool enabled, float speed) { const float sign = speed < 0.f ? -1.f : 1.f; spin_timer->setProperty("step", sign * std::max(0.001f, std::abs(speed) * 0.005f)); if(enabled) spin_timer->start(16); else spin_timer->stop(); }
void MoleculeViewportWidget::selectAtomBySourceID(int source_id, bool additive) { for(int i=0;i<atoms.size();++i)if(atoms[i].source_id==source_id){selectAtom(i,additive);return;} }


bool MoleculeViewportWidget::handleSceneRay(const Vec4f& origin4, const Vec4f& dir4, bool show_context_menu, bool additive, const QPoint& global_pos)
{
	if(atoms.isEmpty()) return false;
	QVector3D origin(origin4[0],origin4[1],origin4[2]),dir(dir4[0],dir4[1],dir4[2]);if(dir.lengthSquared()<1.0e-12f)return false;dir.normalize();
	QVector3D center;for(const Atom&a:atoms)center+=a.pos;center/=(float)atoms.size();const float coord_scale=std::max(0.01f,options.object_scale)*0.75f;
	auto atomObjectPosition=[&](const Atom& atom){const QVector3D p=(atom.pos-center)*coord_scale;return QVector3D(p.x(),-p.z(),p.y());};
	int atom_hit=-1;float atom_t=1.0e30f;for(int i=0;i<atoms.size();++i){if(!options.show_hydrogen&&atoms[i].element==QStringLiteral("H"))continue;const QVector3D c=atomObjectPosition(atoms[i]);float radius=(QString::fromStdString(options.visualization_mode)==QStringLiteral("space_fill")?0.42f:0.22f)*options.atom_radius;if(atoms[i].element==QStringLiteral("H"))radius*=0.58f;radius=std::max(radius,0.16f);const QVector3D oc=origin-c;const float b=QVector3D::dotProduct(oc,dir),disc=b*b-QVector3D::dotProduct(oc,oc)+radius*radius;if(disc>=0){const float t=-b-std::sqrt(disc);if(t>=0&&t<atom_t){atom_t=t;atom_hit=i;}}}
	int bond_hit=-1;float bond_t=1.0e30f;if(selection_mode==QStringLiteral("bond")||show_context_menu){for(int i=0;i<bonds.size();++i){const Bond&b=bonds[i];const QVector3D a=atomObjectPosition(atoms[b.atom_a]),c=atomObjectPosition(atoms[b.atom_b]),v=c-a,w0=origin-a;const float aa=1.f,bb=QVector3D::dotProduct(dir,v),cc=QVector3D::dotProduct(v,v),dd=QVector3D::dotProduct(dir,w0),ee=QVector3D::dotProduct(v,w0),den=aa*cc-bb*bb;if(den<1.0e-8f)continue;float t=(bb*ee-cc*dd)/den,u=(aa*ee-bb*dd)/den;u=std::max(0.f,std::min(1.f,u));t=std::max(0.f,t);const float distance=(origin+dir*t-(a+v*u)).length();const float radius=std::max(0.05f,options.bond_thickness*0.9f);if(distance<=radius&&t<bond_t){bond_t=t;bond_hit=i;}}}
	if(selection_mode==QStringLiteral("bond")&&bond_hit>=0&&bond_t<=atom_t){selectBond(bond_hit);atom_hit=-1;}else if(atom_hit>=0)selectAtom(atom_hit,additive);else if(bond_hit>=0)selectBond(bond_hit);else return false;
	if(!show_context_menu)return true;
	QMenu menu(this);
	menu.setStyleSheet(QStringLiteral("QMenu{max-width:360px;}"));
	if(atom_hit>=0){const Atom&a=atoms[atom_hit];const PeriodicElementRecord*e=PeriodicTableModel::elementBySymbol(a.element);QAction*title=menu.addAction(QString::fromUtf8("%1 — %2, атом %3").arg(a.element,e?e->name:QString::fromUtf8("элемент")).arg(a.source_id));title->setEnabled(false);menu.addSeparator();QAction*label=menu.addAction(QString::fromUtf8("Показать/скрыть подпись"));QAction*card=menu.addAction(QString::fromUtf8("Показать/скрыть 3D-карточку"));QAction*measure=menu.addAction(QString::fromUtf8("Начать измерение расстояния"));QAction*all=menu.addAction(QString::fromUtf8("Выбрать все атомы %1").arg(atoms[atom_hit].element));QAction*periodic=menu.addAction(QString::fromUtf8("Открыть в периодической таблице"));QAction*chosen=menu.exec(global_pos);if(chosen==label&&actionRequested)actionRequested(QStringLiteral("toggle_labels"),atom_hit);else if(chosen==card&&actionRequested)actionRequested(QStringLiteral("toggle_info_card"),atom_hit);else if(chosen==measure)startMeasurement(QStringLiteral("distance"));else if(chosen==all)selectElement(atoms[atom_hit].element);else if(chosen==periodic)openPeriodicTable(atoms[atom_hit].element);}
	else if(bond_hit>=0){const Bond&b=bonds[bond_hit];QAction*title=menu.addAction(QString::fromUtf8("Связь %1—%2").arg(atoms[b.atom_a].source_id).arg(atoms[b.atom_b].source_id));title->setEnabled(false);menu.addSeparator();QAction*angle=menu.addAction(QString::fromUtf8("Начать измерение угла"));QAction*torsion=menu.addAction(QString::fromUtf8("Начать измерение торсиона"));QAction*chosen=menu.exec(global_pos);if(chosen==angle)startMeasurement(QStringLiteral("angle"));else if(chosen==torsion)startMeasurement(QStringLiteral("torsion"));}
	return true;
}


void MoleculeViewportWidget::completeMeasurementIfReady()
{
	const int needed = measurement_mode == QStringLiteral("distance") ? 2 : (measurement_mode == QStringLiteral("angle") ? 3 : (measurement_mode == QStringLiteral("torsion") ? 4 : 0));
	if(needed == 0 || selected_atoms.size() < needed) return;
	Measurement m; m.kind=measurement_mode; m.atom_indices=selected_atoms.mid(selected_atoms.size()-needed, needed);
	if(needed == 2) { m.value=(atoms[m.atom_indices[0]].pos-atoms[m.atom_indices[1]].pos).length(); m.unit=QString::fromUtf8("Å"); }
	else if(needed == 3) { const QVector3D p0=atoms[m.atom_indices[0]].pos, p1=atoms[m.atom_indices[1]].pos, p2=atoms[m.atom_indices[2]].pos; m.value=angleDegrees(p0-p1,p2-p1); m.unit=QString::fromUtf8("°"); }
	else { m.value=torsionDegrees(atoms[m.atom_indices[0]].pos,atoms[m.atom_indices[1]].pos,atoms[m.atom_indices[2]].pos,atoms[m.atom_indices[3]].pos); m.unit=QString::fromUtf8("°"); }
	measurements.push_back(m); measurement_mode.clear(); notifyChanged();
}


QString MoleculeViewportWidget::measurementsJson() const
{
	QJsonArray array;
	for(const Measurement& m : measurements)
	{
		QJsonObject o;
		o.insert(QStringLiteral("kind"), m.kind);
		QJsonArray ids;
		bool valid = true;
		for(int i : m.atom_indices)
		{
			if(i < 0 || i >= atoms.size())
			{
				valid = false;
				break;
			}
			ids.append(atoms[i].source_id);
		}
		if(!valid || ids.isEmpty())
			continue;
		o.insert(QStringLiteral("atoms"), ids);
		o.insert(QStringLiteral("value"), m.value);
		o.insert(QStringLiteral("unit"), m.unit);
		array.append(o);
	}
	return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}


void MoleculeViewportWidget::setMeasurementsJson(const QString& json)
{
	measurements.clear(); const QJsonDocument doc=QJsonDocument::fromJson(json.toUtf8()); if(!doc.isArray()) { update(); return; }
	for(const QJsonValue& v:doc.array()) { const QJsonObject o=v.toObject(); Measurement m; m.kind=o.value(QStringLiteral("kind")).toString(); m.value=o.value(QStringLiteral("value")).toDouble(); m.unit=o.value(QStringLiteral("unit")).toString(); for(const QJsonValue& idv:o.value(QStringLiteral("atoms")).toArray()) for(int i=0;i<atoms.size();++i) if(atoms[i].source_id==idv.toInt()) m.atom_indices.push_back(i); if(!m.atom_indices.isEmpty()) measurements.push_back(m); } update();
}


QString MoleculeViewportWidget::atomLabel(const Atom& a) const
{
	const PeriodicElementRecord* e=PeriodicTableModel::elementBySymbol(a.element); const QString mode=QString::fromStdString(options.label_mode);
	if(mode==QStringLiteral("atom_number")) return QString::number(a.source_id);
	if(mode==QStringLiteral("element_number")) return a.element+QString::number(a.source_id);
	if(mode==QStringLiteral("atomic_number")) return e?QString::number(e->atomic_number):QStringLiteral("?");
	if(mode==QStringLiteral("atomic_mass")) return e?e->atomic_mass:QStringLiteral("Not available");
	if(mode==QStringLiteral("formal_charge")) return a.formal_charge==0?QStringLiteral("0"):QString::number(a.formal_charge);
	if(mode==QStringLiteral("custom_attribute")) return a.custom_attribute.isEmpty()?QStringLiteral("Not available"):a.custom_attribute;
	return a.element;
}


QString MoleculeViewportWidget::atomDetails(int i) const
{
	if(i<0||i>=atoms.size()) return QString(); const Atom&a=atoms[i]; const PeriodicElementRecord*e=PeriodicTableModel::elementBySymbol(a.element); QStringList connected; for(const Bond&b:bonds) { if(b.atom_a==i) connected<<QString::number(atoms[b.atom_b].source_id); if(b.atom_b==i) connected<<QString::number(atoms[b.atom_a].source_id); }
	return QString::fromUtf8("%1 — %2\nAtom index: %3\nAtomic number: %4\nAtomic mass: %5\nFormal charge: %6\nCoordinates: %7, %8, %9 Å\nConnected atoms: %10")
		.arg(a.element,e?e->name:QStringLiteral("Unknown element")).arg(a.source_id).arg(e?QString::number(e->atomic_number):QStringLiteral("Not available")).arg(e?e->atomic_mass:QStringLiteral("Not available")).arg(a.formal_charge).arg(a.pos.x(),0,'f',4).arg(a.pos.y(),0,'f',4).arg(a.pos.z(),0,'f',4).arg(connected.isEmpty()?QStringLiteral("None"):connected.join(QStringLiteral(", ")));
}


QString MoleculeViewportWidget::bondDetails(int i) const
{
	if(i<0||i>=bonds.size()) return QString(); const Bond&b=bonds[i]; const double length=(atoms[b.atom_a].pos-atoms[b.atom_b].pos).length();
	return QString::fromUtf8("Atoms: %1—%2\nBond order: %3\nLength: %4 Å\nAromatic: %5\nStereo: %6").arg(atoms[b.atom_a].source_id).arg(atoms[b.atom_b].source_id).arg(b.aromatic?QStringLiteral("aromatic"):QString::number(b.order)).arg(length,0,'f',4).arg(b.aromatic?QStringLiteral("yes"):QStringLiteral("no")).arg(b.stereo.isEmpty()?QStringLiteral("Not available"):b.stereo);
}


QString MoleculeViewportWidget::moleculeMetricsText() const
{
	if(atoms.isEmpty()) return QStringLiteral("Not available"); QVector3D minv=atoms[0].pos,maxv=atoms[0].pos,weighted; double total_mass=0,max_extent=0;
	for(const Atom&a:atoms) { minv.setX(std::min(minv.x(),a.pos.x()));minv.setY(std::min(minv.y(),a.pos.y()));minv.setZ(std::min(minv.z(),a.pos.z()));maxv.setX(std::max(maxv.x(),a.pos.x()));maxv.setY(std::max(maxv.y(),a.pos.y()));maxv.setZ(std::max(maxv.z(),a.pos.z())); const PeriodicElementRecord*e=PeriodicTableModel::elementBySymbol(a.element); bool ok=false; double mass=e?QString(e->atomic_mass).remove('[').remove(']').toDouble(&ok):0; if(ok){weighted+=a.pos*(float)mass;total_mass+=mass;} }
	for(int i=0;i<atoms.size();++i)for(int j=i+1;j<atoms.size();++j)max_extent=std::max(max_extent,(double)(atoms[i].pos-atoms[j].pos).length()); const QVector3D com=total_mass>0?weighted/(float)total_mass:QVector3D(); const QVector3D dim=maxv-minv;
	return QString::fromUtf8("Center of mass: %1, %2, %3 Å\nBounding dimensions: %4 × %5 × %6 Å\nMaximum extent: %7 Å\nMolecular mass: %8 Da")
		.arg(com.x(),0,'f',4).arg(com.y(),0,'f',4).arg(com.z(),0,'f',4).arg(dim.x(),0,'f',4).arg(dim.y(),0,'f',4).arg(dim.z(),0,'f',4).arg(max_extent,0,'f',4).arg(total_mass,0,'f',5);
}


void MoleculeViewportWidget::paintEvent(QPaintEvent*)
{
	QPainter p(this);p.setRenderHint(QPainter::Antialiasing,true);p.fillRect(rect(),QColor(24,29,36)); const QVector<ProjectedAtom> pr=projectAtoms();
	if(atoms.isEmpty()){p.setPen(Qt::lightGray);p.drawText(rect(),Qt::AlignCenter,QStringLiteral("No molecule structure loaded"));return;}
	for(int i=0;i<bonds.size();++i){const Bond&b=bonds[i];if(!options.show_hydrogen&&(atoms[b.atom_a].element==QStringLiteral("H")||atoms[b.atom_b].element==QStringLiteral("H")))continue;QPen pen(i==selected_bond?QColor(255,205,55):QColor(155,160,168),i==selected_bond?7.0:std::max(2.0,(double)options.bond_thickness*25.0),Qt::SolidLine,Qt::RoundCap);p.setPen(pen);p.drawLine(pr[b.atom_a].point,pr[b.atom_b].point);if(b.order>1){QPointF d=pr[b.atom_b].point-pr[b.atom_a].point;double len=std::hypot(d.x(),d.y());if(len>1){QPointF n(-d.y()/len*4,d.x()/len*4);p.drawLine(pr[b.atom_a].point+n,pr[b.atom_b].point+n);}}
	}
	QVector<int> order;for(int i=0;i<atoms.size();++i)order<<i;std::sort(order.begin(),order.end(),[&](int a,int b){return pr[a].depth<pr[b].depth;});
	int labels=0;const bool labels_visible=options.show_labels&&options.label_max_count>0&&(options.label_max_distance<=0.f||10.f/zoom<=options.label_max_distance);
	for(int i:order){if(!options.show_hydrogen&&atoms[i].element==QStringLiteral("H"))continue;QColor c=QString::fromStdString(options.colour_scheme).compare(QStringLiteral("CPK"),Qt::CaseInsensitive)==0?cpkColour(atoms[i].element):QColor((int)(options.display_colour.r*255),(int)(options.display_colour.g*255),(int)(options.display_colour.b*255));const bool sel=selected_atoms.contains(i);p.setBrush(c);p.setPen(QPen(sel?QColor(255,215,65):c.lighter(150),sel?4:1));p.drawEllipse(pr[i].point,pr[i].radius,pr[i].radius);if(labels_visible&&labels++<options.label_max_count){QFont f=p.font();f.setPointSizeF(9.0);f.setBold(true);p.setFont(f);p.setPen(QColor((int)(options.label_colour.r*255),(int)(options.label_colour.g*255),(int)(options.label_colour.b*255)));p.drawText(pr[i].point+QPointF(pr[i].radius+3,-pr[i].radius-2),atomLabel(atoms[i]));}}
	for(const Measurement&m:measurements){if(m.atom_indices.size()<2)continue;p.setPen(QPen(QColor(65,220,220),2,Qt::DashLine));for(int j=1;j<m.atom_indices.size();++j)p.drawLine(pr[m.atom_indices[j-1]].point,pr[m.atom_indices[j]].point);QPointF mid;for(int i:m.atom_indices)mid+=pr[i].point;mid/=m.atom_indices.size();p.setPen(Qt::white);p.drawText(mid+QPointF(6,-6),QStringLiteral("%1 %2").arg(m.value,0,'f',3).arg(m.unit));}
	if(options.show_legend){QMap<QString,int>counts;for(const Atom&a:atoms)if(options.show_hydrogen||a.element!=QStringLiteral("H"))counts[a.element]++;int y=28;int box_w=0;for(auto it=counts.constBegin();it!=counts.constEnd();++it){const PeriodicElementRecord*e=PeriodicTableModel::elementBySymbol(it.key());box_w=std::max(box_w,p.fontMetrics().horizontalAdvance(QStringLiteral("%1 — %2 — %3").arg(it.key(),e?e->name:QStringLiteral("Unknown")).arg(it.value())));}QRect box(width()-box_w-60,10,box_w+48,counts.size()*22+12);p.fillRect(box,QColor(0,0,0,165));for(auto it=counts.constBegin();it!=counts.constEnd();++it){const PeriodicElementRecord*e=PeriodicTableModel::elementBySymbol(it.key());p.setBrush(cpkColour(it.key()));p.setPen(Qt::NoPen);p.drawEllipse(QPointF(box.left()+14,y-5),6,6);p.setPen(Qt::white);p.drawText(box.left()+27,y,QStringLiteral("%1 — %2 — %3").arg(it.key(),e?e->name:QStringLiteral("Unknown")).arg(it.value()));y+=22;}}
	p.setPen(QColor(210,215,220));p.drawText(10,height()-12,QStringLiteral("Selection: %1%2").arg(selectionState(),measurement_mode.isEmpty()?QString():QStringLiteral(" | measurement: %1 (%2 atoms selected)").arg(measurement_mode).arg(selected_atoms.size())));
}


void MoleculeViewportWidget::mousePressEvent(QMouseEvent*e)
{
	last_mouse_pos=e->pos();if(e->button()==Qt::MiddleButton){panning=true;e->accept();return;}if(e->button()!=Qt::LeftButton){QWidget::mousePressEvent(e);return;}const QVector<ProjectedAtom>pr=projectAtoms();const int atom=pickAtom(e->pos(),pr),bond=pickBond(e->pos(),pr);if(atom>=0&&(selection_mode==QStringLiteral("atom")||!measurement_mode.isEmpty()))selectAtom(atom,(e->modifiers()&Qt::ControlModifier)||!measurement_mode.isEmpty());else if(bond>=0&&selection_mode==QStringLiteral("bond"))selectBond(bond);else if(selection_mode==QStringLiteral("molecule")){selected_atoms.clear();selected_bond=-1;molecule_selected=true;notifyChanged();}else if(atom<0&&bond<0){rotating=true;}e->accept();
}


void MoleculeViewportWidget::mouseMoveEvent(QMouseEvent*e){const QPoint d=e->pos()-last_mouse_pos;if(rotating){yaw+=d.x()*0.01f;pitch=std::max(-1.45f,std::min(1.45f,pitch+d.y()*0.01f));update();}if(panning){pan+=QPointF(d);update();}last_mouse_pos=e->pos();}
void MoleculeViewportWidget::mouseReleaseEvent(QMouseEvent*e){Q_UNUSED(e);rotating=false;panning=false;}
void MoleculeViewportWidget::wheelEvent(QWheelEvent*e){zoom=std::max(0.15f,std::min(12.f,zoom*std::pow(1.0015f,(float)e->angleDelta().y())));update();e->accept();}


void MoleculeViewportWidget::openPeriodicTable(const QString& symbol)
{
	PeriodicTableWidget*w=new PeriodicTableWidget();w->setAttribute(Qt::WA_DeleteOnClose);QStringList symbols;for(const Atom&a:atoms)if(!symbols.contains(a.element))symbols<<a.element;w->setHighlightedSymbols(symbols);w->selectElement(symbol);w->elementActivated=[this](const QString&s){selectElement(s);};w->show();w->raise();
}


void MoleculeViewportWidget::contextMenuEvent(QContextMenuEvent*e)
{
	const QVector<ProjectedAtom>pr=projectAtoms();const int atom=pickAtom(e->pos(),pr),bond=pickBond(e->pos(),pr);QMenu menu(this);
	menu.setStyleSheet(QStringLiteral("QMenu{max-width:360px;}"));
	if(atom>=0){const Atom&a=atoms[atom];const PeriodicElementRecord*el=PeriodicTableModel::elementBySymbol(a.element);QAction*title=menu.addAction(QString::fromUtf8("%1 — %2, атом %3").arg(a.element,el?el->name:QString::fromUtf8("элемент")).arg(a.source_id));title->setEnabled(false);menu.addSeparator();QAction*toggle=menu.addAction(QString::fromUtf8("Показать/скрыть подпись"));QAction*card=menu.addAction(QString::fromUtf8("Показать/скрыть 3D-карточку"));QAction*center=menu.addAction(QString::fromUtf8("Центрировать атом"));QAction*measure=menu.addAction(QString::fromUtf8("Начать измерение расстояния"));QAction*all=menu.addAction(QString::fromUtf8("Выбрать все атомы %1").arg(atoms[atom].element));QAction*periodic=menu.addAction(QString::fromUtf8("Открыть в периодической таблице"));QAction*chosen=menu.exec(e->globalPos());if(chosen==toggle){if(actionRequested)actionRequested(QStringLiteral("toggle_labels"),atom);}else if(chosen==card){if(actionRequested)actionRequested(QStringLiteral("toggle_info_card"),atom);}else if(chosen==center){pan+=QPointF(width()*0.5,height()*0.5)-pr[atom].point;zoom=std::max(zoom,2.f);update();}else if(chosen==measure)startMeasurement(QStringLiteral("distance"));else if(chosen==all)selectElement(atoms[atom].element);else if(chosen==periodic)openPeriodicTable(atoms[atom].element);return;}
	if(bond>=0){const Bond&b=bonds[bond];QAction*title=menu.addAction(QString::fromUtf8("Связь %1—%2").arg(atoms[b.atom_a].source_id).arg(atoms[b.atom_b].source_id));title->setEnabled(false);menu.addSeparator();QAction*angle=menu.addAction(QString::fromUtf8("Начать измерение угла"));QAction*torsion=menu.addAction(QString::fromUtf8("Начать измерение торсиона"));QAction*chosen=menu.exec(e->globalPos());if(chosen==angle)startMeasurement(QStringLiteral("angle"));else if(chosen==torsion)startMeasurement(QStringLiteral("torsion"));return;}
	QAction*card=menu.addAction(QString::fromUtf8("Открыть карточку молекулы"));QAction*pubchem=menu.addAction(QString::fromUtf8("Открыть страницу PubChem"));QAction*properties=menu.addAction(QString::fromUtf8("Свойства"));QAction*classifications=menu.addAction(QString::fromUtf8("Классификации"));QAction*images=menu.addAction(QString::fromUtf8("Изображения"));QAction*favorite=menu.addAction(QString::fromUtf8("В избранное / убрать"));menu.addSeparator();QAction*copycid=menu.addAction(QStringLiteral("Copy CID"));QAction*copysmiles=menu.addAction(QStringLiteral("Copy SMILES"));QAction*copyinchi=menu.addAction(QStringLiteral("Copy InChI"));menu.addSeparator();QAction*exporta=menu.addAction(QString::fromUtf8("Экспорт"));QAction*refresh=menu.addAction(QString::fromUtf8("Обновить"));QAction*deletea=menu.addAction(QString::fromUtf8("Удалить молекулу"));QAction*chosen=menu.exec(e->globalPos());
	if(!chosen)return;QString action;if(chosen==card)action=QStringLiteral("card");else if(chosen==pubchem)action=QStringLiteral("pubchem");else if(chosen==properties)action=QStringLiteral("properties");else if(chosen==classifications)action=QStringLiteral("classification");else if(chosen==images)action=QStringLiteral("images");else if(chosen==favorite)action=QStringLiteral("favorite");else if(chosen==copycid)action=QStringLiteral("copy_cid");else if(chosen==copysmiles)action=QStringLiteral("copy_smiles");else if(chosen==copyinchi)action=QStringLiteral("copy_inchi");else if(chosen==exporta)action=QStringLiteral("export");else if(chosen==refresh)action=QStringLiteral("refresh");else if(chosen==deletea)action=QStringLiteral("delete");if(actionRequested)actionRequested(action,-1);
}


void MoleculeViewportWidget::resetView(){yaw=0.45f;pitch=-0.25f;zoom=1.f;pan=QPointF();update();}
void MoleculeViewportWidget::notifyChanged(){update();if(stateChanged)stateChanged();}
