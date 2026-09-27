/*=====================================================================
SpotlightEditor.h
-----------------
Dedicated editor panel for spotlight objects.
=====================================================================*/
#pragma once


#include "ObjectEditor.h"


class SpotlightEditor final : public ObjectEditor
{
public:
	explicit SpotlightEditor(QWidget* parent = 0);
	void setFromObject(const WorldObject& ob, int selected_mat_index, bool ob_in_editing_users_world) override;
};
