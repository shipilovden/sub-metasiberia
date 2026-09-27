/*=====================================================================
SpotlightEditor.cpp
-------------------
Dedicated editor panel for spotlight objects.
=====================================================================*/


#include "SpotlightEditor.h"


SpotlightEditor::SpotlightEditor(QWidget* parent)
	: ObjectEditor(parent)
{
	// The spotlight editor keeps the common object fields.  Only the font
	// control is irrelevant for a spotlight and is hidden here.
	fontLabel->hide();
	fontComboBox->hide();
}


void SpotlightEditor::setFromObject(const WorldObject& ob, int selected_mat_index_, bool ob_in_editing_users_world)
{
	ObjectEditor::setFromObject(ob, selected_mat_index_, ob_in_editing_users_world);

	// ObjectEditor refreshes common-field visibility for every selected object,
	// so enforce the spotlight-specific omission after that refresh.
	fontLabel->hide();
	fontComboBox->hide();
}
