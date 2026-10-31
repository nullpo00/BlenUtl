# SPDX-License-Identifier: GPL-3.0-or-later

# type: ignore

bl_info = {
    "name": "Export: AviUtl ExEdit2 (.json)",
    "description": "Export transform data for AviUtl ExEdit2",
    "author": "NullPo",
    "version": (1, 0, 0),
    "blender": (5, 0, 0),
    "location": "File > Export > AviUtl ExEdit2 (.json)",
    "warning": "",
    "category": "Import-Export"
}

import bpy
import mathutils
import math
import json
from bpy_extras.io_utils import ExportHelper
from bpy.types import Operator


def write_transform_data(context, filepath, frame_st, frame_end) -> bool:

    active_obj = context.active_object
    matrix = active_obj.matrix_world
    res_per = context.scene.render.resolution_percentage
    res_x = context.scene.render.resolution_x * res_per / 100.0
    res_y = context.scene.render.resolution_y * res_per / 100.0
    fps = context.scene.render.fps
    object_type = "OBJECT"
    fov = None
    frames = []

    if active_obj.type == "CAMERA":
        object_type = "CAMERA"
        fov = math.degrees(active_obj.data.angle)

    for frame in range(frame_st, frame_end + 1):
        context.scene.frame_set(frame)

        location = matrix.to_translation()
        rotation = matrix.to_quaternion()
        scale = matrix.to_scale()

        # Transform
        loc_list = [round(l, 8) for l in location]
        rot_list = [round(r, 8) for r in rotation]
        scl_list = [round(s, 8) for s in scale]

        # Frame data
        frames.append({
            "transform": [loc_list, rot_list, scl_list]
        })

    # Write data
    export_data = {
        "width": round(res_x),
        "height": round(res_y),
        "fps": fps,
        "object_type": object_type,
        "duration": (frame_end + 1) - frame_st,
        "fov": fov,
        "frames": frames
    }

    try:
        # Write JSON file
        with open(filepath, "w", encoding="utf-8") as file:
            json.dump(export_data, file, indent=4)
    except Exception:
        return False

    return True


class ExportTransformData(Operator, ExportHelper):
    bl_idname = "export_transform.transform_data"
    bl_label = "Export Transform Data"
    filename_ext = ".json"

    use_frame_range: bpy.props.BoolProperty(
        name="Frame Range",
        description="Use frame range settings",
        default=False
    )

    start_frame: bpy.props.IntProperty(
        name="Start",
        description="First frame of the export frame range",
        default=1,
        min=0
    )

    end_frame: bpy.props.IntProperty(
        name="End",
        description="Final frame of the export frame range",
        default=250,
        min = 0
    )

    # Override
    @classmethod
    def poll(cls, context):
        return context.active_object is not None

    # Override
    def draw(self, context):
        layout = self.layout

        layout.label(text="Export Settings")

        header, body = layout.panel(idname="frame_range", default_closed=False)
        header.prop(self, "use_frame_range")

        if body:
            box = body.box()
            
            col = box.column(align=True)
            col.active = self.use_frame_range
            col.use_property_split = True
            col.use_property_decorate = False
            col.prop(self, "start_frame")
            col.prop(self, "end_frame")

    # Override
    def execute(self, context):

        active_obj = context.active_object
        st = context.scene.frame_start
        end = context.scene.frame_end

        if active_obj is None:
            self.report({"INFO"}, "Active object is None")
            return {"CANCELLED"}

        if self.use_frame_range:
            st = self.start_frame
            end = self.end_frame

        result = write_transform_data(context=context, filepath=self.filepath, frame_st=st, frame_end=end)

        if result:
            self.report({"INFO"}, "Export was successful")
        else:
            self.report({"INFO"}, "Export failed")

        return {"FINISHED"}


def menu_func_export(self, context):
    self.layout.operator(ExportTransformData.bl_idname, text="AviUtl ExEdit2 (.json)")

def register():
    bpy.utils.register_class(ExportTransformData)
    bpy.types.TOPBAR_MT_file_export.append(menu_func_export)

def unregister():
    bpy.utils.unregister_class(ExportTransformData)
    bpy.types.TOPBAR_MT_file_export.remove(menu_func_export)

if __name__ == "__main__":
    register()
