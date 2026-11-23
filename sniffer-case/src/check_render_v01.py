"""v01 preview: interference/clearance checks + blue/black/white renders (no 3MF, no upload)."""
import os, sys, json, numpy as np, trimesh, cadquery as cq
import matplotlib; matplotlib.use("Agg"); import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch
sys.path.insert(0, "/workspace/maker/tools"); from zbuf_render import render_multi
import can_case_v01 as C
HERE = os.path.dirname(os.path.abspath(__file__)); EXP = os.path.join(HERE, "..", "exports"); WK = os.path.join(EXP, "work_v01")
os.makedirs(WK, exist_ok=True)
parts = {"base": C.base(), "lid": C.lid(), "xiao": C.xiao_keepout(), "plug": C.plug_keepout(), "can": C.can_board()}
M = {}
for k, s in parts.items():
    p = os.path.join(WK, f"can_case_v01_{k}.stl"); cq.exporters.export(s, p, tolerance=0.02, angularTolerance=0.1); M[k] = trimesh.load(p)
def iv(a, b):
    try: return round(parts[a].intersect(parts[b]).val().Volume(), 4)
    except Exception: return 0.0
def gap(a, b):
    from OCP.BRepExtrema import BRepExtrema_DistShapeShape
    d = BRepExtrema_DistShapeShape(parts[a].val().wrapped, parts[b].val().wrapped); d.Perform(); return round(d.Value(), 3)
chk = {
    "valid_single_solid": {k: (parts[k].val().isValid(), len(parts[k].solids().vals())) for k in ("base", "lid")},
    "watertight": {k: bool(M[k].is_watertight) for k in ("base", "lid")},
    "outer_assembled_mm": [C.L, C.W, C.H], "base_h": C.Z_SPLIT, "lid_t": C.LID_T,
    "inner_cavity_mm": [C.L - 2 * C.WALL, C.W - 2 * C.WALL, C.Z_SPLIT - C.FLOOR],
    "interference_mm3": {"xiao_vs_base": iv("xiao", "base"), "xiao_vs_lid": iv("xiao", "lid"), "plug_vs_base": iv("plug", "base"),
                         "can_vs_base": iv("can", "base"), "can_vs_xiao": iv("can", "xiao"), "lid_vs_base": iv("lid", "base")},
    "min_gap_mm": {"xiao_to_base": gap("xiao", "base"), "plug_to_base": gap("plug", "base"), "xiao_top_to_lid": round(C.Z_SPLIT - (C.X_Z1 + C.X_TOP), 2)},
    "usb_opening": {"w": C.USB_OPEN_W, "h": C.USB_OPEN_H, "centre_y": C.W / 2, "centre_z": C.USB_CZ, "z_range": [C.USB_CZ - C.USB_OPEN_H / 2, C.USB_CZ + C.USB_OPEN_H / 2]},
    "wire_slot": {"w": C.SLOT_W, "h": C.SLOT_H, "centre_z": C.SLOT_CZ},
    "xiao_cradle": {"pocket_L_W": [C.X_L + 2 * C.X_CLR, C.X_W + 2 * C.X_CLR], "pcb_underside_z": C.X_Z0, "stops_top_z": C.STOP_TOP,
                    "plinth_W": C.X_W - 2 * C.CAST_INSET, "pcb_usb_edge_x": C.X_X0},
    "free_for_can_board_x": [round(C.X_X1 + C.X_CLR + C.STOP_T, 2), C.L - C.WALL], "can_board_EG": [C.CAN_L, C.CAN_W, C.CAN_TALL],
    "lid": {"method": "4 x M2 socket cap + M2 heat-set inserts", "screw": C.SCREW, "insert_hole": [C.INS_D, C.INS_DEPTH], "boss_d": C.BOSS_D},
}
json.dump(chk, open(os.path.join(EXP, "can_case_v01_preview_checks.json"), "w"), indent=1); print(json.dumps(chk, indent=1))

BLUE, WHITE = (0, 0, 1), (1, 1, 1)
def save(img, name, title):
    h, w = img.shape[:2]; fig = plt.figure(figsize=(w / 100, h / 100 + 0.9), dpi=100, facecolor="white")
    ax = fig.add_axes([0, 0, 1, h / (h + 90)]); ax.imshow(img); ax.axis("off")
    fig.text(0.5, 1 - 0.45 * 90 / (h + 90), title, ha="center", va="center", fontsize=13)
    p = os.path.join(EXP, name); fig.savefig(p, facecolor="white"); plt.close(fig); return p
out = []
iso = (-0.9, -1.2, 1.3)
out.append(save(render_multi([(M["base"], BLUE, True), (M["xiao"], WHITE, False), (M["can"], WHITE, False)], iso, size=(1500, 1100)),
                "can_case_v01_base_iso.png", "Base (blue) with XIAO ESP32-C6 keep-out and loose WCMCU-230 (white, EG 17 x 15, 10 mm tall resistor) - USB-C end at left"))
iso2 = (1.0, 1.1, 1.3)
out.append(save(render_multi([(M["base"], BLUE, True), (M["xiao"], WHITE, False)], iso2, size=(1500, 1100)),
                "can_case_v01_base_iso_wire_end.png", "Base from the wire end: 6.2 x 2.4 wire slot, corner bosses for M2 heat-set inserts, XIAO cradle"))
lid_flip = M["lid"].copy()
out.append(save(render_multi([(M["lid"], BLUE, True)], (-0.9, -1.2, 1.3), size=(1300, 900)),
                "can_case_v01_lid_top.png", "Lid, outside face: 4 x M2 counterbored holes (4.2 x 2.1 deep), 3.0 thick"))
out.append(save(render_multi([(M["lid"], BLUE, True)], (-0.9, -1.2, -1.3), up=(0, 0, -1), size=(1300, 900), light=(-0.4, -0.6, -1.0)),
                "can_case_v01_lid_underside.png", "Lid, underside (plain plate, nothing hanging down)"))
asm = [(M["base"], BLUE, True), (M["lid"], BLUE, True), (M["plug"], WHITE, False)]
out.append(save(render_multi(asm, (-1.4, -0.9, 0.8), size=(1500, 1100)), "can_case_v01_assembled_usb_end.png",
                "Assembled 50 x 35 x 20, USB-C end: EG 11.5 x 6.5 cable overmold keep-out (white), mated to the receptacle face 2.3 mm inside"))

# ---- 2D end views with dims ----
def end_view(ax, title, opening, ow, oh, ocz, extra):
    ax.add_patch(FancyBboxPatch((0, 0), C.W, C.H, boxstyle="round,pad=0,rounding_size=0.01", fc=BLUE, ec="black", lw=1.5))
    ax.plot([0, C.W], [C.Z_SPLIT, C.Z_SPLIT], color="black", lw=1.0)
    r = oh / 2; cy = C.W / 2
    t = np.linspace(-np.pi / 2, np.pi / 2, 40)
    xs = np.r_[cy + (ow / 2 - r) + r * np.cos(t), cy - (ow / 2 - r) - r * np.cos(t)]
    ys = np.r_[ocz + r * np.sin(t), ocz - r * np.sin(t)]
    ax.fill(xs, ys, color="white", ec="black", lw=1.2)
    extra(ax)
    ax.annotate("", xy=(cy - ow / 2, ocz + r + 1.6), xytext=(cy + ow / 2, ocz + r + 1.6), arrowprops=dict(arrowstyle="<->", color="black"))
    ax.text(cy, ocz + r + 2.1, f"{ow:.1f}", ha="center", fontsize=10, color="white" if True else "black",
            bbox=dict(fc="black", ec="none", pad=1))
    ax.annotate("", xy=(cy + ow / 2 + 1.5, ocz - r), xytext=(cy + ow / 2 + 1.5, ocz + r), arrowprops=dict(arrowstyle="<->", color="black"))
    ax.text(cy + ow / 2 + 2.0, ocz, f"{oh:.1f}\ncentre z {ocz:.2f}", va="center", fontsize=9, color="white", bbox=dict(fc="black", ec="none", pad=1))
    ax.text(C.W + 1, C.Z_SPLIT + 1.5, "lid 3.0", fontsize=9, va="center"); ax.text(C.W + 1, 8.5, "base 17.0", fontsize=9, va="center")
    ax.text(C.W / 2, -2.2, "35.0 wide x 20.0 tall (outer)", ha="center", fontsize=10)
    ax.set_xlim(-2, C.W + 9); ax.set_ylim(-4, C.H + 3); ax.set_aspect("equal"); ax.axis("off"); ax.set_title(title, fontsize=12)
fig, axs = plt.subplots(1, 2, figsize=(16, 6.4), dpi=110, facecolor="white")
def usb_extra(ax):
    # XIAO PCB + receptacle behind the wall (dashed)
    ax.add_patch(plt.Rectangle((C.X_Y0, C.X_Z0), C.X_W, C.X_T, fill=False, ls="--", ec="black", lw=1.0))
    ax.add_patch(plt.Rectangle((C.W / 2 - C.USB_W / 2, C.X_Z1), C.USB_W, C.USB_H, fill=False, ls="-", ec="black", lw=1.2))
    ax.text(C.X_Y0 - 0.5, C.X_Z0 - 1.2, "XIAO PCB (behind, dashed)", fontsize=8, color="white", bbox=dict(fc="black", ec="none", pad=1))
end_view(axs[0], "USB-C end (x = 0), looking in: cable opening, EG 12 x 7 overmold", "usb", C.USB_OPEN_W, C.USB_OPEN_H, C.USB_CZ, usb_extra)
def wire_extra(ax):
    for i in range(C.N_WIRES):
        ax.add_patch(plt.Circle((C.W / 2 + (i - 1) * C.WIRE_OD, C.SLOT_CZ), C.WIRE_OD / 2, fill=False, ec="black", lw=1.0))
end_view(axs[1], "Wire end (x = 50), looking in: 3 x EG 1.8 OD hookup wires side by side", "wire", C.SLOT_W, C.SLOT_H, C.SLOT_CZ, wire_extra)
fig.tight_layout(); p = os.path.join(EXP, "can_case_v01_end_views.png"); fig.savefig(p, facecolor="white"); plt.close(fig); out.append(p)

# ---- sections through the XIAO cradle ----
def sect(ax, plane_o, plane_n, to2d, xl, yl, title):
    for k, fc in (("base", BLUE), ("lid", BLUE), ("xiao", WHITE), ("plug", WHITE)):
        s = M[k].section(plane_origin=plane_o, plane_normal=plane_n)
        if s is None: continue
        p2, _ = s.to_2D(to_2D=to2d) if False else (None, None)
        for poly in s.discrete:
            uv = poly @ np.array(to2d).T
            ax.fill(uv[:, 0], uv[:, 1], color=fc, ec="black", lw=1.0, ls="--" if k in ("xiao", "plug") else "-")
    ax.set_xlim(*xl); ax.set_ylim(*yl); ax.set_aspect("equal"); ax.set_title(title, fontsize=11)
    ax.grid(True, color=(0.85, 0.85, 1.0), lw=0.4); ax.tick_params(labelsize=8)
fig, axs = plt.subplots(2, 1, figsize=(14, 12), dpi=110, facecolor="white")
sect(axs[0], (0, C.W / 2, 0), (0, 1, 0), [(1, 0, 0), (0, 0, 1)], (-15, 52), (-1, 21),
     "Section y = 17.5 (centre, through USB-C): XIAO on the 2.0 plinth, receptacle face 0.3 inside the wall, cable + overmold (white, dashed = keep-outs)")
sect(axs[1], (14.0, 0, 0), (1, 0, 0), [(0, 1, 0), (0, 0, 1)], (-1, 36), (-1, 21),
     "Section x = 14 (across the cradle): plinth 15.4 wide (inset 1.2 from the pin rows), PCB 17.8, corner bosses not in this cut")
for ax in axs: ax.set_xlabel("mm")
axs[0].annotate("PCB underside z 4.0\nUSB centre z 7.25", xy=(10, 4.0), xytext=(30, 12), fontsize=9, arrowprops=dict(arrowstyle="->"))
fig.tight_layout(); p = os.path.join(EXP, "can_case_v01_section_cradle.png"); fig.savefig(p, facecolor="white"); plt.close(fig); out.append(p)
# extra: section at x = 4.5 through the front stops
fig, ax = plt.subplots(figsize=(9, 6), dpi=110, facecolor="white")
sect(ax, (C.X_X0 + 0.6, 0, 0), (1, 0, 0), [(0, 1, 0), (0, 0, 1)], (-1, 36), (-1, 21), "Section x = 4.4: USB-end corner stops (to z 5.3, below the PCB top) + bosses")
fig.tight_layout(); p = os.path.join(EXP, "can_case_v01_section_stops.png"); fig.savefig(p, facecolor="white"); plt.close(fig); out.append(p)
print("\n".join(out))
