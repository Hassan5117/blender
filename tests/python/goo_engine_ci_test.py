"""Cross-platform Goo Engine CI verification suite for Blender 5.2.4 LTS."""

import math
import os
from pathlib import Path
import shutil
import struct
import tempfile
import time

import bpy
from bpy_extras.object_utils import world_to_camera_view
from mathutils import Vector
import numpy as np


REPO_ROOT = Path(__file__).resolve().parents[2]
TMP_DIR = Path(tempfile.mkdtemp(prefix="goo_ci_"))
OUT_DIR = Path(os.environ.get("GOO_TEST_OUT_DIR", str(TMP_DIR / "renders")))
STOCK_43_BLEND = Path(
    os.environ.get(
        "GOO_STOCK_43_BLEND",
        str(REPO_ROOT / "release" / "datafiles" / "preview_grease_pencil.blend"),
    )
)
SOFTWARE_GPU = os.environ.get("GOO_CI_SOFTWARE_GPU", "0") == "1"
SKIP_GPU_RENDER = os.environ.get("GOO_SKIP_GPU_RENDER", "0") == "1"
OUT_DIR.mkdir(parents=True, exist_ok=True)


def reset_scene(engine="GOO_ENGINE"):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.render.engine = engine
    scene.render.resolution_percentage = 100
    scene.eevee.taa_render_samples = 2 if SOFTWARE_GPU else 4
    scene.view_settings.view_transform = "Standard"
    return scene


def look_at(obj, target):
    direction = Vector(target) - obj.location
    obj.rotation_euler = direction.to_track_quat("-Z", "Y").to_euler()


def rgba_mix_sockets(mix_node):
    factor = next(s for s in mix_node.inputs if s.name == "Factor" and s.type == "VALUE")
    in_a = next(s for s in mix_node.inputs if s.name == "A" and s.type == "RGBA")
    in_b = next(s for s in mix_node.inputs if s.name == "B" and s.type == "RGBA")
    out = next(s for s in mix_node.outputs if s.name == "Result" and s.type == "RGBA")
    return factor, in_a, in_b, out


def new_material(name):
    mat = bpy.data.materials.new(name)
    if hasattr(mat, "use_nodes"):
        mat.use_nodes = True
    nt = mat.node_tree
    for n in list(nt.nodes):
        if n.type != "OUTPUT_MATERIAL":
            nt.nodes.remove(n)
    out = next(n for n in nt.nodes if n.type == "OUTPUT_MATERIAL")
    return mat, nt, out


def add_camera(scene, name, loc, target, lens=35.0, clip_end=100.0):
    cam_data = bpy.data.cameras.new(name)
    cam_data.lens = lens
    cam_data.clip_start = 0.1
    cam_data.clip_end = clip_end
    cam_obj = bpy.data.objects.new(name, cam_data)
    scene.collection.objects.link(cam_obj)
    cam_obj.location = Vector(loc)
    look_at(cam_obj, target)
    scene.camera = cam_obj
    return cam_obj


def add_sun(scene, name, light_dir, energy=3.0, color=(1.0, 1.0, 1.0), angle=math.radians(1.0)):
    ld = bpy.data.lights.new(name, type="SUN")
    ld.energy = energy
    ld.color = color
    ld.angle = angle
    lo = bpy.data.objects.new(name, ld)
    scene.collection.objects.link(lo)
    lo.location = Vector((0.0, 0.0, 8.0))
    lo.rotation_euler = Vector(light_dir).normalized().to_track_quat("-Z", "Y").to_euler()
    return lo


def add_spot(
    scene, name, loc, target, energy=500.0, spot_size_deg=55.0, blend=0.25, radius=0.15, color=(1.0, 1.0, 1.0)
):
    ld = bpy.data.lights.new(name, type="SPOT")
    ld.energy = energy
    ld.color = color
    ld.spot_size = math.radians(spot_size_deg)
    ld.spot_blend = blend
    ld.shadow_soft_size = radius
    lo = bpy.data.objects.new(name, ld)
    scene.collection.objects.link(lo)
    lo.location = Vector(loc)
    look_at(lo, target)
    return lo


def add_card(scene, name, loc, width, height, mat, cam_loc=None):
    bpy.ops.mesh.primitive_plane_add(size=1.0, location=loc)
    obj = bpy.context.active_object
    obj.name = name
    obj.scale = (width, height, 1.0)
    if cam_loc is not None:
        look_at(obj, cam_loc)
    else:
        obj.rotation_euler = (math.radians(90.0), 0.0, 0.0)
    obj.data.materials.append(mat)
    return obj


def load_image_rgba(path):
    img = bpy.data.images.load(str(path), check_existing=False)
    w, h = img.size
    buf = np.empty(w * h * 4, dtype=np.float32)
    img.pixels.foreach_get(buf)
    bpy.data.images.remove(img)
    return buf.reshape((h, w, 4))


def sample_ndc(arr, nx, ny, radius=3):
    h, w, _ = arr.shape
    cx = int(np.clip(round(nx * w), radius, w - 1 - radius))
    cy = int(np.clip(round(ny * h), radius, h - 1 - radius))
    patch = arr[cy - radius:cy + radius + 1, cx - radius:cx + radius + 1]
    return patch.mean(axis=(0, 1))


def proj_point(scene, cam, world_co):
    depsgraph = bpy.context.evaluated_depsgraph_get()
    cam_eval = cam.evaluated_get(depsgraph)
    co = world_to_camera_view(scene, cam_eval, Vector(world_co))
    return co.x, co.y


# ---------------------------------------------------------------------------
# Binary .blend parser / SDNA decoder / v404 + v305 converter
# ---------------------------------------------------------------------------

def _read_cstrings(buf, offset, count):
    items = []
    pos = offset
    for _ in range(count):
        end = buf.find(b"\x00", pos)
        if end < 0:
            raise ValueError("Unterminated string in SDNA")
        items.append(buf[pos:end].decode("ascii"))
        pos = end + 1
    pos = (pos + 3) & ~3
    return items, pos


def _member_base_name(name):
    s = name
    while s.startswith("*"):
        s = s[1:]
    if s.startswith("(*"):
        close = s.find(")")
        if close != -1:
            s = s[2:close]
    bracket = s.find("[")
    if bracket != -1:
        s = s[:bracket]
    return s


def _member_array_len(name):
    mul = 1
    i = 0
    while i < len(name):
        if name[i] == "[":
            j = name.find("]", i + 1)
            if j != -1:
                mul *= int(name[i + 1:j])
                i = j + 1
                continue
        i += 1
    return mul


def parse_sdna(dna_bytes):
    if dna_bytes[:4] != b"SDNA":
        raise ValueError("Missing SDNA magic")
    pos = 4
    if dna_bytes[pos:pos + 4] != b"NAME":
        raise ValueError("Missing NAME magic")
    names_num = struct.unpack_from("<I", dna_bytes, pos + 4)[0]
    names, pos = _read_cstrings(dna_bytes, pos + 8, names_num)

    if dna_bytes[pos:pos + 4] != b"TYPE":
        raise ValueError("Missing TYPE magic")
    types_num = struct.unpack_from("<I", dna_bytes, pos + 4)[0]
    types, pos = _read_cstrings(dna_bytes, pos + 8, types_num)

    if dna_bytes[pos:pos + 4] != b"TLEN":
        raise ValueError("Missing TLEN magic")
    tlen_count = types_num
    tlens = list(struct.unpack_from(f"<{tlen_count}H", dna_bytes, pos + 4))
    pos += 4 + tlen_count * 2
    if tlen_count & 1:
        pos += 2

    if dna_bytes[pos:pos + 4] != b"STRC":
        raise ValueError("Missing STRC magic")
    structs_num = struct.unpack_from("<I", dna_bytes, pos + 4)[0]
    pos += 8
    structs = []
    for _ in range(structs_num):
        type_idx, members_num = struct.unpack_from("<HH", dna_bytes, pos)
        pos += 4
        members = []
        for _ in range(members_num):
            m_type_idx, m_name_idx = struct.unpack_from("<HH", dna_bytes, pos)
            pos += 4
            members.append([m_type_idx, m_name_idx])
        structs.append([type_idx, members])

    return {
        "names": names,
        "types": types,
        "tlens": tlens,
        "structs": structs,
    }


def encode_sdna(sdna):
    out = bytearray(b"SDNA")
    out += b"NAME" + struct.pack("<I", len(sdna["names"]))
    for n in sdna["names"]:
        out += n.encode("ascii") + b"\x00"
    while len(out) & 3:
        out += b"\x00"

    out += b"TYPE" + struct.pack("<I", len(sdna["types"]))
    for t in sdna["types"]:
        out += t.encode("ascii") + b"\x00"
    while len(out) & 3:
        out += b"\x00"

    out += b"TLEN" + struct.pack(f"<{len(sdna['tlens'])}H", *sdna["tlens"])
    if len(sdna["tlens"]) & 1:
        out += b"\x00\x00"

    out += b"STRC" + struct.pack("<I", len(sdna["structs"]))
    for type_idx, members in sdna["structs"]:
        out += struct.pack("<HH", type_idx, len(members))
        for m_type_idx, m_name_idx in members:
            out += struct.pack("<HH", m_type_idx, m_name_idx)
    return bytes(out)


def sdna_struct_by_name(sdna, struct_name):
    for s_idx, (t_idx, _) in enumerate(sdna["structs"]):
        if sdna["types"][t_idx] == struct_name:
            return s_idx
    raise KeyError(f"Struct {struct_name} not in SDNA")


def sdna_member_offset(sdna, struct_name, field_path):
    cur_struct = struct_name
    total_off = 0
    parts = field_path.split(".")
    for pi, part in enumerate(parts):
        s_idx = sdna_struct_by_name(sdna, cur_struct)
        _, members = sdna["structs"][s_idx]
        off = 0
        found = False
        is_ptr = False
        for m_type_idx, m_name_idx in members:
            m_name = sdna["names"][m_name_idx]
            m_type = sdna["types"][m_type_idx]
            is_ptr = m_name.startswith("*") or m_name.startswith("(*")
            elem_sz = 8 if is_ptr else sdna["tlens"][m_type_idx]
            sz = elem_sz * _member_array_len(m_name)
            if m_name == part or _member_base_name(m_name) == part:
                total_off += off
                cur_struct = m_type
                found = True
                break
            off += sz
        if not found:
            raise KeyError(f"Member {part} not found in {cur_struct} (path={field_path})")
        if pi < len(parts) - 1 and is_ptr:
            raise ValueError(f"Cannot traverse pointer {part}")
    return total_off


def sdna_rename_struct_member(sdna, struct_name, old_member_name, new_member_name):
    s_idx = sdna_struct_by_name(sdna, struct_name)
    _, members = sdna["structs"][s_idx]
    if new_member_name in sdna["names"]:
        new_idx = sdna["names"].index(new_member_name)
    else:
        new_idx = len(sdna["names"])
        sdna["names"].append(new_member_name)
    for m in members:
        if sdna["names"][m[1]] == old_member_name:
            m[1] = new_idx
            return
    raise KeyError(f"Member {old_member_name} not found in {struct_name}")


def sdna_rename_struct_type(sdna, old_type_name, new_type_name):
    for i, name in enumerate(sdna["types"]):
        if name == old_type_name:
            sdna["types"][i] = new_type_name
            return
    raise KeyError(f"Type {old_type_name} not in SDNA")


def _strip_goo_sdna(target_sdna, suffix):
    sdna_rename_struct_member(target_sdna, "Lamp", "light_group_bits[4]", f"_pad_{suffix}_la[4]")
    sdna_rename_struct_member(target_sdna, "Lamp", "contact_dist", f"_pad_{suffix}_cd")
    sdna_rename_struct_member(target_sdna, "Material", "light_group_bits[4]", f"_pad_{suffix}_ma[4]")
    sdna_rename_struct_member(target_sdna, "SceneEEVEE", "bloom_threshold", f"_pad_{suffix}_bl")
    sdna_rename_struct_member(target_sdna, "SceneEEVEE", "gtao_factor", f"_pad_{suffix}_gf")
    sdna_rename_struct_member(target_sdna, "SceneEEVEE", "goo_gtao_quality", f"_pad_{suffix}_gq")
    sdna_rename_struct_member(target_sdna, "SceneEEVEE", "ssr_quality", f"_pad_{suffix}_sq")
    sdna_rename_struct_member(target_sdna, "SceneEEVEE", "sss_samples", f"_pad_{suffix}_ss")
    sdna_rename_struct_type(target_sdna, "NodeShaderInfo", "_pad_NodeShInfo")
    sdna_rename_struct_type(target_sdna, "NodeSdfPrimitive", "_pad_NodeSdfPrim")


def read_blend_blocks(path):
    with open(path, "rb") as f:
        raw = f.read()
    assert raw[:7] == b"BLENDER", f"Not a blend file: {raw[:17]!r}"
    if raw[7:9] == b"17":
        pos = 17
        large = True
    else:
        pos = 12
        large = False

    blocks = []
    while pos < len(raw):
        if large:
            code_b, sdna_nr, old_ptr, length, nr = struct.unpack_from("<4siQqq", raw, pos)
            pos += 32
        else:
            code_b, length, old_ptr, sdna_nr, nr = struct.unpack_from("<4siQii", raw, pos)
            pos += 24
        payload = bytearray(raw[pos:pos + length])
        pos += length
        blocks.append([code_b, sdna_nr, old_ptr, nr, payload])
        if code_b == b"ENDB":
            break
    return blocks


def write_v404_blend(path, blocks, header=b"BLENDER-v404"):
    with open(path, "wb") as f:
        f.write(header)
        for code_b, sdna_nr, old_ptr, nr, payload in blocks:
            f.write(struct.pack("<4siQii", code_b, len(payload), old_ptr, sdna_nr, nr))
            f.write(payload)


def _strip_node_storage(blocks, sdna, node_idname_bytes):
    bnode_sdna_nr = sdna_struct_by_name(sdna, "bNode")
    off_bnode_idname = sdna_member_offset(sdna, "bNode", "idname")
    off_bnode_storage = sdna_member_offset(sdna, "bNode", "storage")
    dropped = set()
    for code_b, sdna_nr, _old_ptr, nr, payload in blocks:
        if code_b == b"DATA" and sdna_nr == bnode_sdna_nr:
            bnode_sz = len(payload) // max(1, nr)
            for idx in range(nr):
                base = idx * bnode_sz
                idname = bytes(payload[base + off_bnode_idname:base + off_bnode_idname + 64]).split(b"\x00", 1)[0]
                if idname == node_idname_bytes:
                    ptr = struct.unpack_from("<Q", payload, base + off_bnode_storage)[0]
                    if ptr != 0:
                        dropped.add(ptr)
                        struct.pack_into("<Q", payload, base + off_bnode_storage, 0)
    return dropped


def convert_52_to_v404_fixtures(src_blend, goo44_out, stock_src_blend, stock44_out, goo35_src_blend, goo35_out):
    import copy

    base_blocks = read_blend_blocks(src_blend)
    dna_block = next(b for b in base_blocks if b[0] == b"DNA1")
    sdna = parse_sdna(bytes(dna_block[4]))

    off_glob_minver = sdna_member_offset(sdna, "FileGlobal", "minversion")
    off_glob_minsubver = sdna_member_offset(sdna, "FileGlobal", "minsubversion")
    off_sc_name = sdna_member_offset(sdna, "Scene", "id.name")
    off_sc_engine = sdna_member_offset(sdna, "Scene", "r.engine")
    off_sc_gtao_q = sdna_member_offset(sdna, "Scene", "eevee.goo_gtao_quality")
    off_sc_fast_gi_q = sdna_member_offset(sdna, "Scene", "eevee.gtao_quality")
    off_ma_name = sdna_member_offset(sdna, "Material", "id.name")
    off_ma_use_nodes = sdna_member_offset(sdna, "Material", "use_nodes")
    off_wo_use_nodes = sdna_member_offset(sdna, "World", "use_nodes")

    # 1) Goo 4.4 fixture
    goo_blocks = copy.deepcopy(base_blocks)
    goo_sdna = copy.deepcopy(sdna)
    sdna_rename_struct_member(goo_sdna, "SceneEEVEE", "gtao_quality", "fast_gi_quality")
    sdna_rename_struct_member(goo_sdna, "SceneEEVEE", "goo_gtao_quality", "gtao_quality")

    dropped_storage_ptrs = _strip_node_storage(goo_blocks, sdna, b"ShaderNodeTwirl")

    filtered_goo_blocks = []
    for blk in goo_blocks:
        code_b, sdna_nr, old_ptr, nr, payload = blk
        if code_b == b"DATA" and old_ptr in dropped_storage_ptrs:
            continue
        if code_b == b"GLOB":
            payload[0:4] = b"  34"
            struct.pack_into("<H", payload, off_glob_minver, 402)
            struct.pack_into("<H", payload, off_glob_minsubver, 0)
        elif code_b == b"DNA1":
            blk[4] = bytearray(encode_sdna(goo_sdna))
        elif code_b == b"SC\x00\x00":
            sc_name = bytes(payload[off_sc_name:off_sc_name + 66]).split(b"\x00", 1)[0]
            payload[off_sc_engine:off_sc_engine + 32] = b"\x00" * 32
            if sc_name == b"SCGooScene":
                payload[off_sc_engine:off_sc_engine + 14] = b"BLENDER_EEVEE\x00"
                struct.pack_into("<f", payload, off_sc_gtao_q, 0.65)
                struct.pack_into("<f", payload, off_sc_fast_gi_q, 0.25)
            else:
                payload[off_sc_engine:off_sc_engine + 19] = b"BLENDER_EEVEE_NEXT\x00"
        elif code_b == b"MA\x00\x00":
            ma_name = bytes(payload[off_ma_name:off_ma_name + 66]).split(b"\x00", 1)[0]
            payload[off_ma_use_nodes] = 0 if ma_name == b"MALegacyNoNodes" else 1
        elif code_b == b"WO\x00\x00":
            struct.pack_into("<h", payload, off_wo_use_nodes, 1)
        filtered_goo_blocks.append(blk)

    write_v404_blend(goo44_out, filtered_goo_blocks)

    # 2) Stock Blender 4.4 fixture
    stock_blocks = read_blend_blocks(stock_src_blend)
    stock_dna_blk = next(b for b in stock_blocks if b[0] == b"DNA1")
    stock_sdna = parse_sdna(bytes(stock_dna_blk[4]))
    s_off_minver = sdna_member_offset(stock_sdna, "FileGlobal", "minversion")
    s_off_minsub = sdna_member_offset(stock_sdna, "FileGlobal", "minsubversion")
    s_off_engine = sdna_member_offset(stock_sdna, "Scene", "r.engine")
    s_off_ma_nodes = sdna_member_offset(stock_sdna, "Material", "use_nodes")
    s_off_wo_nodes = sdna_member_offset(stock_sdna, "World", "use_nodes")
    _strip_goo_sdna(stock_sdna, "st")

    for blk in stock_blocks:
        code_b, sdna_nr, old_ptr, nr, payload = blk
        if code_b == b"GLOB":
            payload[0:4] = b"  32"
            struct.pack_into("<H", payload, s_off_minver, 402)
            struct.pack_into("<H", payload, s_off_minsub, 0)
        elif code_b == b"DNA1":
            blk[4] = bytearray(encode_sdna(stock_sdna))
        elif code_b == b"SC\x00\x00":
            payload[s_off_engine:s_off_engine + 32] = b"\x00" * 32
            payload[s_off_engine:s_off_engine + 14] = b"BLENDER_EEVEE\x00"
        elif code_b == b"MA\x00\x00":
            payload[s_off_ma_nodes] = 1
        elif code_b == b"WO\x00\x00":
            struct.pack_into("<h", payload, s_off_wo_nodes, 1)

    write_v404_blend(stock44_out, stock_blocks)

    # 3) Goo 3.5 fixture
    g35_blocks = read_blend_blocks(goo35_src_blend)
    g35_dna_blk = next(b for b in g35_blocks if b[0] == b"DNA1")
    g35_sdna = parse_sdna(bytes(g35_dna_blk[4]))
    g_off_minver = sdna_member_offset(g35_sdna, "FileGlobal", "minversion")
    g_off_minsub = sdna_member_offset(g35_sdna, "FileGlobal", "minsubversion")
    g_off_engine = sdna_member_offset(g35_sdna, "Scene", "r.engine")
    g_off_ma_nodes = sdna_member_offset(g35_sdna, "Material", "use_nodes")
    g_off_wo_nodes = sdna_member_offset(g35_sdna, "World", "use_nodes")
    dropped_g35 = _strip_node_storage(g35_blocks, g35_sdna, b"ShaderNodeShaderInfo")
    _strip_goo_sdna(g35_sdna, "g35")

    filtered_g35 = []
    for blk in g35_blocks:
        code_b, sdna_nr, old_ptr, nr, payload = blk
        if code_b == b"DATA" and old_ptr in dropped_g35:
            continue
        if code_b == b"GLOB":
            payload[0:4] = b"   0"
            struct.pack_into("<H", payload, g_off_minver, 305)
            struct.pack_into("<H", payload, g_off_minsub, 0)
        elif code_b == b"DNA1":
            blk[4] = bytearray(encode_sdna(g35_sdna))
        elif code_b == b"SC\x00\x00":
            payload[g_off_engine:g_off_engine + 32] = b"\x00" * 32
            payload[g_off_engine:g_off_engine + 14] = b"BLENDER_EEVEE\x00"
        elif code_b == b"MA\x00\x00":
            payload[g_off_ma_nodes] = 1
        elif code_b == b"WO\x00\x00":
            struct.pack_into("<h", payload, g_off_wo_nodes, 1)
        filtered_g35.append(blk)

    write_v404_blend(goo35_out, filtered_g35, header=b"BLENDER-v305")


# ---------------------------------------------------------------------------
# Test 1: Author scene, convert to v404/v305 Goo & Stock fixtures, load & verify
# ---------------------------------------------------------------------------

def build_compat_source_blend(src_blend):
    scene = reset_scene("GOO_ENGINE")
    scene.name = "GooScene"
    scene.render.resolution_x = 320 if SOFTWARE_GPU else 640
    scene.render.resolution_y = 180 if SOFTWARE_GPU else 360
    scene.eevee.taa_render_samples = 2 if SOFTWARE_GPU else 4
    scene.eevee.gtao_quality = 0.65
    scene.eevee.use_gtao = True
    scene.eevee.use_bloom = True
    scene.eevee.bloom_threshold = 1.2
    scene.eevee.bloom_intensity = 0.08
    scene.eevee.use_ssr = True
    scene.eevee.use_ssr_refraction = True
    scene.eevee.use_volumetric_lights = True
    scene.eevee.volumetric_samples = 16 if SOFTWARE_GPU else 32
    scene.eevee.sss_samples = 7
    scene.render.use_motion_blur = True
    scene.eevee.motion_blur_steps = 2

    wo = bpy.data.worlds.new("Goo44World")
    wo.use_nodes = True
    bg = wo.node_tree.nodes["Background"]
    bg.inputs["Color"].default_value = (0.04, 0.06, 0.12, 1.0)
    bg.inputs["Strength"].default_value = 1.0
    scene.world = wo

    cam = add_camera(scene, "CompatCam", (0.0, -7.8, 1.8), (0.0, 0.0, 1.15), lens=34.0)
    cam.data.dof.use_dof = True
    cam.data.dof.focus_distance = 7.8
    cam.data.dof.aperture_fstop = 8.0

    add_sun(scene, "CompatSun", (-0.5, 0.4, -0.7), energy=3.0)
    spot = add_spot(scene, "CompatSpot", (2.5, -3.5, 3.2), (0.0, 0.0, 1.2), energy=400.0, color=(0.3, 0.85, 1.0))
    spot_grp = spot.data.light_groups.groups.add()
    spot_grp.name = "Accent"
    spot.data.light_groups.use_default = False

    bpy.ops.mesh.primitive_plane_add(size=18.0, location=(0.0, 0.0, 0.0))
    floor = bpy.context.active_object
    floor.name = "CompatFloor"
    fmat, fnt, fout = new_material("FloorClip")
    fmat.blend_method = "CLIP"
    fmat.shadow_method = "CLIP"
    fmat.check_shadow_id = True
    fbsdf = fnt.nodes.new("ShaderNodeBsdfPrincipled")
    fbsdf.inputs["Base Color"].default_value = (0.18, 0.20, 0.24, 1.0)
    fbsdf.inputs["Roughness"].default_value = 0.55
    fnt.links.new(fbsdf.outputs["BSDF"], fout.inputs["Surface"])
    floor.data.materials.append(fmat)

    dual_mat, dnt, d_out_eevee = new_material("DualOutputMat")
    d_out_eevee.target = "EEVEE"
    em_e = dnt.nodes.new("ShaderNodeEmission")
    em_e.inputs["Color"].default_value = (0.10, 0.92, 0.25, 1.0)
    em_e.inputs["Strength"].default_value = 1.2
    dnt.links.new(em_e.outputs["Emission"], d_out_eevee.inputs["Surface"])
    d_out_cyc = dnt.nodes.new("ShaderNodeOutputMaterial")
    d_out_cyc.target = "CYCLES"
    em_c = dnt.nodes.new("ShaderNodeEmission")
    em_c.inputs["Color"].default_value = (0.95, 0.05, 0.05, 1.0)
    em_c.inputs["Strength"].default_value = 1.2
    dnt.links.new(em_c.outputs["Emission"], d_out_cyc.inputs["Surface"])
    add_card(scene, "CardDualOutput", (-2.2, 0.0, 2.0), 1.2, 0.9, dual_mat)

    leg_mat = bpy.data.materials.new("LegacyNoNodes")
    leg_mat.diffuse_color = (0.95, 0.45, 0.05, 1.0)
    leg_mat.roughness = 0.45
    leg_mat.metallic = 0.0
    add_card(scene, "CardLegacyNoNodes", (-0.75, 0.0, 2.0), 1.2, 0.9, leg_mat)

    mat_2d, nt2d, out2d = new_material("Goo2DNodes")
    mat_2d.blend_method = "HASHED"
    mat_2d.shadow_method = "HASHED"
    tc = nt2d.nodes.new("ShaderNodeTexCoord")
    tw = nt2d.nodes.new("ShaderNodeTwirl")
    tw.inputs["Center"].default_value = (0.5, 0.5, 0.0)
    tw.inputs["Amount"].default_value = 6.0
    hx = nt2d.nodes.new("ShaderNodeTexHexagon")
    hx.value_mode = "SDF"
    wr = nt2d.nodes.new("ShaderNodeWaterRipples")
    wr.mode = "DROPS"
    wr.inputs["Time"].default_value = 1.1
    ramp = nt2d.nodes.new("ShaderNodeOKLabColorRamp")
    ramp.color_ramp.elements[0].color = (0.08, 0.15, 0.85, 1.0)
    ramp.color_ramp.elements[1].color = (0.98, 0.80, 0.15, 1.0)
    mul2d = nt2d.nodes.new("ShaderNodeMath")
    mul2d.operation = "MULTIPLY"
    em2d = nt2d.nodes.new("ShaderNodeEmission")
    em2d.inputs["Strength"].default_value = 1.2
    nt2d.links.new(tc.outputs["UV"], tw.inputs["Vector"])
    nt2d.links.new(tw.outputs["Vector"], hx.inputs["Vector"])
    nt2d.links.new(tw.outputs["Vector"], wr.inputs["Vector"])
    nt2d.links.new(hx.outputs["Value"], mul2d.inputs[0])
    nt2d.links.new(wr.outputs["Mask"], mul2d.inputs[1])
    nt2d.links.new(mul2d.outputs["Value"], ramp.inputs["Fac"])
    nt2d.links.new(ramp.outputs["Color"], em2d.inputs["Color"])
    nt2d.links.new(em2d.outputs["Emission"], out2d.inputs["Surface"])
    add_card(scene, "CardGoo2D", (0.75, 0.0, 2.0), 1.2, 0.9, mat_2d)

    mat_sdf, nts, outs = new_material("GooSdfNodes")
    tcs = nts.nodes.new("ShaderNodeTexCoord")
    vop = nts.nodes.new("ShaderNodeSdfVectorOp")
    vop.operation = "MAP_05"
    prim = nts.nodes.new("ShaderNodeSdfPrimitive")
    prim.mode = "STAR_2D"
    prim.inputs["Size"].default_value = 0.28
    sn = nts.nodes.new("ShaderNodeSdfNoise")
    sn.inputs["Detail"].default_value = 3.0
    op = nts.nodes.new("ShaderNodeSdfOp")
    op.operation = "MASK"
    op.inputs["Value"].default_value = 0.02
    li = nts.nodes.new("ShaderNodeLightInfo")
    li.light_object = spot
    mix_li = nts.nodes.new("ShaderNodeMix")
    mix_li.data_type = "RGBA"
    fac, ca, cb, cres = rgba_mix_sockets(mix_li)
    ca.default_value = (0.08, 0.08, 0.14, 1.0)
    ems = nts.nodes.new("ShaderNodeEmission")
    ems.inputs["Strength"].default_value = 1.3
    nts.links.new(tcs.outputs["UV"], vop.inputs["Vector1"])
    nts.links.new(vop.outputs["Vector"], prim.inputs["Vector"])
    nts.links.new(vop.outputs["Vector"], sn.inputs["Position"])
    nts.links.new(prim.outputs["Distance"], sn.inputs["Distance"])
    nts.links.new(sn.outputs["Distance"], op.inputs[0])
    nts.links.new(op.outputs["Distance"], fac)
    nts.links.new(li.outputs["Light Color"], cb)
    nts.links.new(cres, ems.inputs["Color"])
    nts.links.new(ems.outputs["Emission"], outs.inputs["Surface"])
    add_card(scene, "CardGooSdf", (2.2, 0.0, 2.0), 1.2, 0.9, mat_sdf)

    mat_npr, ntn, outn = new_material("GooNprNodes")
    mat_npr.use_sss_translucency = True
    mat_npr.light_groups.groups.add().name = "Accent"
    sh = ntn.nodes.new("ShaderNodeShaderInfo")
    sh.use_own_light_groups = True
    sh.light_group_bits = (0, 0, 0, 3)
    curv = ntn.nodes.new("ShaderNodeCurvature")
    ssi = ntn.nodes.new("ShaderNodeScreenspaceInfo")
    add1 = ntn.nodes.new("ShaderNodeMix")
    add1.data_type = "RGBA"
    add1.blend_type = "ADD"
    f1, a1, _b1, r1 = rgba_mix_sockets(add1)
    f1.default_value = 0.25
    add2 = ntn.nodes.new("ShaderNodeMix")
    add2.data_type = "RGBA"
    add2.blend_type = "ADD"
    f2, a2, b2, r2 = rgba_mix_sockets(add2)
    f2.default_value = 0.1
    sd = ntn.nodes.new("ShaderNodeSetDepth")
    sd.inputs["View Depth"].default_value = 7.4
    emn = ntn.nodes.new("ShaderNodeEmission")
    ntn.links.new(sh.outputs["Diffuse Shading"], a1)
    ntn.links.new(curv.outputs["Scene Rim"], f1)
    ntn.links.new(r1, a2)
    ntn.links.new(ssi.outputs["Scene Color"], b2)
    ntn.links.new(r2, emn.inputs["Color"])
    ntn.links.new(emn.outputs["Emission"], sd.inputs["Shader"])
    ntn.links.new(sd.outputs["Shader"], outn.inputs["Surface"])
    bpy.ops.mesh.primitive_uv_sphere_add(radius=0.55, location=(-0.8, -0.3, 0.6))
    sph = bpy.context.active_object
    bpy.ops.object.shade_smooth()
    sph.data.materials.append(mat_npr)

    mat_glass, ntg, outg = new_material("GooBlendGlass")
    mat_glass.blend_method = "BLEND"
    mat_glass.use_screen_refraction = True
    mat_glass.refraction_depth = 0.5
    pg = ntg.nodes.new("ShaderNodeBsdfPrincipled")
    pg.inputs["Base Color"].default_value = (0.7, 0.9, 1.0, 1.0)
    pg.inputs["Roughness"].default_value = 0.08
    if "Transmission Weight" in pg.inputs:
        pg.inputs["Transmission Weight"].default_value = 0.9
    ntg.links.new(pg.outputs["BSDF"], outg.inputs["Surface"])
    bpy.ops.mesh.primitive_uv_sphere_add(radius=0.55, location=(0.8, -0.3, 0.6))
    gsph = bpy.context.active_object
    bpy.ops.object.shade_smooth()
    gsph.data.materials.append(mat_glass)

    sc2 = bpy.data.scenes.new("EeveeNextScene")
    sc2.render.engine = "BLENDER_EEVEE"
    bpy.ops.mesh.primitive_cube_add(size=1.0, location=(0.0, 0.0, 0.0))

    bpy.ops.wm.save_as_mainfile(filepath=str(src_blend), compress=False)


def build_stock_source_blend(stock_src_blend):
    scene = reset_scene("BLENDER_EEVEE")
    scene.name = "Stock44Scene"
    add_camera(scene, "StockCam", (0.0, -5.0, 1.5), (0.0, 0.0, 0.5))
    bpy.ops.mesh.primitive_cube_add(size=1.2, location=(0.0, 0.0, 0.5))
    cube = bpy.context.active_object
    mat, nt, out = new_material("StockMat")
    bsdf = nt.nodes.new("ShaderNodeBsdfPrincipled")
    bsdf.inputs["Base Color"].default_value = (0.6, 0.4, 0.2, 1.0)
    nt.links.new(bsdf.outputs["BSDF"], out.inputs["Surface"])
    cube.data.materials.append(mat)
    bpy.ops.wm.save_as_mainfile(filepath=str(stock_src_blend), compress=False)


def build_goo35_source_blend(goo35_src_blend):
    scene = reset_scene("GOO_ENGINE")
    scene.name = "Goo35Scene"
    scene.render.resolution_x = 320
    scene.render.resolution_y = 240
    scene.eevee.taa_render_samples = 2 if SOFTWARE_GPU else 4
    scene.eevee.use_bloom = True
    wo = bpy.data.worlds.new("Goo35World")
    wo.use_nodes = True
    wo.node_tree.nodes["Background"].inputs["Color"].default_value = (0.02, 0.03, 0.06, 1.0)
    scene.world = wo
    add_camera(scene, "Compat35Cam", (0.0, -4.5, 0.8), (0.0, 0.0, 0.8), lens=35.0)
    add_sun(scene, "Compat35Sun", (0.0, 0.85, -0.5), energy=2.5)
    bpy.ops.curve.primitive_bezier_circle_add(radius=1.2, location=(0.0, 0.0, 0.8))
    disc = bpy.context.active_object
    disc.rotation_euler = (math.radians(90.0), 0.0, 0.0)
    disc.data.dimensions = "2D"
    disc.data.fill_mode = "BOTH"
    mat, nt, out = new_material("Goo35Toon")
    mat.blend_method = "CLIP"
    mat.shadow_method = "CLIP"
    sh = nt.nodes.new("ShaderNodeShaderInfo")
    em = nt.nodes.new("ShaderNodeEmission")
    em.inputs["Strength"].default_value = 1.2
    nt.links.new(sh.outputs["Diffuse Shading"], em.inputs["Color"])
    nt.links.new(em.outputs["Emission"], out.inputs["Surface"])
    disc.data.materials.append(mat)
    txt = bpy.data.texts.new(".version_warning.py")
    txt.use_module = True
    txt.from_string("# Goo 3.5 warning script\nassert 'Goo Engine' in __import__('bpy').app.version_string\n")
    bpy.ops.wm.save_as_mainfile(filepath=str(goo35_src_blend), compress=False)


def test_1_goo44_and_stock4x_compat():
    print("\n=== TEST 1: Goo 3.5 / 4.4 & Stock 4.x .blend Compatibility & Migration ===")
    assert bpy.app.version_string == "5.2.4 LTS Goo Engine", (
        f"Unexpected bpy.app.version_string: {bpy.app.version_string!r}"
    )
    print(f"PASS: bpy.app.version_string == {bpy.app.version_string!r}")

    src_blend = TMP_DIR / "m12_src_52.blend"
    goo44_blend = TMP_DIR / "goo44_fixture.blend"
    stock_src_blend = TMP_DIR / "m12_stock_src_52.blend"
    stock44_blend = TMP_DIR / "stock44_fixture.blend"
    goo35_src_blend = TMP_DIR / "m12_goo35_src_52.blend"
    goo35_blend = TMP_DIR / "goo35_fixture.blend"

    build_compat_source_blend(src_blend)
    build_stock_source_blend(stock_src_blend)
    build_goo35_source_blend(goo35_src_blend)
    convert_52_to_v404_fixtures(
        src_blend, goo44_blend, stock_src_blend, stock44_blend, goo35_src_blend, goo35_blend
    )

    if STOCK_43_BLEND.exists():
        bpy.ops.wm.open_mainfile(filepath=str(STOCK_43_BLEND))
        for sc in bpy.data.scenes:
            assert sc.render.engine == "BLENDER_EEVEE", (
                f"Stock 4.3 scene {sc.name} migrated unexpectedly to {sc.render.engine}"
            )
        print(f"PASS: Stock 4.3 ({STOCK_43_BLEND.name}) preserved engine BLENDER_EEVEE")
    else:
        raise FileNotFoundError(f"Missing Stock 4.3 fixture blend: {STOCK_43_BLEND}")

    bpy.ops.wm.open_mainfile(filepath=str(stock44_blend))
    for sc in bpy.data.scenes:
        assert sc.render.engine == "BLENDER_EEVEE", (
            f"Stock 4.4 scene {sc.name} migrated unexpectedly to {sc.render.engine}"
        )
    print("PASS: Stock 4.4 fixture preserved engine BLENDER_EEVEE")

    bpy.ops.wm.open_mainfile(filepath=str(goo44_blend))
    goo_sc = bpy.data.scenes["GooScene"]
    ee_sc = bpy.data.scenes["EeveeNextScene"]
    assert goo_sc.render.engine == "GOO_ENGINE", f"Expected GOO_ENGINE, got {goo_sc.render.engine}"
    assert ee_sc.render.engine == "BLENDER_EEVEE", f"Expected BLENDER_EEVEE, got {ee_sc.render.engine}"
    assert abs(goo_sc.eevee.gtao_quality - 0.65) < 1e-5, (
        f"goo_gtao_quality not preserved from Goo 4.4 gtao_quality: {goo_sc.eevee.gtao_quality}"
    )

    found_types = set()
    for nt in [m.node_tree for m in bpy.data.materials if m.node_tree] + [
        w.node_tree for w in bpy.data.worlds if w.node_tree
    ]:
        for node in nt.nodes:
            assert node.bl_idname != "NodeUndefined" and node.type != "CUSTOM", (
                f"Undefined node in {nt.name}: {node.bl_idname}"
            )
            found_types.add(node.bl_idname)
        for link in nt.links:
            assert link.is_valid, f"Invalid link in {nt.name}: {link.from_node.name} -> {link.to_node.name}"

    expected_13 = {
        "ShaderNodeShaderInfo",
        "ShaderNodeCurvature",
        "ShaderNodeScreenspaceInfo",
        "ShaderNodeLightInfo",
        "ShaderNodeSetDepth",
        "ShaderNodeOKLabColorRamp",
        "ShaderNodeSdfPrimitive",
        "ShaderNodeSdfOp",
        "ShaderNodeSdfVectorOp",
        "ShaderNodeSdfNoise",
        "ShaderNodeTexHexagon",
        "ShaderNodeTwirl",
        "ShaderNodeWaterRipples",
    }
    missing = expected_13 - found_types
    assert not missing, f"Missing Goo nodes after v404 load: {missing}"

    tw_mat = bpy.data.materials["Goo2DNodes"]
    tw_copy = tw_mat.copy()
    bpy.data.materials.remove(tw_copy)
    print("PASS: Goo 4.4 compat migrated cleanly (all 13 Goo shader nodes intact)")

    if not SKIP_GPU_RENDER:
        bpy.context.window.scene = goo_sc
        exr_path = TMP_DIR / "goo44_compat.exr"
        goo_sc.render.image_settings.file_format = "OPEN_EXR"
        goo_sc.render.image_settings.color_depth = "32"
        goo_sc.render.filepath = str(exr_path)
        bpy.ops.render.render(write_still=True)
        arr = load_image_rgba(exr_path)[..., :3]
        assert np.all(np.isfinite(arr)), "Non-finite pixels in migrated Goo 4.4 render"

        cam = goo_sc.camera
        u_dual, v_dual = proj_point(goo_sc, cam, (-2.2, 0.0, 2.0))
        u_leg, v_leg = proj_point(goo_sc, cam, (-0.75, 0.0, 2.0))
        patch_r = 3 if SOFTWARE_GPU else 5
        c_dual = sample_ndc(arr, u_dual, v_dual, radius=patch_r)
        c_leg = sample_ndc(arr, u_leg, v_leg, radius=patch_r)
        print(f"DualOutputMat sample={c_dual.round(3)}, LegacyNoNodes sample={c_leg.round(3)}")
        assert c_dual[1] > 2.0 * c_dual[0] and c_dual[1] > 0.3, (
            f"Dual SHD_OUTPUT_EEVEE material did not render green EEVEE branch: {c_dual}"
        )
        assert c_leg[0] > 2.0 * c_leg[2] and c_leg[0] > 0.25, (
            f"Legacy use_nodes=0 material did not render warm orange: {c_leg}"
        )

        png_path = OUT_DIR / "goo_ci_goo44_compat.png"
        goo_sc.render.image_settings.file_format = "PNG"
        goo_sc.render.image_settings.color_depth = "8"
        goo_sc.render.filepath = str(png_path)
        bpy.ops.render.render(write_still=True)
        print(f"PASS: Goo 4.4 compat rendered cleanly -> {png_path}")

    bpy.ops.wm.open_mainfile(filepath=str(goo35_blend))
    sc35 = bpy.data.scenes["Goo35Scene"]
    assert sc35.render.engine == "GOO_ENGINE", f"Expected Goo 3.5 migration to GOO_ENGINE, got {sc35.render.engine}"
    toon35 = bpy.data.materials["Goo35Toon"]
    assert toon35.blend_method == "CLIP", f"4.2 versioning corrupted blend_method to {toon35.blend_method}"
    assert bpy.data.texts[".version_warning.py"].use_module is False, (
        "TX.version_warning.py still has TXT_ISSCRIPT enabled"
    )
    toon35_copy = toon35.copy()
    bpy.data.materials.remove(toon35_copy)

    if not SKIP_GPU_RENDER:
        exr35 = TMP_DIR / "goo35_compat.exr"
        sc35.render.image_settings.file_format = "OPEN_EXR"
        sc35.render.image_settings.color_depth = "32"
        sc35.render.filepath = str(exr35)
        bpy.context.window.scene = sc35
        bpy.ops.render.render(write_still=True)
        arr35 = load_image_rgba(exr35)[..., :3]
        assert np.all(np.isfinite(arr35)), "Non-finite pixels in migrated Goo 3.5 render"
        ux35, uy35 = proj_point(sc35, sc35.camera, (0.0, 0.0, 0.8))
        c35 = sample_ndc(arr35, ux35, uy35, radius=5)
        assert c35.mean() > 0.1, f"Goo 3.5 ShaderNodeShaderInfo rendered black/dark: {c35}"
        print(f"PASS: Goo 3.5 compat migrated (engine=GOO_ENGINE, blend_method=CLIP, sample={c35.round(3)})")
    else:
        print("PASS: Goo 3.5 compat migrated (engine=GOO_ENGINE, blend_method=CLIP, GPU render skipped)")


# ---------------------------------------------------------------------------
# Test 2: Combined Multi-Effect Stress Showcase
# ---------------------------------------------------------------------------

def test_2_master_showcase():
    print("\n=== TEST 2: Combined Multi-Effect Stress Showcase ===")
    sc = reset_scene("GOO_ENGINE")
    sc.view_settings.view_transform = (
        "AgX"
        if "AgX" in [i.identifier for i in sc.view_settings.bl_rna.properties["view_transform"].enum_items]
        else "Standard"
    )
    if SOFTWARE_GPU:
        sc.render.resolution_x = 320
        sc.render.resolution_y = 180
        sc.eevee.taa_render_samples = 2
        sc.eevee.volumetric_samples = 16
        sc.eevee.motion_blur_steps = 2
    else:
        sc.render.resolution_x = 960
        sc.render.resolution_y = 540
        sc.eevee.taa_render_samples = 8
        sc.eevee.volumetric_samples = 48
        sc.eevee.motion_blur_steps = 6

    sc.render.hair_type = "STRIP"
    sc.eevee.use_gtao = True
    sc.eevee.gtao_distance = 0.35
    sc.eevee.use_bloom = True
    sc.eevee.bloom_threshold = 1.4
    sc.eevee.bloom_intensity = 0.08
    sc.eevee.use_ssr = True
    sc.eevee.use_ssr_refraction = True
    sc.eevee.use_ssr_halfres = False
    sc.eevee.sss_samples = 9
    sc.eevee.use_bokeh_high_quality_slight_defocus = True
    sc.eevee.use_volumetric_lights = True
    sc.eevee.use_volumetric_shadows = True
    sc.eevee.volumetric_tile_size = 4 if isinstance(sc.eevee.volumetric_tile_size, int) else "4"
    sc.render.use_motion_blur = True
    sc.render.motion_blur_shutter = 0.75
    sc.render.motion_blur_position = "CENTER"
    sc.eevee.motion_blur_max = 64

    wo = bpy.data.worlds.new("MasterWorld")
    wo.use_nodes = True
    wnt = wo.node_tree
    wnt.nodes.clear()
    wout = wnt.nodes.new("ShaderNodeOutputWorld")
    tc = wnt.nodes.new("ShaderNodeTexCoord")
    sep = wnt.nodes.new("ShaderNodeSeparateXYZ")
    mr = wnt.nodes.new("ShaderNodeMapRange")
    mr.inputs["From Min"].default_value = -0.1
    mr.inputs["From Max"].default_value = 0.5
    wr = wnt.nodes.new("ShaderNodeOKLabColorRamp")
    wr.color_ramp.elements[0].color = (0.02, 0.03, 0.07, 1.0)
    wr.color_ramp.elements[1].color = (0.09, 0.14, 0.28, 1.0)
    bg = wnt.nodes.new("ShaderNodeBackground")
    bg.inputs["Strength"].default_value = 0.9
    wnt.links.new(tc.outputs["Generated"], sep.inputs["Vector"])
    wnt.links.new(sep.outputs["Z"], mr.inputs["Value"])
    wnt.links.new(mr.outputs["Result"], wr.inputs["Fac"])
    wnt.links.new(wr.outputs["Color"], bg.inputs["Color"])
    wnt.links.new(bg.outputs["Background"], wout.inputs["Surface"])

    wvol = wnt.nodes.new("ShaderNodeVolumePrincipled")
    wvol.inputs["Color"].default_value = (0.75, 0.88, 1.0, 1.0)
    wvol.inputs["Density"].default_value = 0.018
    wvol.inputs["Anisotropy"].default_value = 0.55
    wnt.links.new(wvol.outputs["Volume"], wout.inputs["Volume"])
    sc.world = wo

    cam = add_camera(sc, "MasterCam", (0.0, -7.8, 1.75), (0.0, 0.2, 1.1), lens=38.0)
    cam.data.dof.use_dof = True
    cam.data.dof.focus_distance = 7.8
    cam.data.dof.aperture_fstop = 2.8
    cam.data.dof.aperture_blades = 6

    add_sun(sc, "KeySun", (-0.55, 0.45, -0.7), energy=2.8, color=(1.0, 0.94, 0.86))
    rim_spot = add_spot(
        sc, "CyanRimSpot", (3.6, 2.2, 2.8), (-1.4, 0.0, 1.15), energy=1400.0, spot_size_deg=60.0, color=(0.15, 0.88, 1.0)
    )
    rim_grp = rim_spot.data.light_groups.groups.add()
    rim_grp.name = "HeroRim"
    rim_spot.data.light_groups.use_default = False
    add_spot(sc, "ShaftSpot", (-4.8, 1.6, 4.4), (0.5, 0.5, 0.6), energy=2200.0, spot_size_deg=50.0, radius=0.06, color=(1.0, 0.78, 0.48))

    bpy.ops.mesh.primitive_plane_add(size=24.0, location=(0.0, 0.0, 0.0))
    floor = bpy.context.active_object
    floor.name = "MasterFloor"
    fmat, fnt, fout = new_material("MasterFloorMat")
    fbsdf = fnt.nodes.new("ShaderNodeBsdfPrincipled")
    fbsdf.inputs["Base Color"].default_value = (0.12, 0.13, 0.16, 1.0)
    fbsdf.inputs["Roughness"].default_value = 0.08
    fnt.links.new(fbsdf.outputs["BSDF"], fout.inputs["Surface"])
    floor.data.materials.append(fmat)

    bpy.ops.object.lightprobe_add(type="PLANE", location=(0.0, 0.0, 0.02))
    planar = bpy.context.active_object
    planar.scale = (6.0, 6.0, 1.0)
    planar.data.influence_distance = 0.8

    bpy.ops.object.lightprobe_add(type="SPHERE", location=(0.0, 0.0, 1.5))
    sph_probe = bpy.context.active_object
    sph_probe.data.influence_distance = 12.0

    bpy.ops.object.lightprobe_add(type="VOLUME", location=(0.0, 0.0, 1.5))
    vol_probe = bpy.context.active_object
    vol_probe.scale = (5.0, 5.0, 2.5)
    vol_probe.data.resolution_x = 3
    vol_probe.data.resolution_y = 3
    vol_probe.data.resolution_z = 2

    npr_mat, nnt, nout = new_material("MasterNPR")
    sh = nnt.nodes.new("ShaderNodeShaderInfo")
    sh.use_own_light_groups = False
    curv = nnt.nodes.new("ShaderNodeCurvature")
    curv.inputs["Samples"].default_value = 8.0
    ramp = nnt.nodes.new("ShaderNodeOKLabColorRamp")
    ramp.color_ramp.interpolation = "CONSTANT"
    ramp.color_ramp.elements[0].position = 0.0
    ramp.color_ramp.elements[0].color = (0.24, 0.10, 0.28, 1.0)
    ramp.color_ramp.elements[1].position = 0.45
    ramp.color_ramp.elements[1].color = (0.96, 0.42, 0.24, 1.0)
    e2 = ramp.color_ramp.elements.new(0.82)
    e2.color = (1.0, 0.88, 0.68, 1.0)

    sh_rim = nnt.nodes.new("ShaderNodeShaderInfo")
    sh_rim.use_own_light_groups = True
    sh_rim.light_group_bits = (0, 0, 0, 2)
    li = nnt.nodes.new("ShaderNodeLightInfo")
    li.light_object = rim_spot
    rim_bw = nnt.nodes.new("ShaderNodeRGBToBW")
    rim_step = nnt.nodes.new("ShaderNodeMapRange")
    rim_step.inputs["From Min"].default_value = 0.02
    rim_step.inputs["From Max"].default_value = 0.08
    mix_rim = nnt.nodes.new("ShaderNodeMix")
    mix_rim.data_type = "RGBA"
    rfac, ra, rb, rres = rgba_mix_sockets(mix_rim)
    rim_em_mix = nnt.nodes.new("ShaderNodeEmission")
    rim_em_mix.inputs["Strength"].default_value = 1.6

    nnt.links.new(sh.outputs["Half-lambert factor"], ramp.inputs["Fac"])
    nnt.links.new(ramp.outputs["Color"], ra)
    nnt.links.new(li.outputs["Light Color"], rb)
    nnt.links.new(sh_rim.outputs["Diffuse Shading"], rim_bw.inputs["Color"])
    nnt.links.new(rim_bw.outputs["Val"], rim_step.inputs["Value"])
    nnt.links.new(rim_step.outputs["Result"], rfac)
    nnt.links.new(rres, rim_em_mix.inputs["Color"])
    nnt.links.new(rim_em_mix.outputs["Emission"], nout.inputs["Surface"])

    bpy.ops.mesh.primitive_monkey_add(size=1.45, location=(-1.65, 0.0, 0.95))
    suz = bpy.context.active_object
    bpy.ops.object.shade_smooth()
    suz.rotation_euler = (0.0, 0.0, math.radians(-20.0))
    suz.data.materials.append(npr_mat)

    cdata = bpy.data.hair_curves.new("HeroCrest")
    cdata.add_curves([12] * 36)
    rad_attr = cdata.attributes.new("radius", "FLOAT", "POINT")
    pos_flat, rad_flat = [], []
    for ci in range(36):
        bx = -2.1 + (ci % 9) * 0.11
        by = -0.2 + (ci // 9) * 0.12
        for pi in range(12):
            t = pi / 11.0
            pos_flat.extend([bx + 0.18 * (t ** 1.5), by + 0.1 * math.sin(t * math.pi), 1.45 + 0.85 * t])
            rad_flat.append(0.03 * (1.0 - 0.6 * t))
    cdata.attributes["position"].data.foreach_set("vector", pos_flat)
    rad_attr.data.foreach_set("value", rad_flat)
    hmat, hnt, hout = new_material("MasterHairMat")
    hbsdf = hnt.nodes.new("ShaderNodeBsdfHairPrincipled")
    hbsdf.parametrization = "COLOR"
    hbsdf.inputs["Color"].default_value = (0.15, 0.78, 0.95, 1.0)
    hbsdf.inputs["Roughness"].default_value = 0.3
    hnt.links.new(hbsdf.outputs["BSDF"], hout.inputs["Surface"])
    cdata.materials.append(hmat)
    cob = bpy.data.objects.new("HeroCrestObj", cdata)
    sc.collection.objects.link(cob)

    gmat, gnt, gout = new_material("MasterGlass")
    gmat.blend_method = "BLEND"
    gmat.use_screen_refraction = True
    gmat.refraction_depth = 0.8
    gbsdf = gnt.nodes.new("ShaderNodeBsdfPrincipled")
    gbsdf.inputs["Base Color"].default_value = (0.75, 0.94, 1.0, 1.0)
    gbsdf.inputs["Roughness"].default_value = 0.05
    if "Transmission Weight" in gbsdf.inputs:
        gbsdf.inputs["Transmission Weight"].default_value = 0.92
    gnt.links.new(gbsdf.outputs["BSDF"], gout.inputs["Surface"])
    bpy.ops.mesh.primitive_uv_sphere_add(segments=48, ring_count=24, radius=0.72, location=(0.0, -0.3, 0.85))
    glass_sph = bpy.context.active_object
    bpy.ops.object.shade_smooth()
    glass_sph.data.materials.append(gmat)

    smat, snt, sout = new_material("MasterSSS")
    smat.use_sss_translucency = True
    sbsdf = snt.nodes.new("ShaderNodeBsdfPrincipled")
    sbsdf.inputs["Base Color"].default_value = (0.35, 0.92, 0.65, 1.0)
    sbsdf.inputs["Roughness"].default_value = 0.35
    if "Subsurface Weight" in sbsdf.inputs:
        sbsdf.inputs["Subsurface Weight"].default_value = 1.0
    if "Subsurface Scale" in sbsdf.inputs:
        sbsdf.inputs["Subsurface Scale"].default_value = 0.35
    if "Subsurface Radius" in sbsdf.inputs:
        sbsdf.inputs["Subsurface Radius"].default_value = (0.35, 1.4, 0.8)
    snt.links.new(sbsdf.outputs["BSDF"], sout.inputs["Surface"])
    bpy.ops.mesh.primitive_uv_sphere_add(segments=48, ring_count=24, radius=0.72, location=(1.75, 0.0, 0.85))
    sss_sph = bpy.context.active_object
    bpy.ops.object.shade_smooth()
    sss_sph.data.materials.append(smat)

    bpy.ops.mesh.primitive_cube_add(size=1.8, location=(2.2, 2.2, 1.5))
    vcube = bpy.context.active_object
    vmat, vnt, vout = new_material("MasterObjVol")
    vnt.nodes.clear()
    vout = vnt.nodes.new("ShaderNodeOutputMaterial")
    vp = vnt.nodes.new("ShaderNodeVolumePrincipled")
    vp.inputs["Color"].default_value = (0.5, 0.9, 1.0, 1.0)
    vp.inputs["Density"].default_value = 0.55
    vp.inputs["Emission Color"].default_value = (0.15, 0.80, 1.0, 1.0)
    vp.inputs["Emission Strength"].default_value = 1.4
    vnt.links.new(vp.outputs["Volume"], vout.inputs["Volume"])
    vcube.data.materials.append(vmat)

    bpy.ops.mesh.primitive_uv_sphere_add(segments=24, ring_count=12, radius=0.22, location=(0.0, 0.4, 2.55))
    comet = bpy.context.active_object
    cmat, cnt, cout = new_material("MasterComet")
    cem = cnt.nodes.new("ShaderNodeEmission")
    cem.inputs["Color"].default_value = (1.0, 0.62, 0.16, 1.0)
    cem.inputs["Strength"].default_value = 8.0
    cnt.links.new(cem.outputs["Emission"], cout.inputs["Surface"])
    comet.data.materials.append(cmat)
    for fnum, xpos in [(1, -1.6), (3, 1.6)]:
        sc.frame_set(fnum)
        comet.location.x = xpos
        comet.keyframe_insert(data_path="location")
    for fc in comet.animation_data.action.layers[0].strips[0].channelbags[0].fcurves:
        for kp in fc.keyframe_points:
            kp.interpolation = "LINEAR"
    sc.frame_set(2, subframe=0.0)

    if SKIP_GPU_RENDER:
        print("PASS: Master Showcase scene graph constructed cleanly (GPU render skipped)")
        return

    exr_path = TMP_DIR / "master_showcase.exr"
    sc.render.image_settings.file_format = "OPEN_EXR"
    sc.render.image_settings.color_depth = "32"
    sc.render.filepath = str(exr_path)
    t0 = time.perf_counter()
    bpy.ops.render.render(write_still=True)
    dt = time.perf_counter() - t0
    arr = load_image_rgba(exr_path)[..., :3]
    assert np.all(np.isfinite(arr)), "Non-finite pixels in Master Showcase"
    assert arr.max() > 1.5 and arr.mean() > 0.03, (
        f"Unexpected dynamic range in Master Showcase: min={arr.min()}, mean={arr.mean()}, max={arr.max()}"
    )

    png_path = OUT_DIR / "goo_ci_master_showcase.png"
    sc.render.image_settings.file_format = "PNG"
    sc.render.image_settings.color_depth = "8"
    sc.render.filepath = str(png_path)
    bpy.ops.render.render(write_still=True)
    print(f"PASS: Master Showcase rendered in {dt:.2f}s (max={arr.max():.2f}, mean={arr.mean():.3f}) -> {png_path}")


# ---------------------------------------------------------------------------
# Test 3: Stock BLENDER_EEVEE Coexistence Sanity
# ---------------------------------------------------------------------------

def test_3_stock_eevee_sanity():
    print("\n=== TEST 3: Stock BLENDER_EEVEE Sanity ===")
    sc = reset_scene("BLENDER_EEVEE")
    if SKIP_GPU_RENDER:
        print("PASS: Stock BLENDER_EEVEE registered cleanly (GPU render skipped)")
        return

    sc.render.resolution_x = 160
    sc.render.resolution_y = 120
    sc.eevee.taa_render_samples = 2
    add_camera(sc, "EeveeSanityCam", (0.0, -4.0, 1.2), (0.0, 0.0, 0.5))
    add_sun(sc, "EeveeSanitySun", (-0.4, 0.6, -0.8), energy=3.0)
    bpy.ops.mesh.primitive_cube_add(size=1.2, location=(0.0, 0.0, 0.5))
    cube = bpy.context.active_object
    mat, nt, out = new_material("EeveeSanityMat")
    bsdf = nt.nodes.new("ShaderNodeBsdfPrincipled")
    bsdf.inputs["Base Color"].default_value = (0.35, 0.72, 0.92, 1.0)
    nt.links.new(bsdf.outputs["BSDF"], out.inputs["Surface"])
    cube.data.materials.append(mat)

    exr_path = TMP_DIR / "eevee_sanity.exr"
    sc.render.image_settings.file_format = "OPEN_EXR"
    sc.render.image_settings.color_depth = "32"
    sc.render.filepath = str(exr_path)
    bpy.ops.render.render(write_still=True)
    arr = load_image_rgba(exr_path)[..., :3]
    assert np.all(np.isfinite(arr)), "Non-finite pixels in Stock BLENDER_EEVEE sanity render"
    assert arr.max() > 0.1, f"Stock BLENDER_EEVEE rendered unexpectedly dark: max={arr.max()}"
    print(f"PASS: Stock BLENDER_EEVEE sanity rendered (max={arr.max():.2f}, mean={arr.mean():.3f})")


if __name__ == "__main__":
    try:
        test_1_goo44_and_stock4x_compat()
        test_2_master_showcase()
        test_3_stock_eevee_sanity()
        print("\nALL_GOO_CI_TESTS_PASSED")
    finally:
        shutil.rmtree(TMP_DIR, ignore_errors=True)
