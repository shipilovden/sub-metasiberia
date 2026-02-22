#include "ParcelEditor.h"


#include "PhysicsObject.h"
#include <opengl/OpenGLEngine.h>
#include "../qt/SignalBlocker.h"
#include "../qt/IndigoDoubleSpinBox.h"
#include "../qt/QtUtils.h"
#include <StringUtils.h>
#include <ContainerUtils.h>
#include <QtGui/QDesktopServices>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QCheckBox>
#include <set>
#include <algorithm>
#include <cctype>


template <class StringType>
static void checkStringSize(StringType& s, size_t max_size)
{
	// TODO: throw exception instead?
	if(s.size() > max_size)
		s = s.substr(0, max_size);
}


static std::string canonicalUserRef(const std::string& s)
{
	return toLowerCase(stripHeadAndTailWhitespace(s));
}


static bool tryParseUserIDToken(const std::string& token, UserID& user_id_out)
{
	const std::string stripped = stripHeadAndTailWhitespace(token);
	if(stripped.empty())
		return false;

	auto tryParseDigits = [&](const std::string& digits) -> bool
	{
		if(digits.empty())
			return false;

		for(size_t i=0; i<digits.size(); ++i)
			if(!std::isdigit((unsigned char)digits[i]))
				return false;

		try
		{
			const uint64 v = stringToUInt64(digits);
			if(v > 0xffffffffULL)
				return false;

			user_id_out = UserID((uint32)v);
			return true;
		}
		catch(...)
		{
			return false;
		}
	};

	if(tryParseDigits(stripped))
		return true;

	const size_t close_paren = stripped.rfind(')');
	const size_t open_paren = (close_paren != std::string::npos) ? stripped.rfind('(', close_paren) : std::string::npos;
	if((open_paren != std::string::npos) && (close_paren != std::string::npos) && (close_paren > open_paren + 1))
	{
		const std::string inside = stripHeadAndTailWhitespace(stripped.substr(open_paren + 1, close_paren - open_paren - 1));
		if(tryParseDigits(inside))
			return true;
	}

	return false;
}


ParcelEditor::ParcelEditor(QWidget *parent)
:	QWidget(parent),
	have_last_set_parcel(false),
	can_edit_basic_fields(false),
	can_edit_owner_and_geometry(false),
	can_edit_member_lists(false),
	ownerLineEdit(NULL),
	positionXDoubleSpinBox(NULL),
	positionYDoubleSpinBox(NULL),
	positionZDoubleSpinBox(NULL),
	scaleXDoubleSpinBox(NULL),
	scaleYDoubleSpinBox(NULL),
	scaleZDoubleSpinBox(NULL),
	linkScaleCheckBox(NULL),
	last_x_scale_over_y_scale(1.0),
	last_x_scale_over_z_scale(1.0),
	last_y_scale_over_z_scale(1.0)
{
	setupUi(this);

	ownerLineEdit = new QLineEdit(this->groupBox);
	ownerLineEdit->setObjectName("ownerLineEdit");
	ownerLineEdit->setPlaceholderText("owner id");
	this->ownerLabel->hide();
	this->gridLayout->addWidget(ownerLineEdit, 2, 1, 1, 1);

	{
		QLabel* position_label = new QLabel("Position x/y/z:", this->groupBox);
		this->gridLayout->addWidget(position_label, 15, 0, 1, 1);

		QWidget* pos_widget = new QWidget(this->groupBox);
		QHBoxLayout* h = new QHBoxLayout(pos_widget);
		h->setContentsMargins(0, 0, 0, 0);

		positionXDoubleSpinBox = new IndigoDoubleSpinBox(pos_widget);
		positionYDoubleSpinBox = new IndigoDoubleSpinBox(pos_widget);
		positionZDoubleSpinBox = new IndigoDoubleSpinBox(pos_widget);

		const double min_coord = -1000000000.0;
		const double max_coord = 1000000000.0;

		positionXDoubleSpinBox->setMinimum(min_coord);
		positionXDoubleSpinBox->setMaximum(max_coord);
		positionXDoubleSpinBox->setSingleStep(0.1);

		positionYDoubleSpinBox->setMinimum(min_coord);
		positionYDoubleSpinBox->setMaximum(max_coord);
		positionYDoubleSpinBox->setSingleStep(0.1);

		positionZDoubleSpinBox->setMinimum(min_coord);
		positionZDoubleSpinBox->setMaximum(max_coord);
		positionZDoubleSpinBox->setSingleStep(0.1);

		h->addWidget(positionXDoubleSpinBox);
		h->addWidget(positionYDoubleSpinBox);
		h->addWidget(positionZDoubleSpinBox);

		this->gridLayout->addWidget(pos_widget, 15, 1, 1, 1);
	}

	{
		QLabel* scale_label = new QLabel("Scale x/y/z:", this->groupBox);
		this->gridLayout->addWidget(scale_label, 16, 0, 1, 1);

		QWidget* scale_widget = new QWidget(this->groupBox);
		QVBoxLayout* v = new QVBoxLayout(scale_widget);
		v->setContentsMargins(0, 0, 0, 0);
		v->setSpacing(2);

		QWidget* scale_spin_widget = new QWidget(scale_widget);
		QHBoxLayout* h = new QHBoxLayout(scale_spin_widget);
		h->setContentsMargins(0, 0, 0, 0);

		scaleXDoubleSpinBox = new IndigoDoubleSpinBox(scale_spin_widget);
		scaleYDoubleSpinBox = new IndigoDoubleSpinBox(scale_spin_widget);
		scaleZDoubleSpinBox = new IndigoDoubleSpinBox(scale_spin_widget);

		const double min_scale = 0.001;
		const double max_scale = 1000000000.0;

		scaleXDoubleSpinBox->setMinimum(min_scale);
		scaleXDoubleSpinBox->setMaximum(max_scale);
		scaleXDoubleSpinBox->setSingleStep(0.1);

		scaleYDoubleSpinBox->setMinimum(min_scale);
		scaleYDoubleSpinBox->setMaximum(max_scale);
		scaleYDoubleSpinBox->setSingleStep(0.1);

		scaleZDoubleSpinBox->setMinimum(min_scale);
		scaleZDoubleSpinBox->setMaximum(max_scale);
		scaleZDoubleSpinBox->setSingleStep(0.1);

		h->addWidget(scaleXDoubleSpinBox);
		h->addWidget(scaleYDoubleSpinBox);
		h->addWidget(scaleZDoubleSpinBox);

		v->addWidget(scale_spin_widget);

		linkScaleCheckBox = new QCheckBox("Link x/y/z scale", scale_widget);
		linkScaleCheckBox->setChecked(true);
		v->addWidget(linkScaleCheckBox, 0, Qt::AlignLeft);

		this->gridLayout->addWidget(scale_widget, 16, 1, 1, 1);
	}

	connect(this->titleLineEdit,                SIGNAL(textChanged(const QString&)), this, SIGNAL(parcelChanged()));
	connect(this->descriptionTextEdit,          SIGNAL(textChanged()),               this, SIGNAL(parcelChanged()));
	connect(this->allWriteableCheckBox,         SIGNAL(toggled(bool)),               this, SIGNAL(parcelChanged()));
	connect(this->muteOutsideAudioCheckBox,     SIGNAL(toggled(bool)),               this, SIGNAL(parcelChanged()));
	connect(this->spawnXDoubleSpinBox,          SIGNAL(valueChanged(double)),        this, SIGNAL(parcelChanged()));
	connect(this->spawnYDoubleSpinBox,          SIGNAL(valueChanged(double)),        this, SIGNAL(parcelChanged()));
	connect(this->spawnZDoubleSpinBox,          SIGNAL(valueChanged(double)),        this, SIGNAL(parcelChanged()));
	connect(this->adminsTextEdit,               SIGNAL(textChanged()),               this, SIGNAL(parcelChanged()));
	connect(this->writersTextEdit,              SIGNAL(textChanged()),               this, SIGNAL(parcelChanged()));
	connect(this->ownerLineEdit,                SIGNAL(textChanged(const QString&)), this, SIGNAL(parcelChanged()));
	connect(this->positionXDoubleSpinBox,       SIGNAL(valueChanged(double)),        this, SIGNAL(parcelChanged()));
	connect(this->positionYDoubleSpinBox,       SIGNAL(valueChanged(double)),        this, SIGNAL(parcelChanged()));
	connect(this->positionZDoubleSpinBox,       SIGNAL(valueChanged(double)),        this, SIGNAL(parcelChanged()));
	connect(this->scaleXDoubleSpinBox,          SIGNAL(valueChanged(double)),        this, SLOT(xScaleChanged(double)));
	connect(this->scaleYDoubleSpinBox,          SIGNAL(valueChanged(double)),        this, SLOT(yScaleChanged(double)));
	connect(this->scaleZDoubleSpinBox,          SIGNAL(valueChanged(double)),        this, SLOT(zScaleChanged(double)));
	connect(this->linkScaleCheckBox,            SIGNAL(toggled(bool)),               this, SLOT(linkScaleCheckBoxToggled(bool)));

	setEditingPermissions(false, false, false);
}


ParcelEditor::~ParcelEditor()
{
}


void ParcelEditor::setFromParcel(const Parcel& parcel)
{
	last_set_parcel = parcel;
	have_last_set_parcel = true;
	rebuildKnownUserRefMaps(parcel);

	this->IDLabel->setText(QtUtils::toQString(parcel.id.toString()));

	const std::string owner_name = !parcel.owner_name.empty() ? parcel.owner_name : nameForUserID(parcel.owner_id);

	{
		SignalBlocker b(this->ownerLineEdit);
		this->ownerLineEdit->setText(QtUtils::toQString(formatUserRef(parcel.owner_id, owner_name)));
	}
	this->createdTimeLabel->setText(QtUtils::toQString(parcel.created_time.timeAgoDescription()));

	{
		SignalBlocker b(this->titleLineEdit);
		this->titleLineEdit->setText(QtUtils::toQString(parcel.title));
	}
	{
		SignalBlocker b(this->descriptionTextEdit);
		this->descriptionTextEdit->setPlainText(QtUtils::toQString(parcel.description));
	}
	{
		SignalBlocker b(this->writersTextEdit);
		this->writersTextEdit->setPlainText(QtUtils::toQString(joinedUserRefs(parcel.writer_ids, parcel.writer_names)));
	}
	{
		SignalBlocker b(this->adminsTextEdit);
		this->adminsTextEdit->setPlainText(QtUtils::toQString(joinedUserRefs(parcel.admin_ids, parcel.admin_names)));
	}

	SignalBlocker::setChecked(this->allWriteableCheckBox, parcel.all_writeable);

	SignalBlocker::setChecked(this->muteOutsideAudioCheckBox, BitUtils::isBitSet(parcel.flags, Parcel::MUTE_OUTSIDE_AUDIO_FLAG));

	this->minLabel->setText(QtUtils::toQString(parcel.aabb_min.toString()));
	this->maxLabel->setText(QtUtils::toQString(parcel.aabb_max.toString()));

	SignalBlocker::setValue(this->spawnXDoubleSpinBox, parcel.spawn_point.x);
	SignalBlocker::setValue(this->spawnYDoubleSpinBox, parcel.spawn_point.y);
	SignalBlocker::setValue(this->spawnZDoubleSpinBox, parcel.spawn_point.z);

	SignalBlocker::setValue(this->positionXDoubleSpinBox, parcel.aabb_min.x);
	SignalBlocker::setValue(this->positionYDoubleSpinBox, parcel.aabb_min.y);
	SignalBlocker::setValue(this->positionZDoubleSpinBox, parcel.aabb_min.z);

	const Vec3d size = parcel.aabb_max - parcel.aabb_min;
	SignalBlocker::setValue(this->scaleXDoubleSpinBox, std::max(size.x, 0.001));
	SignalBlocker::setValue(this->scaleYDoubleSpinBox, std::max(size.y, 0.001));
	SignalBlocker::setValue(this->scaleZDoubleSpinBox, std::max(size.z, 0.001));
	updateScaleRatiosFromCurrentValues();
}


void ParcelEditor::setEditingPermissions(bool can_edit_basic_fields_, bool can_edit_owner_and_geometry_, bool can_edit_member_lists_)
{
	can_edit_basic_fields = can_edit_basic_fields_;
	can_edit_owner_and_geometry = can_edit_owner_and_geometry_;
	can_edit_member_lists = can_edit_member_lists_;
	applyEditingPermissions();
}


void ParcelEditor::applyEditingPermissions()
{
	this->titleLineEdit->setReadOnly(!can_edit_basic_fields);
	this->descriptionTextEdit->setReadOnly(!can_edit_basic_fields);
	this->allWriteableCheckBox->setEnabled(can_edit_basic_fields);
	this->muteOutsideAudioCheckBox->setEnabled(can_edit_basic_fields);
	this->spawnXDoubleSpinBox->setEnabled(can_edit_basic_fields);
	this->spawnYDoubleSpinBox->setEnabled(can_edit_basic_fields);
	this->spawnZDoubleSpinBox->setEnabled(can_edit_basic_fields);

	this->ownerLineEdit->setReadOnly(!can_edit_owner_and_geometry);
	this->positionXDoubleSpinBox->setEnabled(can_edit_owner_and_geometry);
	this->positionYDoubleSpinBox->setEnabled(can_edit_owner_and_geometry);
	this->positionZDoubleSpinBox->setEnabled(can_edit_owner_and_geometry);
	this->scaleXDoubleSpinBox->setEnabled(can_edit_owner_and_geometry);
	this->scaleYDoubleSpinBox->setEnabled(can_edit_owner_and_geometry);
	this->scaleZDoubleSpinBox->setEnabled(can_edit_owner_and_geometry);
	this->linkScaleCheckBox->setEnabled(can_edit_owner_and_geometry);

	this->adminsTextEdit->setReadOnly(!can_edit_member_lists);
	this->writersTextEdit->setReadOnly(!can_edit_member_lists);
}


void ParcelEditor::updateScaleRatiosFromCurrentValues()
{
	const double sx = std::max(this->scaleXDoubleSpinBox->value(), 1.0e-9);
	const double sy = std::max(this->scaleYDoubleSpinBox->value(), 1.0e-9);
	const double sz = std::max(this->scaleZDoubleSpinBox->value(), 1.0e-9);

	this->last_x_scale_over_y_scale = sx / sy;
	this->last_x_scale_over_z_scale = sx / sz;
	this->last_y_scale_over_z_scale = sy / sz;
}


void ParcelEditor::xScaleChanged(double new_x)
{
	if(this->linkScaleCheckBox->isChecked())
	{
		const double safe_xy = std::max(this->last_x_scale_over_y_scale, 1.0e-9);
		const double safe_xz = std::max(this->last_x_scale_over_z_scale, 1.0e-9);
		const double new_y = new_x / safe_xy;
		const double new_z = new_x / safe_xz;

		SignalBlocker::setValue(scaleYDoubleSpinBox, new_y);
		SignalBlocker::setValue(scaleZDoubleSpinBox, new_z);
	}
	else
	{
		updateScaleRatiosFromCurrentValues();
	}

	emit parcelChanged();
}


void ParcelEditor::yScaleChanged(double new_y)
{
	if(this->linkScaleCheckBox->isChecked())
	{
		const double safe_xy = std::max(this->last_x_scale_over_y_scale, 1.0e-9);
		const double safe_yz = std::max(this->last_y_scale_over_z_scale, 1.0e-9);
		const double new_x = safe_xy * new_y;
		const double new_z = new_y / safe_yz;

		SignalBlocker::setValue(scaleXDoubleSpinBox, new_x);
		SignalBlocker::setValue(scaleZDoubleSpinBox, new_z);
	}
	else
	{
		updateScaleRatiosFromCurrentValues();
	}

	emit parcelChanged();
}


void ParcelEditor::zScaleChanged(double new_z)
{
	if(this->linkScaleCheckBox->isChecked())
	{
		const double safe_xz = std::max(this->last_x_scale_over_z_scale, 1.0e-9);
		const double safe_yz = std::max(this->last_y_scale_over_z_scale, 1.0e-9);
		const double new_x = safe_xz * new_z;
		const double new_y = safe_yz * new_z;

		SignalBlocker::setValue(scaleXDoubleSpinBox, new_x);
		SignalBlocker::setValue(scaleYDoubleSpinBox, new_y);
	}
	else
	{
		updateScaleRatiosFromCurrentValues();
	}

	emit parcelChanged();
}


void ParcelEditor::linkScaleCheckBoxToggled(bool val)
{
	assertOrDeclareUsed(val);
	updateScaleRatiosFromCurrentValues();
}


void ParcelEditor::rebuildKnownUserRefMaps(const Parcel& parcel)
{
	known_user_ref_to_id.clear();
	known_user_id_to_name.clear();

	auto add_ref = [&](const UserID& user_id, const std::string& user_name)
	{
		if(!user_id.valid())
			return;

		known_user_ref_to_id[canonicalUserRef(user_id.toString())] = user_id;

		if(!user_name.empty())
		{
			known_user_ref_to_id[canonicalUserRef(user_name)] = user_id;
			known_user_ref_to_id[canonicalUserRef(user_name + " (" + user_id.toString() + ")")] = user_id;
			known_user_id_to_name[user_id.value()] = user_name;
		}
	};

	add_ref(parcel.owner_id, parcel.owner_name);

	for(size_t i=0; i<parcel.admin_ids.size(); ++i)
	{
		const std::string name = (i < parcel.admin_names.size()) ? parcel.admin_names[i] : std::string();
		add_ref(parcel.admin_ids[i], name);
	}

	for(size_t i=0; i<parcel.writer_ids.size(); ++i)
	{
		const std::string name = (i < parcel.writer_names.size()) ? parcel.writer_names[i] : std::string();
		add_ref(parcel.writer_ids[i], name);
	}
}


std::string ParcelEditor::formatUserRef(const UserID& user_id, const std::string& user_name) const
{
	if(user_id.valid())
	{
		if(!user_name.empty())
			return user_name + " (" + user_id.toString() + ")";
		else
			return user_id.toString();
	}

	return "[Unknown]";
}


std::string ParcelEditor::nameForUserID(const UserID& user_id) const
{
	const auto it = known_user_id_to_name.find(user_id.value());
	if(it != known_user_id_to_name.end())
		return it->second;
	return std::string();
}


bool ParcelEditor::tryParseUserRef(const std::string& ref, UserID& user_id_out) const
{
	if(tryParseUserIDToken(ref, user_id_out))
		return true;

	const auto it = known_user_ref_to_id.find(canonicalUserRef(ref));
	if(it != known_user_ref_to_id.end())
	{
		user_id_out = it->second;
		return true;
	}

	return false;
}


bool ParcelEditor::parseUserRefList(const std::string& refs_text, const std::vector<UserID>& fallback_ids, std::vector<UserID>& ids_out) const
{
	const std::string trimmed = stripHeadAndTailWhitespace(refs_text);
	if(trimmed.empty())
	{
		ids_out.clear();
		return true;
	}

	std::string normalised = refs_text;
	for(size_t i=0; i<normalised.size(); ++i)
	{
		if(normalised[i] == '\n' || normalised[i] == '\r' || normalised[i] == ';')
			normalised[i] = ',';
	}

	std::vector<UserID> parsed_ids;
	std::set<uint32> inserted_ids;
	bool parsed_any = false;

	const std::vector<std::string> tokens = split(normalised, ',');
	for(size_t i=0; i<tokens.size(); ++i)
	{
		const std::string token = stripHeadAndTailWhitespace(tokens[i]);
		if(token.empty())
			continue;

		UserID parsed_user_id;
		if(tryParseUserRef(token, parsed_user_id) && parsed_user_id.valid() && (inserted_ids.count(parsed_user_id.value()) == 0))
		{
			parsed_any = true;
			inserted_ids.insert(parsed_user_id.value());
			parsed_ids.push_back(parsed_user_id);
		}
	}

	if(parsed_any)
	{
		ids_out = parsed_ids;
		return true;
	}
	else
	{
		ids_out = fallback_ids;
		return false;
	}
}


std::string ParcelEditor::joinedUserRefs(const std::vector<UserID>& user_ids, const std::vector<std::string>& user_names) const
{
	std::vector<std::string> refs;
	refs.reserve(user_ids.size());

	for(size_t i=0; i<user_ids.size(); ++i)
	{
		const std::string name = (i < user_names.size()) ? user_names[i] : std::string();
		refs.push_back(formatUserRef(user_ids[i], name));
	}

	return StringUtils::join(refs, ",\n");
}


void ParcelEditor::toParcel(Parcel& parcel_out)
{
	if(can_edit_basic_fields)
	{
		parcel_out.title = QtUtils::toStdString(this->titleLineEdit->text());
		checkStringSize(parcel_out.title, Parcel::MAX_TITLE_SIZE);

		parcel_out.description = QtUtils::toStdString(this->descriptionTextEdit->toPlainText());
		checkStringSize(parcel_out.description, Parcel::MAX_DESCRIPTION_SIZE);

		parcel_out.all_writeable = this->allWriteableCheckBox->isChecked();

		const bool mute_outside_audio = this->muteOutsideAudioCheckBox->isChecked();
		parcel_out.flags = 0;
		if(mute_outside_audio)
			BitUtils::setBit(parcel_out.flags, Parcel::MUTE_OUTSIDE_AUDIO_FLAG);

		parcel_out.spawn_point.x = this->spawnXDoubleSpinBox->value();
		parcel_out.spawn_point.y = this->spawnYDoubleSpinBox->value();
		parcel_out.spawn_point.z = this->spawnZDoubleSpinBox->value();
	}

	if(can_edit_owner_and_geometry)
	{
		UserID parsed_owner_id;
		if(tryParseUserRef(QtUtils::toStdString(this->ownerLineEdit->text()), parsed_owner_id) && parsed_owner_id.valid())
			parcel_out.owner_id = parsed_owner_id;

		const double px = this->positionXDoubleSpinBox->value();
		const double py = this->positionYDoubleSpinBox->value();
		const double pz = this->positionZDoubleSpinBox->value();
		const double sx = std::max(this->scaleXDoubleSpinBox->value(), 0.001);
		const double sy = std::max(this->scaleYDoubleSpinBox->value(), 0.001);
		const double sz = std::max(this->scaleZDoubleSpinBox->value(), 0.001);

		parcel_out.verts[0] = Vec2d(px,      py);
		parcel_out.verts[1] = Vec2d(px + sx, py);
		parcel_out.verts[2] = Vec2d(px + sx, py + sy);
		parcel_out.verts[3] = Vec2d(px,      py + sy);
		parcel_out.zbounds = Vec2d(pz, pz + sz);
	}

	if(can_edit_member_lists)
	{
		std::vector<UserID> parsed_admin_ids;
		parseUserRefList(QtUtils::toStdString(this->adminsTextEdit->toPlainText()), parcel_out.admin_ids, parsed_admin_ids);
		parcel_out.admin_ids = parsed_admin_ids;

		std::vector<UserID> parsed_writer_ids;
		parseUserRefList(QtUtils::toStdString(this->writersTextEdit->toPlainText()), parcel_out.writer_ids, parsed_writer_ids);
		parcel_out.writer_ids = parsed_writer_ids;
	}

	// Ensure owner always has admin/writer rights.
	if(parcel_out.owner_id.valid())
	{
		if(!ContainerUtils::contains(parcel_out.admin_ids, parcel_out.owner_id))
			parcel_out.admin_ids.push_back(parcel_out.owner_id);
		if(!ContainerUtils::contains(parcel_out.writer_ids, parcel_out.owner_id))
			parcel_out.writer_ids.push_back(parcel_out.owner_id);
	}

	// Keep denormalised name fields coherent with the IDs we can resolve locally.
	{
		const std::string owner_name = nameForUserID(parcel_out.owner_id);
		if(!owner_name.empty())
			parcel_out.owner_name = owner_name;
		else if(parcel_out.owner_id.valid())
			parcel_out.owner_name = "user id: " + parcel_out.owner_id.toString();
	}

	parcel_out.admin_names.resize(parcel_out.admin_ids.size());
	for(size_t i=0; i<parcel_out.admin_ids.size(); ++i)
	{
		const std::string name = nameForUserID(parcel_out.admin_ids[i]);
		parcel_out.admin_names[i] = !name.empty() ? name : ("user id: " + parcel_out.admin_ids[i].toString());
	}

	parcel_out.writer_names.resize(parcel_out.writer_ids.size());
	for(size_t i=0; i<parcel_out.writer_ids.size(); ++i)
	{
		const std::string name = nameForUserID(parcel_out.writer_ids[i]);
		parcel_out.writer_names[i] = !name.empty() ? name : ("user id: " + parcel_out.writer_ids[i].toString());
	}

	parcel_out.build();
}


void ParcelEditor::setCurrentServerURL(const std::string& server_url)
{
	current_server_url = server_url;
	
	// Parse URL to extract hostname
	std::string hostname = "vr.metasiberia.com"; // Default fallback
	
	if(!current_server_url.empty())
	{
		// Parse URL to extract hostname
		// Format: sub://hostname:port/path
		if(current_server_url.find("sub://") == 0)
		{
			std::string url_part = current_server_url.substr(6); // Remove "sub://"
			size_t colon_pos = url_part.find(':');
			size_t slash_pos = url_part.find('/');
			
			if(colon_pos != std::string::npos)
				hostname = url_part.substr(0, colon_pos);
			else if(slash_pos != std::string::npos)
				hostname = url_part.substr(0, slash_pos);
			else
				hostname = url_part;
		}
	}
	
	// Update the link text
	QString link_text = QString::fromStdString("<a href=\"#boo\">Show parcel on " + hostname + "</a>");
	this->showOnWebLabel->setText(link_text);
}

void ParcelEditor::on_showOnWebLabel_linkActivated(const QString&)
{
	// Extract hostname from server URL
	std::string hostname = "vr.metasiberia.com"; // Default fallback
	
	if(!current_server_url.empty())
	{
		// Parse URL to extract hostname
		// Format: sub://hostname:port/path
		if(current_server_url.find("sub://") == 0)
		{
			std::string url_part = current_server_url.substr(6); // Remove "sub://"
			size_t colon_pos = url_part.find(':');
			size_t slash_pos = url_part.find('/');
			
			if(colon_pos != std::string::npos)
				hostname = url_part.substr(0, colon_pos);
			else if(slash_pos != std::string::npos)
				hostname = url_part.substr(0, slash_pos);
			else
				hostname = url_part;
		}
	}
	
	// Open URL with parcel ID: https://hostname/parcel/ID
	QDesktopServices::openUrl(QString::fromStdString("https://" + hostname + "/parcel/" + this->IDLabel->text().toStdString()));
}
