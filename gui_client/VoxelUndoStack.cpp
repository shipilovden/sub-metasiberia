/*=====================================================================
VoxelUndoStack.cpp
------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "VoxelUndoStack.h"


#include <utility>


bool VoxelUndoStack::push(const UID object_uid, VoxelEditCommand command)
{
	if(!object_uid.valid())
		return false;
	command.compact();
	if(command.empty())
		return false;

	History& history = histories[object_uid.value()];
	history.undo_commands.push_back(std::move(command));
	history.redo_commands.clear();
	return true;
}


bool VoxelUndoStack::push(const WorldObject& object, VoxelEditCommand command)
{
	if(object.object_type != WorldObject::ObjectType_VoxelGroup)
		return false;
	return push(object.uid, std::move(command));
}


bool VoxelUndoStack::undo(WorldObject& object, VoxelEditCommand* applied_command_out)
{
	if(object.object_type != WorldObject::ObjectType_VoxelGroup || !object.uid.valid())
		return false;
	auto history_it = histories.find(object.uid.value());
	if(history_it == histories.end() || history_it->second.undo_commands.empty())
		return false;

	History& history = history_it->second;
	VoxelEditCommand command = std::move(history.undo_commands.back());
	history.undo_commands.pop_back();
	command.undo(object); // Caller owns decompression, mesh rebuild and network synchronisation.
	if(applied_command_out)
		*applied_command_out = command;
	history.redo_commands.push_back(std::move(command));
	return true;
}


bool VoxelUndoStack::redo(WorldObject& object, VoxelEditCommand* applied_command_out)
{
	if(object.object_type != WorldObject::ObjectType_VoxelGroup || !object.uid.valid())
		return false;
	auto history_it = histories.find(object.uid.value());
	if(history_it == histories.end() || history_it->second.redo_commands.empty())
		return false;

	History& history = history_it->second;
	VoxelEditCommand command = std::move(history.redo_commands.back());
	history.redo_commands.pop_back();
	command.redo(object); // Caller owns decompression, mesh rebuild and network synchronisation.
	if(applied_command_out)
		*applied_command_out = command;
	history.undo_commands.push_back(std::move(command));
	return true;
}


bool VoxelUndoStack::canUndo(const UID object_uid) const
{
	const auto it = histories.find(object_uid.value());
	return it != histories.end() && !it->second.undo_commands.empty();
}


bool VoxelUndoStack::canRedo(const UID object_uid) const
{
	const auto it = histories.find(object_uid.value());
	return it != histories.end() && !it->second.redo_commands.empty();
}


size_t VoxelUndoStack::undoDepth(const UID object_uid) const
{
	const auto it = histories.find(object_uid.value());
	return it == histories.end() ? 0 : it->second.undo_commands.size();
}


size_t VoxelUndoStack::redoDepth(const UID object_uid) const
{
	const auto it = histories.find(object_uid.value());
	return it == histories.end() ? 0 : it->second.redo_commands.size();
}


void VoxelUndoStack::clear(const UID object_uid)
{
	histories.erase(object_uid.value());
}


void VoxelUndoStack::clear(const WorldObject& object)
{
	clear(object.uid);
}


void VoxelUndoStack::clearAll()
{
	histories.clear();
}


bool VoxelUndoStack::runSelfTest(std::string* details_out)
{
	auto fail = [details_out](const char* message)
	{
		if(details_out)
			*details_out = message;
		return false;
	};

	VoxelEditCommand command("Self-test edit");
	command.recordChange(Vec3<int>(1, 0, 0), VoxelChange::NO_MATERIAL, 0, 0);
	glare::AllocatorVector<Voxel, 16> voxels;
	voxels.push_back(Voxel(Vec3<int>(0, 0, 0), 0));
	command.redo(voxels);
	if(voxels.size() != 2)
		return fail("Could not apply test voxel command.");
	command.undo(voxels);
	if(voxels.size() != 1)
		return fail("Could not reverse test voxel command.");

	VoxelUndoStack stack;
	const UID object_uid(1001);
	const UID other_uid(1002);
	if(!stack.push(object_uid, command) || stack.undoDepth(object_uid) != 1)
		return fail("Could not store per-object voxel command.");
	if(stack.canUndo(other_uid) || !stack.canUndo(object_uid))
		return fail("Voxel undo histories are not isolated by UID.");

	stack.clear(object_uid);
	if(stack.canUndo(object_uid) || stack.canRedo(object_uid))
		return fail("Voxel undo clear failed.");

	if(details_out)
		*details_out = "ok";
	return true;
}
