"""CAN bus sniffer case v01 (PREVIEW stage): XIAO ESP32-C6 + WCMCU-230 (SN65HVD230), two-piece base + lid.
Outer 50 x 35 x 20 mm (measured, exact). x = length (USB-C end at x=0, wire end at x=50), y = width, z = up.
Lid retention: 4 x M2 socket caps through the lid into M2 heat-set inserts in corner bosses of the base.
Tags: OWNER = measured dimension, SEEED = Seeed docs (not measured), EG = educated guess."""
import cadquery as cq

# ---- outer (OWNER, measured) ----
L, W, H = 50.0, 35.0, 20.0
WALL = 2.0                 # >= 1.6 (0.6 nozzle, 2 walls + margin)
FLOOR = 2.0
LID_T = 3.0                # lid plate, room for a 2.1 mm counterbore
Z_SPLIT = H - LID_T        # 17.0: base top / lid underside
R_OUT = 3.0                # outer vertical corner radius (EG, looks/handling)
BOT_CH = 0.4               # elephant-foot chamfer on the bed edges

# ---- XIAO ESP32-C6 (SEEED: 21.0 x 17.8; thickness/USB from Seeed KiCad footprint, EG) ----
X_L, X_W, X_T = 21.0, 17.8, 1.6
X_TOP = 3.2                # EG: tallest part on the top side (shield / USB-C shell)
USB_W, USB_H, USB_OVH = 9.0, 3.3, 1.5   # EG: USB-C receptacle size and overhang past the PCB edge
USB_GAP = 0.3              # receptacle face sits 0.3 inside the wall's inner face
X_STANDOFF = 2.0           # PCB underside 2.0 above the floor (plinth), lifts the USB centre
X_CLR = 0.25               # pocket clearance per side around the PCB
X_Y0 = W / 2 - X_W / 2     # centred in width
X_X0 = WALL + USB_GAP + USB_OVH          # PCB USB edge, x = 3.8
X_X1 = X_X0 + X_L                        # 24.8
X_Z0 = FLOOR + X_STANDOFF                # 4.0 PCB underside
X_Z1 = X_Z0 + X_T                        # 5.6 PCB top
USB_CZ = X_Z1 + USB_H / 2                # 7.25 receptacle centre height
CAST_INSET = 1.2           # plinth inset from the long edges: clears castellated-pad solder and wires
STOP_T = 1.6               # corner stop wall thickness
STOP_LEG_LONG = 1.8        # stop legs along the long sides (pin rows start ~2.1 from each end)
STOP_LEG_SHORT = 3.0       # along the short sides (USB receptacle is the centre 9 mm)
STOP_TOP = X_Z1 - 0.3      # stops end 0.3 below the PCB top: never touch top-side parts or pads

# ---- USB-C cable opening, USB end wall (EG overmold 12 x 7 incl. clearance) ----
USB_OPEN_W, USB_OPEN_H = 12.0, 7.0

# ---- wire slot, far end wall (EG: 3 x 1.8 mm OD 22 AWG hookup side by side + 0.4 each way) ----
WIRE_OD, N_WIRES = 1.8, 3
SLOT_W = N_WIRES * WIRE_OD + 0.8         # 6.2
SLOT_H = WIRE_OD + 0.6                   # 2.4
SLOT_CZ = 8.0              # EG: mid-height-ish, above the loose CAN board

# ---- lid screws: M2 heat-set inserts (EG insert 3.2 hole x 4.0 deep, check your insert's spec) ----
INS_D, INS_DEPTH = 3.2, 4.0
PILOT_D, PILOT_DEPTH = 2.0, 7.0          # screw run-out below the insert
BOSS_D = 6.6               # wall around the insert hole 1.7 (>= 1.6)
BOSS_C = WALL + 2.6        # boss centre 4.6 from each outer face (merges into both walls)
CLR_D = 2.4                # M2 clearance in the lid
CB_D, CB_DEPTH = 4.2, 2.1  # M2 socket head 3.8 dia x 2.0 (ISO 4762) + clearance; leaves 0.9 under the head
SCREW = "M2 x 6 socket cap (0.9 lid + 4.0 insert + 1.1 in the pilot)"
BOSSES = [(BOSS_C, BOSS_C), (L - BOSS_C, BOSS_C), (BOSS_C, W - BOSS_C), (L - BOSS_C, W - BOSS_C)]

# ---- WCMCU-230 (EG from vendor listings 17 x 15 x 3 mm; loose, no bracket) ----
CAN_L, CAN_W, CAN_T = 17.0, 15.0, 1.6
CAN_TALL = 10.0            # EG: standing 120 ohm resistor / header stubs above the board


def bx(x0, x1, y0, y1, z0, z1):
    return cq.Workplane("XY").box(x1 - x0, y1 - y0, z1 - z0, centered=False).translate((x0, y0, z0))


def stadium_x(xw, cy, cz, w, h, depth):
    """slot through an end wall (normal along X), long axis along Y."""
    r = h / 2
    return (cq.Workplane("YZ").workplane(offset=xw).center(cy, cz).slot2D(w, h, 0).extrude(depth))


def outer(z0, z1):
    s = bx(0, L, 0, W, z0, z1).edges("|Z").fillet(R_OUT)
    return s


def base():
    b = outer(0, Z_SPLIT).faces("<Z").edges().chamfer(BOT_CH)
    cav = bx(WALL, L - WALL, WALL, W - WALL, FLOOR, Z_SPLIT + 1).edges("|Z").fillet(R_OUT - WALL)
    b = b.cut(cav)
    # corner bosses (full height, merged into the walls) with heat-set insert holes
    for (x, y) in BOSSES:
        b = b.union(cq.Workplane("XY").workplane(offset=FLOOR - 0.01).center(x, y).circle(BOSS_D / 2).extrude(Z_SPLIT - FLOOR + 0.01))
        b = b.cut(cq.Workplane("XY").workplane(offset=Z_SPLIT - INS_DEPTH).center(x, y).circle(INS_D / 2).extrude(INS_DEPTH + 1))
        b = b.cut(cq.Workplane("XY").workplane(offset=Z_SPLIT - INS_DEPTH - PILOT_DEPTH + 0.01).center(x, y).circle(PILOT_D / 2).extrude(PILOT_DEPTH))
    # XIAO cradle: plinth under the PCB (inset from the castellated long edges) + 4 corner stops
    b = b.union(bx(X_X0, X_X1, X_Y0 + CAST_INSET, X_Y0 + X_W - CAST_INSET, FLOOR - 0.01, X_Z0))
    xa, xb = X_X0 - X_CLR, X_X1 + X_CLR
    ya, yb = X_Y0 - X_CLR, X_Y0 + X_W + X_CLR
    for cx, sx in ((xa, -1), (xb, 1)):
        for cy, sy in ((ya, -1), (yb, 1)):
            # L-shaped stop hugging the PCB corner from outside
            leg_s = bx(min(cx, cx + sx * STOP_T), max(cx, cx + sx * STOP_T),
                       min(cy, cy - sy * STOP_LEG_SHORT), max(cy, cy - sy * STOP_LEG_SHORT) + 0, FLOOR - 0.01, STOP_TOP)
            leg_l = bx(min(cx + sx * STOP_T, cx - sx * STOP_LEG_LONG), max(cx + sx * STOP_T, cx - sx * STOP_LEG_LONG),
                       min(cy, cy + sy * STOP_T), max(cy, cy + sy * STOP_T), FLOOR - 0.01, STOP_TOP)
            b = b.union(leg_s).union(leg_l)
    # ledge under the PCB at the stops (PCB corners rest on z = X_Z0 there as well as on the plinth)
    # USB-C cable opening (through the x=0 wall), stadium 12 x 7, centred on the receptacle
    b = b.cut(stadium_x(-1, W / 2, USB_CZ, USB_OPEN_W, USB_OPEN_H, WALL + 2))
    # wire slot (through the x=L wall)
    b = b.cut(stadium_x(L - WALL - 1, W / 2, SLOT_CZ, SLOT_W, SLOT_H, WALL + 2))
    return b


def lid():
    l = outer(Z_SPLIT, H).faces(">Z").edges().chamfer(BOT_CH)     # outer face prints on the bed
    for (x, y) in BOSSES:
        l = l.cut(cq.Workplane("XY").workplane(offset=Z_SPLIT - 1).center(x, y).circle(CLR_D / 2).extrude(LID_T + 2))
        l = l.cut(cq.Workplane("XY").workplane(offset=H - CB_DEPTH).center(x, y).circle(CB_D / 2).extrude(CB_DEPTH + 1))
    return l


def xiao_keepout():
    pcb = bx(X_X0, X_X1, X_Y0, X_Y0 + X_W, X_Z0, X_Z1)
    top = bx(X_X0 + 0.5, X_X1 - 0.5, X_Y0 + 0.5, X_Y0 + X_W - 0.5, X_Z1, X_Z1 + X_TOP)
    usb = bx(X_X0 - USB_OVH, X_X0 + 7.3, W / 2 - USB_W / 2, W / 2 + USB_W / 2, X_Z1, X_Z1 + USB_H)
    return pcb.union(top).union(usb)


def plug_keepout():
    """EG USB-C cable, fully mated: the metal plug is inside the receptacle; the overmold (EG 11.5 x 6.5) starts at the
    receptacle face (x = 2.3) and runs out through the 12 x 7 opening."""
    face = X_X0 - USB_OVH
    return stadium_x(-14.0, W / 2, USB_CZ, 11.5, 6.5, 14.0 + face)   # rounded ends like a real overmold (EG)


def can_board(x0=28.0, y0=None):
    y0 = (W - CAN_W) / 2 if y0 is None else y0
    pcb = bx(x0, x0 + CAN_L, y0, y0 + CAN_W, FLOOR, FLOOR + CAN_T)
    res = bx(x0 + 3, x0 + 5, y0 + 4, y0 + 6, FLOOR + CAN_T, FLOOR + CAN_T + CAN_TALL)
    return pcb.union(res)


if __name__ == "__main__":
    b, l = base(), lid()
    for n, s in (("base", b), ("lid", l)):
        bb = s.val().BoundingBox(); print(n, s.val().isValid(), len(s.solids().vals()), round(bb.xlen, 2), round(bb.ylen, 2), round(bb.zlen, 2))
    print("plug edge", X_X0 - USB_OVH, "usb cz", USB_CZ, "pcb", X_X0, X_X1, X_Y0, X_Z0, X_Z1)
