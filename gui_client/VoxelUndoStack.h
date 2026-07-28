/*=====================================================================
VoxelUndoStack.h
----------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


#include "VoxelTools.h"
#include <map>
#include <string>
#include <vector>


// Delta-only, intentionally unbounded history.  Histories are isolated by
// object UID so changing the selected object cannot consume another object's
// undo entries.  Mesh rebuild/network updates remain the caller's job.
class VoxelUndoStack
{
public:
	bool push(UID object_uid, VoxelEditCommand command);
	bool push(const WorldObject& object, VoxelEditCommand command);

	bool undo(WorldObject& object, VoxelEditCommand* applied_command_out = nullptr);
	bool redo(WorldObject& object, VoxelEditCommand* applied_command_out = nullptr);

	bool canUndo(UID object_uid) const;
	bool canRedo(UID object_uid) const;
	size_t undoDepth(UID object_uid) const;
	size_t redoDepth(UID object_uid) const;

	void clear(UID object_uid);
	void clear(const WorldObject& object);
	void clearAll();

	static bool runSelfTest(std::string* details_out = nullptr);

private:
	struct History
	{
		std::vector<VoxelEditCommand> undo_commands;
		std::vector<VoxelEditCommand> redo_commands;
	};

	std::map<uint64, History> histories;
};
