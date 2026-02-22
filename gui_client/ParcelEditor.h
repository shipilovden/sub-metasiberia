#pragma once


#include "../shared/Parcel.h"
#include "ui_ParcelEditor.h"
#include <unordered_map>
#include <vector>

class IndigoDoubleSpinBox;
class QLineEdit;


class ParcelEditor : public QWidget, public Ui::ParcelEditor
{
	Q_OBJECT        // must include this if you use Qt signals/slots

public:
	ParcelEditor(QWidget *parent = 0);
	~ParcelEditor();

	void setFromParcel(const Parcel& parcel);

	void toParcel(Parcel& parcel_out);

	void setCurrentServerURL(const std::string& server_url);
	void setEditingPermissions(bool can_edit_basic_fields, bool can_edit_owner_and_geometry, bool can_edit_member_lists);

signals:;
	void parcelChanged();

private slots:
	void on_showOnWebLabel_linkActivated(const QString& link);

protected:
	void applyEditingPermissions();
	void rebuildKnownUserRefMaps(const Parcel& parcel);
	std::string formatUserRef(const UserID& user_id, const std::string& user_name) const;
	bool tryParseUserRef(const std::string& ref, UserID& user_id_out) const;
	bool parseUserRefList(const std::string& refs_text, const std::vector<UserID>& fallback_ids, std::vector<UserID>& ids_out) const;
	std::string joinedUserRefs(const std::vector<UserID>& user_ids, const std::vector<std::string>& user_names) const;
	std::string nameForUserID(const UserID& user_id) const;

	std::string current_server_url;
	Parcel last_set_parcel;
	bool have_last_set_parcel;
	bool can_edit_basic_fields;
	bool can_edit_owner_and_geometry;
	bool can_edit_member_lists;

	QLineEdit* ownerLineEdit;

	IndigoDoubleSpinBox* positionXDoubleSpinBox;
	IndigoDoubleSpinBox* positionYDoubleSpinBox;
	IndigoDoubleSpinBox* positionZDoubleSpinBox;
	IndigoDoubleSpinBox* scaleXDoubleSpinBox;
	IndigoDoubleSpinBox* scaleYDoubleSpinBox;
	IndigoDoubleSpinBox* scaleZDoubleSpinBox;

	std::unordered_map<std::string, UserID> known_user_ref_to_id;
	std::unordered_map<uint32, std::string> known_user_id_to_name;
};
