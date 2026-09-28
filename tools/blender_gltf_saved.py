"""The glTF export settings a .blend saved, ready to hand to bpy.ops.export_scene.gltf.

Shared by tools/blender_export_profile.py and tools/blender_export_glb.py, which both have to
reproduce the export EXACTLY as the dialog would run it: the scene's glTF2ExportSettings (what the
dialog remembers between exports) and the active collection, which a background Blender has no UI
to remember and so has to be set by name.

Imported by path - the scripts put their own folder on sys.path, since Blender runs them with
--python and not as a package.
"""

import ast
import sys

import bpy


def arg(argv, name, default):
    """The value after `name` in the script's arguments (those after "--"), or `default`."""
    return argv[argv.index(name) + 1] if name in argv and argv.index(name) + 1 < len(argv) else default


def find_layer_collection(layer, name):
    if layer.collection.name == name:
        return layer
    for child in layer.children:
        found = find_layer_collection(child, name)
        if found:
            return found
    return None


def saved_settings(argv, out, collection):
    """
    The file's saved settings with filepath = `out`, `--set key=value` overrides applied (values as
    Python literals where they parse), and the active collection set to `collection` when the
    settings export the active one. Exits with a message when the file has no saved settings.
    """
    scene = bpy.context.scene
    saved = scene.get("glTF2ExportSettings")
    if not saved:
        print("\nNo saved glTF export settings in this file - export once from the dialog first.")
        sys.exit(1)

    accepted = {p.identifier for p in bpy.ops.export_scene.gltf.get_rna_type().properties}
    settings = {}
    for key in saved.keys():
        if key in accepted and key != "filepath":
            value = saved[key]
            settings[key] = value.to_list() if hasattr(value, "to_list") else value
    settings["filepath"] = out

    for i, a in enumerate(argv):
        if a == "--set" and i + 1 < len(argv) and "=" in argv[i + 1]:
            key, raw = argv[i + 1].split("=", 1)
            try:
                value = ast.literal_eval(raw)
            except (ValueError, SyntaxError):
                value = raw
            if key not in accepted:
                print("\n--set %s: the exporter has no such setting" % key)
                sys.exit(1)
            settings[key] = value
            print("  overriding %s = %r" % (key, value))

    if settings.get("use_active_collection"):
        layer = find_layer_collection(bpy.context.view_layer.layer_collection, collection)
        if not layer:
            print("\nNo collection '%s' in the view layer - pass --collection NAME." % collection)
            sys.exit(1)
        bpy.context.view_layer.active_layer_collection = layer
    return settings
