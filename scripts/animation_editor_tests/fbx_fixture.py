"""Generate owned synthetic Mixamo-named fixtures; never loads user .blend settings."""
import bpy
import os
import sys

directory = sys.argv[sys.argv.index('--') + 1]
os.makedirs(directory, exist_ok=True)
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.object.armature_add()
rig = bpy.context.object
rig.name = 'Armature'
bpy.ops.object.mode_set(mode='EDIT')
rig.data.edit_bones.remove(rig.data.edit_bones[0])
names = ['Hips', 'Spine', 'Spine1', 'Spine2', 'Neck', 'Head',
         'LeftShoulder', 'LeftArm', 'LeftForeArm', 'LeftHand',
         'RightShoulder', 'RightArm', 'RightForeArm', 'RightHand',
         'LeftUpLeg', 'LeftLeg', 'LeftFoot', 'LeftToeBase', 'LeftToe_End',
         'RightUpLeg', 'RightLeg', 'RightFoot', 'RightToeBase', 'RightToe_End']
for i, name in enumerate(names):
    bone = rig.data.edit_bones.new('mixamorig:' + name)
    bone.head = (0, 0, 1 + i * .02)
    bone.tail = (0, .05, 1 + i * .02)
    if i:
        bone.parent = rig.data.edit_bones['mixamorig:Hips']
bpy.ops.object.mode_set(mode='POSE')
hand = rig.pose.bones['mixamorig:LeftHand']
hand.rotation_mode = 'XYZ'
for frame, angle in [(1, 0), (16, .6), (31, 0)]:
    hand.rotation_euler.z = angle
    hand.keyframe_insert(data_path='rotation_euler', frame=frame)
bpy.ops.object.mode_set(mode='OBJECT')
bpy.context.scene.frame_start = 1
bpy.context.scene.frame_end = 31
bpy.context.scene.render.fps = 30
bpy.ops.export_scene.fbx(filepath=os.path.join(directory, 'without_skin.fbx'),
                         object_types={'ARMATURE'}, add_leaf_bones=False,
                         bake_anim=True, bake_anim_use_nla_strips=False,
                         bake_anim_use_all_actions=False)
print('PASS generated animation-only FBX fixture')

# The same motion with a minimal real skin exercises FBX cluster bind matrices.
mesh = bpy.data.meshes.new('Skin')
mesh.from_pydata([(0, 0, 1), (.1, 0, 1), (0, .1, 1)], [], [(0, 1, 2)])
skin = bpy.data.objects.new('Skin', mesh)
bpy.context.collection.objects.link(skin)
skin.parent = rig
skin.vertex_groups.new(name='mixamorig:Hips').add([0, 1, 2], 1, 'REPLACE')
skin.modifiers.new('Armature', 'ARMATURE').object = rig
bpy.ops.export_scene.fbx(filepath=os.path.join(directory, 'with_skin.fbx'),
                         object_types={'ARMATURE', 'MESH'}, add_leaf_bones=False,
                         bake_anim=True, bake_anim_use_nla_strips=False,
                         bake_anim_use_all_actions=False)
print('PASS generated skinned FBX fixture')
