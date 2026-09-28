"""Blender add-on: glTF export keeps shape keys on meshes with modifiers.

INSTALL: Edit > Preferences > Add-ons > (the dropdown, top right) Install from Disk... > this file,
then tick it. After that it works inside the ordinary File > Export > glTF 2.0 dialog, with your
remembered settings; the dialog gains a section "Keep shape keys through modifiers", on by default.

WHY: with "Apply Modifiers" on, Blender's glTF exporter DROPS EVERY SHAPE KEY of a mesh that has any
modifier besides Armature, and bakes the keys' current values into the mesh instead - the add-on
says so only in a code comment (io_scene_gltf2/blender/exp/primitive_extract.py, "shape keys are
not preserved if we apply modifiers"). archer_face is Mirror + Armature with a MouthOpen key: the
key never arrived and her mouth was exported at whatever the slider was on. Blender will not apply a
modifier to a mesh with shape keys either, so the only manual fix is giving up the Mirror.

HOW, through the exporter's own user-extension hooks (the same mechanism its documented example
add-ons use), for every mesh the export will include that has shape keys and enabled modifiers
BEFORE its first Armature:

  pre_export_hook   evaluates the mesh once per key - all keys at 0 for the basis, then each key
                    alone at 1 - with the Armature and everything after it switched off; builds a
                    new mesh from the basis (UVs, materials and the Mirror's vertex groups come
                    with it) and rebuilds every key on it from those positions, at value 0; then
                    points the object at that mesh and switches the baked modifiers off.
  post_export_hook  points the object back at its own mesh, switches the modifiers back on and
                    deletes the baked mesh.

So the scene after an export is the scene before it: same mesh, same modifiers, same key values.
If the export fails part way the post hook never runs - a timer registered by the pre hook puts
everything back as soon as the dialog has closed, whichever way it closed.

A key that changes the vertex count through the modifiers (a Mirror merges by distance per
evaluation, so a key that moves a seam vertex off the seam can split it) cannot be rebuilt on one
mesh; that object is left alone, exported the old way, and the export's log says which key.

The same bake, headless, for the command line: tools/blender_export_glb.py.
"""

bl_info = {
    "name": "glTF: keep shape keys through modifiers",
    "author": "win32_transparent",
    "version": (1, 0, 0),
    "blender": (4, 2, 0),
    "location": "File > Export > glTF 2.0 - its own section in the dialog",
    "description": "Exports shape keys of meshes with a Mirror (or other) modifier, which the stock "
                   "glTF exporter drops when Apply Modifiers is on",
    "category": "Import-Export",
}

import bpy

#Every object this add-on has swapped and not yet put back. Module level, so the fail-safe timer
#finds the same list the hooks filled.
_swapped = []


def _exported_objects(export_settings):
    """The objects the export will look at, by the same three rules its dialog offers."""
    context = bpy.context
    if export_settings.get('gltf_active_collection'):
        collection = context.view_layer.active_layer_collection.collection
        if export_settings.get('gltf_active_collection_with_nested'):
            return list(collection.all_objects)
        return list(collection.objects)
    if export_settings.get('gltf_selected'):
        return list(context.selected_objects)
    return list(context.scene.objects)


def _bake_modifiers(obj):
    """The enabled modifiers before obj's first Armature - what the bake takes into the mesh."""
    mods = list(obj.modifiers)
    first_arm = next((i for i, m in enumerate(mods) if m.type == 'ARMATURE'), len(mods))
    return [m for m in mods[:first_arm] if m.show_viewport], mods[first_arm:]


def _evaluate_positions(obj):
    depsgraph = bpy.context.evaluated_depsgraph_get()
    depsgraph.update()
    ev = obj.evaluated_get(depsgraph)
    mesh = ev.to_mesh()
    co = [0.0] * (len(mesh.vertices) * 3)
    mesh.vertices.foreach_get("co", co)
    ev.to_mesh_clear()
    return co


def _swap_in_baked(obj, log):
    bake, after = _bake_modifiers(obj)
    keys = obj.data.shape_keys.key_blocks
    values = [k.value for k in keys]
    after_vis = [m.show_viewport for m in after]
    for m in after:
        m.show_viewport = False
    try:
        shapes = []
        problem = None
        for k in keys[1:]:
            k.value = 0.0
        basis = _evaluate_positions(obj)
        for k in keys[1:]:
            for other in keys[1:]:
                other.value = 0.0
            k.value = 1.0
            co = _evaluate_positions(obj)
            if len(co) != len(basis):
                problem = "shape key '%s' changes the vertex count through the modifiers (%d -> %d)" % (
                    k.name, len(basis) // 3, len(co) // 3)
                break
            shapes.append((k.name, co))
        baked = None
        if problem is None:
            for k in keys[1:]:
                k.value = 0.0
            depsgraph = bpy.context.evaluated_depsgraph_get()
            depsgraph.update()
            baked = bpy.data.meshes.new_from_object(obj.evaluated_get(depsgraph),
                                                    preserve_all_data_layers=True, depsgraph=depsgraph)
            baked.name = obj.data.name + ".keep_shape_keys"
    finally:
        for m, vis in zip(after, after_vis):
            m.show_viewport = vis
        for k, v in zip(keys, values):
            k.value = v
    if problem is not None:
        log.warning("%s: left alone, its shape keys will not export - %s" % (obj.name, problem))
        return

    record = {
        "obj": obj,
        "mesh": obj.data,
        "fake_user": obj.data.use_fake_user,
        "baked": baked,
        "mods": [(m, m.show_viewport) for m in bake],
    }
    #The object's own mesh has no user while it is swapped out; a fake one keeps it through anything
    #that might purge orphans in between.
    obj.data.use_fake_user = True
    obj.data = baked
    for m in bake:
        m.show_viewport = False
    obj.shape_key_add(name="Basis", from_mix=False)
    for name, co in shapes:
        kb = obj.shape_key_add(name=name, from_mix=False)
        kb.data.foreach_set("co", co)
        kb.value = 0.0
    _swapped.append(record)
    log.info("%s: baked %s into the export with shape keys %s" % (
        obj.name, "+".join(m.name for m in bake), [n for n, _ in shapes]))


def _restore_all():
    """Puts every swapped object back. Safe to call twice; returns None so a timer runs it once."""
    while _swapped:
        record = _swapped.pop()
        obj = record["obj"]
        try:
            obj.data = record["mesh"]
            record["mesh"].use_fake_user = record["fake_user"]
            for m, vis in record["mods"]:
                m.show_viewport = vis
            if record["baked"].users == 0:
                bpy.data.meshes.remove(record["baked"])
        except ReferenceError:
            pass        #the object itself was deleted; nothing left to put back
    return None


class _Log:
    """The export's own log when there is one (its messages pop up after the export), else print."""
    def __init__(self, export_settings):
        self.log = export_settings.get('log')

    def info(self, message):
        print("[keep shape keys] " + message)
        if self.log:
            self.log.info("keep shape keys: " + message)

    def warning(self, message):
        print("[keep shape keys] WARNING " + message)
        if self.log:
            self.log.warning("keep shape keys: " + message, popup=True)


class glTF2ExportUserExtension:
    def __init__(self):
        #Built by the exporter at the start of every export, so this is the live scene's setting.
        self.properties = bpy.context.scene.gltf_keep_shape_keys

    def pre_export_hook(self, export_settings):
        if not self.properties.enabled:
            return
        log = _Log(export_settings)
        #Nothing is dropped when modifiers are not applied, and nothing is wanted without morphs.
        if not export_settings.get('gltf_apply') or not export_settings.get('gltf_morph'):
            return
        _restore_all()      #anything a failed export before this one left swapped
        for obj in _exported_objects(export_settings):
            if obj.type != 'MESH' or not obj.data.shape_keys or len(obj.data.shape_keys.key_blocks) < 2:
                continue
            if obj.mode != 'OBJECT':
                log.warning("%s is in %s mode - its shape keys are left to the exporter" % (obj.name, obj.mode))
                continue
            bake, _ = _bake_modifiers(obj)
            if bake:
                _swap_in_baked(obj, log)
        if _swapped:
            bpy.app.timers.register(_restore_all, first_interval=0.0)

    def post_export_hook(self, export_settings):
        _restore_all()


#--- Settings, saved in the .blend with the scene -------------------------------------------------
class KeepShapeKeysProperties(bpy.types.PropertyGroup):
    enabled: bpy.props.BoolProperty(
        name="Keep shape keys through modifiers",
        description="Bake a mesh's modifiers before its Armature (a Mirror, say) into the export with its "
                    "shape keys, instead of the stock exporter dropping the keys. The scene is put back "
                    "exactly as it was afterwards",
        default=True)


def draw_export(context, layout):
    header, body = layout.panel("GLTF_keep_shape_keys_exporter", default_closed=True)
    header.use_property_split = False
    header.prop(context.scene.gltf_keep_shape_keys, "enabled")
    if body is not None:
        body.label(text="Needs Apply Modifiers and Shape Keys on.")
        body.label(text="The .blend is not changed.")


def register():
    bpy.utils.register_class(KeepShapeKeysProperties)
    bpy.types.Scene.gltf_keep_shape_keys = bpy.props.PointerProperty(type=KeepShapeKeysProperties)
    from io_scene_gltf2 import exporter_extension_layout_draw
    exporter_extension_layout_draw["Keep shape keys through modifiers"] = draw_export


def unregister():
    _restore_all()
    try:
        from io_scene_gltf2 import exporter_extension_layout_draw
        exporter_extension_layout_draw.pop("Keep shape keys through modifiers", None)
    except ImportError:
        pass
    del bpy.types.Scene.gltf_keep_shape_keys
    bpy.utils.unregister_class(KeepShapeKeysProperties)
