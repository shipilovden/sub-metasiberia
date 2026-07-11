/*=====================================================================
PeriodicTable.cpp
------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "PeriodicTable.h"


#include <QtCore/QSet>
#include <QtGui/QPainter>
#include <QtGui/QBrush>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLabel>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QTableView>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QVBoxLayout>
#include <algorithm>
#include <cmath>


namespace
{
struct ElementSeed
{
	const char* symbol;
	const char* name;
	const char* mass;
	int group;
	int period;
	const char* category;
};


const ElementSeed element_seeds[] = {
	{"H","Hydrogen","1.008",1,1,"nonmetal"},{"He","Helium","4.002602",18,1,"noble gas"},
	{"Li","Lithium","6.94",1,2,"alkali metal"},{"Be","Beryllium","9.0121831",2,2,"alkaline earth metal"},{"B","Boron","10.81",13,2,"metalloid"},{"C","Carbon","12.011",14,2,"nonmetal"},{"N","Nitrogen","14.007",15,2,"nonmetal"},{"O","Oxygen","15.999",16,2,"nonmetal"},{"F","Fluorine","18.998403163",17,2,"halogen"},{"Ne","Neon","20.1797",18,2,"noble gas"},
	{"Na","Sodium","22.98976928",1,3,"alkali metal"},{"Mg","Magnesium","24.305",2,3,"alkaline earth metal"},{"Al","Aluminium","26.9815385",13,3,"post-transition metal"},{"Si","Silicon","28.085",14,3,"metalloid"},{"P","Phosphorus","30.973761998",15,3,"nonmetal"},{"S","Sulfur","32.06",16,3,"nonmetal"},{"Cl","Chlorine","35.45",17,3,"halogen"},{"Ar","Argon","39.948",18,3,"noble gas"},
	{"K","Potassium","39.0983",1,4,"alkali metal"},{"Ca","Calcium","40.078",2,4,"alkaline earth metal"},{"Sc","Scandium","44.955908",3,4,"transition metal"},{"Ti","Titanium","47.867",4,4,"transition metal"},{"V","Vanadium","50.9415",5,4,"transition metal"},{"Cr","Chromium","51.9961",6,4,"transition metal"},{"Mn","Manganese","54.938044",7,4,"transition metal"},{"Fe","Iron","55.845",8,4,"transition metal"},{"Co","Cobalt","58.933194",9,4,"transition metal"},{"Ni","Nickel","58.6934",10,4,"transition metal"},{"Cu","Copper","63.546",11,4,"transition metal"},{"Zn","Zinc","65.38",12,4,"transition metal"},{"Ga","Gallium","69.723",13,4,"post-transition metal"},{"Ge","Germanium","72.630",14,4,"metalloid"},{"As","Arsenic","74.921595",15,4,"metalloid"},{"Se","Selenium","78.971",16,4,"nonmetal"},{"Br","Bromine","79.904",17,4,"halogen"},{"Kr","Krypton","83.798",18,4,"noble gas"},
	{"Rb","Rubidium","85.4678",1,5,"alkali metal"},{"Sr","Strontium","87.62",2,5,"alkaline earth metal"},{"Y","Yttrium","88.90584",3,5,"transition metal"},{"Zr","Zirconium","91.224",4,5,"transition metal"},{"Nb","Niobium","92.90637",5,5,"transition metal"},{"Mo","Molybdenum","95.95",6,5,"transition metal"},{"Tc","Technetium","[98]",7,5,"transition metal"},{"Ru","Ruthenium","101.07",8,5,"transition metal"},{"Rh","Rhodium","102.90550",9,5,"transition metal"},{"Pd","Palladium","106.42",10,5,"transition metal"},{"Ag","Silver","107.8682",11,5,"transition metal"},{"Cd","Cadmium","112.414",12,5,"transition metal"},{"In","Indium","114.818",13,5,"post-transition metal"},{"Sn","Tin","118.710",14,5,"post-transition metal"},{"Sb","Antimony","121.760",15,5,"metalloid"},{"Te","Tellurium","127.60",16,5,"metalloid"},{"I","Iodine","126.90447",17,5,"halogen"},{"Xe","Xenon","131.293",18,5,"noble gas"},
	{"Cs","Caesium","132.90545196",1,6,"alkali metal"},{"Ba","Barium","137.327",2,6,"alkaline earth metal"},{"La","Lanthanum","138.90547",3,6,"lanthanide"},{"Ce","Cerium","140.116",0,6,"lanthanide"},{"Pr","Praseodymium","140.90766",0,6,"lanthanide"},{"Nd","Neodymium","144.242",0,6,"lanthanide"},{"Pm","Promethium","[145]",0,6,"lanthanide"},{"Sm","Samarium","150.36",0,6,"lanthanide"},{"Eu","Europium","151.964",0,6,"lanthanide"},{"Gd","Gadolinium","157.25",0,6,"lanthanide"},{"Tb","Terbium","158.92535",0,6,"lanthanide"},{"Dy","Dysprosium","162.500",0,6,"lanthanide"},{"Ho","Holmium","164.93033",0,6,"lanthanide"},{"Er","Erbium","167.259",0,6,"lanthanide"},{"Tm","Thulium","168.93422",0,6,"lanthanide"},{"Yb","Ytterbium","173.045",0,6,"lanthanide"},{"Lu","Lutetium","174.9668",3,6,"lanthanide"},{"Hf","Hafnium","178.49",4,6,"transition metal"},{"Ta","Tantalum","180.94788",5,6,"transition metal"},{"W","Tungsten","183.84",6,6,"transition metal"},{"Re","Rhenium","186.207",7,6,"transition metal"},{"Os","Osmium","190.23",8,6,"transition metal"},{"Ir","Iridium","192.217",9,6,"transition metal"},{"Pt","Platinum","195.084",10,6,"transition metal"},{"Au","Gold","196.966569",11,6,"transition metal"},{"Hg","Mercury","200.592",12,6,"transition metal"},{"Tl","Thallium","204.38",13,6,"post-transition metal"},{"Pb","Lead","207.2",14,6,"post-transition metal"},{"Bi","Bismuth","208.98040",15,6,"post-transition metal"},{"Po","Polonium","[209]",16,6,"post-transition metal"},{"At","Astatine","[210]",17,6,"halogen"},{"Rn","Radon","[222]",18,6,"noble gas"},
	{"Fr","Francium","[223]",1,7,"alkali metal"},{"Ra","Radium","[226]",2,7,"alkaline earth metal"},{"Ac","Actinium","[227]",3,7,"actinide"},{"Th","Thorium","232.0377",0,7,"actinide"},{"Pa","Protactinium","231.03588",0,7,"actinide"},{"U","Uranium","238.02891",0,7,"actinide"},{"Np","Neptunium","[237]",0,7,"actinide"},{"Pu","Plutonium","[244]",0,7,"actinide"},{"Am","Americium","[243]",0,7,"actinide"},{"Cm","Curium","[247]",0,7,"actinide"},{"Bk","Berkelium","[247]",0,7,"actinide"},{"Cf","Californium","[251]",0,7,"actinide"},{"Es","Einsteinium","[252]",0,7,"actinide"},{"Fm","Fermium","[257]",0,7,"actinide"},{"Md","Mendelevium","[258]",0,7,"actinide"},{"No","Nobelium","[259]",0,7,"actinide"},{"Lr","Lawrencium","[266]",3,7,"actinide"},{"Rf","Rutherfordium","[267]",4,7,"transition metal"},{"Db","Dubnium","[268]",5,7,"transition metal"},{"Sg","Seaborgium","[269]",6,7,"transition metal"},{"Bh","Bohrium","[270]",7,7,"transition metal"},{"Hs","Hassium","[269]",8,7,"transition metal"},{"Mt","Meitnerium","[278]",9,7,"unknown"},{"Ds","Darmstadtium","[281]",10,7,"unknown"},{"Rg","Roentgenium","[282]",11,7,"unknown"},{"Cn","Copernicium","[285]",12,7,"transition metal"},{"Nh","Nihonium","[286]",13,7,"unknown"},{"Fl","Flerovium","[289]",14,7,"post-transition metal"},{"Mc","Moscovium","[290]",15,7,"unknown"},{"Lv","Livermorium","[293]",16,7,"unknown"},{"Ts","Tennessine","[294]",17,7,"halogen"},{"Og","Oganesson","[294]",18,7,"noble gas"}
};


QString knownProperty(const QString& symbol, const char* property)
{
	struct CommonData { const char* symbol; const char* config; const char* density; const char* melt; const char* boil; const char* en; const char* radius; };
	static const CommonData common[] = {
		{"H","1s1","0.08988 g/L","13.99 K","20.271 K","2.20","53 pm"},
		{"C","[He] 2s2 2p2","2.267 g/cm3","3823 K (sublimes)","Not available","2.55","67 pm"},
		{"N","[He] 2s2 2p3","1.2506 g/L","63.15 K","77.355 K","3.04","56 pm"},
		{"O","[He] 2s2 2p4","1.429 g/L","54.36 K","90.188 K","3.44","48 pm"},
		{"F","[He] 2s2 2p5","1.696 g/L","53.48 K","85.03 K","3.98","42 pm"},
		{"P","[Ne] 3s2 3p3","1.823 g/cm3","317.30 K","553.65 K","2.19","98 pm"},
		{"S","[Ne] 3s2 3p4","2.067 g/cm3","388.36 K","717.8 K","2.58","88 pm"},
		{"Cl","[Ne] 3s2 3p5","3.2 g/L","171.6 K","239.11 K","3.16","79 pm"}
	};
	for(size_t i=0; i<sizeof(common)/sizeof(common[0]); ++i)
		if(symbol == QString::fromLatin1(common[i].symbol))
		{
			if(qstrcmp(property, "config") == 0) return QString::fromLatin1(common[i].config);
			if(qstrcmp(property, "density") == 0) return QString::fromLatin1(common[i].density);
			if(qstrcmp(property, "melt") == 0) return QString::fromLatin1(common[i].melt);
			if(qstrcmp(property, "boil") == 0) return QString::fromLatin1(common[i].boil);
			if(qstrcmp(property, "en") == 0) return QString::fromLatin1(common[i].en);
			if(qstrcmp(property, "radius") == 0) return QString::fromLatin1(common[i].radius);
		}
	return QStringLiteral("Not available");
}


QColor categoryColour(const QString& category)
{
	if(category.contains(QStringLiteral("noble"))) return QColor(100, 170, 225);
	if(category.contains(QStringLiteral("halogen"))) return QColor(90, 205, 130);
	if(category.contains(QStringLiteral("alkali "))) return QColor(225, 120, 90);
	if(category.contains(QStringLiteral("alkaline"))) return QColor(235, 170, 90);
	if(category.contains(QStringLiteral("transition"))) return QColor(205, 150, 205);
	if(category.contains(QStringLiteral("lanthanide"))) return QColor(205, 175, 110);
	if(category.contains(QStringLiteral("actinide"))) return QColor(205, 135, 110);
	if(category.contains(QStringLiteral("metalloid"))) return QColor(95, 190, 175);
	return QColor(145, 175, 195);
}
}


PeriodicTableModel::PeriodicTableModel(QObject* parent) : QAbstractTableModel(parent) {}


const QVector<PeriodicElementRecord>& PeriodicTableModel::allElements()
{
	static QVector<PeriodicElementRecord> records;
	if(records.isEmpty())
	{
		for(size_t i=0; i<sizeof(element_seeds)/sizeof(element_seeds[0]); ++i)
		{
			const ElementSeed& seed = element_seeds[i];
			PeriodicElementRecord r;
			r.atomic_number = (int)i + 1;
			r.symbol = QString::fromLatin1(seed.symbol);
			r.name = QString::fromLatin1(seed.name);
			r.atomic_mass = QString::fromLatin1(seed.mass);
			r.group = seed.group;
			r.period = seed.period;
			r.category = QString::fromLatin1(seed.category);
			r.electron_configuration = knownProperty(r.symbol, "config");
			r.density = knownProperty(r.symbol, "density");
			r.melting_point = knownProperty(r.symbol, "melt");
			r.boiling_point = knownProperty(r.symbol, "boil");
			r.electronegativity = knownProperty(r.symbol, "en");
			r.atomic_radius = knownProperty(r.symbol, "radius");
			r.source = QStringLiteral("IUPAC element names/atomic weights; common physical properties: NIST reference values. Unavailable values are not inferred.");
			records.push_back(r);
		}
	}
	return records;
}


const PeriodicElementRecord* PeriodicTableModel::elementBySymbol(const QString& symbol)
{
	const QVector<PeriodicElementRecord>& records = allElements();
	for(int i=0; i<records.size(); ++i)
		if(records[i].symbol.compare(symbol.trimmed(), Qt::CaseInsensitive) == 0)
			return &records[i];
	return 0;
}


int PeriodicTableModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : allElements().size(); }
int PeriodicTableModel::columnCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : 13; }


QVariant PeriodicTableModel::data(const QModelIndex& index, int role) const
{
	if(!index.isValid() || index.row() < 0 || index.row() >= allElements().size()) return QVariant();
	const PeriodicElementRecord& r = allElements()[index.row()];
	if(role == Qt::BackgroundRole) return categoryColour(r.category).lighter(160);
	if(role != Qt::DisplayRole && role != Qt::ToolTipRole) return QVariant();
	if(role == Qt::ToolTipRole)
		return QStringLiteral("%1 (%2)\nCategory: %3\nSource: %4").arg(r.name, r.symbol, r.category, r.source);
	switch(index.column())
	{
	case 0: return r.atomic_number; case 1: return r.symbol; case 2: return r.name; case 3: return r.atomic_mass;
	case 4: return r.group > 0 ? QVariant(r.group) : QVariant(QStringLiteral("f-block")); case 5: return r.period; case 6: return r.category;
	case 7: return r.electron_configuration; case 8: return r.density; case 9: return r.melting_point; case 10: return r.boiling_point;
	case 11: return r.electronegativity; case 12: return r.atomic_radius;
	default: return QVariant();
	}
}


QVariant PeriodicTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
	if(role != Qt::DisplayRole) return QVariant();
	if(orientation == Qt::Vertical) return section + 1;
	static const char* headers[] = {"Z","Symbol","Name","Atomic mass","Group","Period","Category","Electron configuration","Density","Melting point","Boiling point","Electronegativity","Atomic radius"};
	return section >= 0 && section < 13 ? QString::fromLatin1(headers[section]) : QVariant();
}


const PeriodicElementRecord* PeriodicTableModel::elementAt(int row) const
{
	return row >= 0 && row < allElements().size() ? &allElements()[row] : 0;
}


PeriodicTable3DVisualizer::PeriodicTable3DVisualizer(QWidget* parent) : QWidget(parent), property_name(QStringLiteral("Atomic number"))
{
	setMinimumHeight(300);
	setToolTip(QStringLiteral("Native 3D-style property graph. Bar height currently represents atomic number; unavailable scientific values are not invented."));
}


void PeriodicTable3DVisualizer::setHighlightedSymbols(const QStringList& symbols) { highlighted_symbols = symbols; update(); }
void PeriodicTable3DVisualizer::setPropertyName(const QString& name) { property_name = name; update(); }


void PeriodicTable3DVisualizer::paintEvent(QPaintEvent*)
{
	QPainter p(this); p.setRenderHint(QPainter::Antialiasing, true); p.fillRect(rect(), palette().base());
	p.setPen(palette().text().color()); p.drawText(12, 20, QStringLiteral("3D property graph — %1").arg(property_name));
	const QVector<PeriodicElementRecord>& records = PeriodicTableModel::allElements();
	const float cell_w = std::max(8.f, (width() - 30.f) / 18.f);
	const float cell_h = std::max(22.f, (height() - 60.f) / 7.f);
	for(int i=0; i<records.size(); ++i)
	{
		const PeriodicElementRecord& r = records[i];
		int col = r.group > 0 ? r.group - 1 : ((r.category == QStringLiteral("lanthanide") ? (r.atomic_number - 57) : (r.atomic_number - 89)) + 2);
		int row = r.group > 0 ? r.period - 1 : (r.category == QStringLiteral("lanthanide") ? 5 : 6);
		const float h = 4.f + 18.f * std::sqrt((float)r.atomic_number / 118.f);
		QRectF bar(15.f + col * cell_w, 40.f + row * cell_h + (cell_h - h), cell_w - 2.f, h);
		QColor c = categoryColour(r.category);
		if(highlighted_symbols.contains(r.symbol, Qt::CaseInsensitive)) c = QColor(255, 215, 70);
		p.fillRect(bar, c); p.setPen(c.darker(150)); p.drawRect(bar); p.setPen(palette().text().color());
		if(cell_w >= 17.f) p.drawText(QRectF(bar.left(), bar.top() - 13.f, bar.width(), 13.f), Qt::AlignCenter, r.symbol);
	}
}


PeriodicTableWidget::PeriodicTableWidget(QWidget* parent) : QWidget(parent), model(new PeriodicTableModel(this)), table_view(new QTableView(this)), list_widget(new QListWidget(this)), graph_widget(new PeriodicTable3DVisualizer(this))
{
	setWindowTitle(QStringLiteral("MetaSiberia — Periodic Table (118 elements)")); setMinimumSize(900, 520);
	QVBoxLayout* layout = new QVBoxLayout(this);
	QLabel* status = new QLabel(QStringLiteral("Native Qt periodic table. Physical fields show Not available when this module has no sourced value."), this); status->setWordWrap(true); layout->addWidget(status);
	QTabWidget* tabs = new QTabWidget(this); layout->addWidget(tabs);
	table_view->setModel(model); table_view->setSelectionBehavior(QAbstractItemView::SelectRows); table_view->setSelectionMode(QAbstractItemView::SingleSelection); table_view->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents); tabs->addTab(table_view, QStringLiteral("Table"));
	for(const PeriodicElementRecord& r : PeriodicTableModel::allElements())
	{
		QListWidgetItem* item = new QListWidgetItem(QStringLiteral("%1  %2 — %3 — %4").arg(r.atomic_number).arg(r.symbol, r.name, r.atomic_mass), list_widget);
		item->setData(Qt::UserRole, r.symbol);
	}
	tabs->addTab(list_widget, QStringLiteral("List")); tabs->addTab(graph_widget, QStringLiteral("3D property graph"));
	connect(table_view, &QTableView::doubleClicked, this, [this](const QModelIndex& index) { const PeriodicElementRecord* r = model->elementAt(index.row()); if(r && elementActivated) elementActivated(r->symbol); });
	connect(list_widget, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) { if(item && elementActivated) elementActivated(item->data(Qt::UserRole).toString()); });
}


void PeriodicTableWidget::setHighlightedSymbols(const QStringList& symbols)
{
	graph_widget->setHighlightedSymbols(symbols);
	for(int i=0; i<list_widget->count(); ++i)
	{
		QListWidgetItem* item = list_widget->item(i);
		item->setBackground(symbols.contains(item->data(Qt::UserRole).toString(), Qt::CaseInsensitive) ? QColor(255, 230, 120) : QBrush());
	}
}


void PeriodicTableWidget::selectElement(const QString& symbol)
{
	const QVector<PeriodicElementRecord>& records = PeriodicTableModel::allElements();
	for(int i=0; i<records.size(); ++i)
		if(records[i].symbol.compare(symbol, Qt::CaseInsensitive) == 0)
		{
			table_view->selectRow(i); table_view->scrollTo(model->index(i, 0));
			for(int j=0; j<list_widget->count(); ++j) if(list_widget->item(j)->data(Qt::UserRole).toString().compare(symbol, Qt::CaseInsensitive) == 0) { list_widget->setCurrentRow(j); break; }
			break;
		}
}
