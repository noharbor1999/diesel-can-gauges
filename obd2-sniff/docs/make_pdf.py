#!/usr/bin/env python3
"""Makes the printable OBD2 pin-finder guide (multimeter only) and the separate message-ID log.
Black ink only. Gray only for table header fill. Large type. ASD-STE100 style sentences."""
from reportlab.lib.pagesizes import letter, landscape
from reportlab.lib.units import inch
from reportlab.lib import colors
from reportlab.lib.styles import ParagraphStyle
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import (BaseDocTemplate, PageTemplate, Frame, Paragraph, Spacer, Table,
                                TableStyle, PageBreak, NextPageTemplate, Flowable, KeepTogether, CondPageBreak)
import os

D = "/usr/share/fonts/truetype/dejavu/"
pdfmetrics.registerFont(TTFont("Sans", D + "DejaVuSans.ttf"))
pdfmetrics.registerFont(TTFont("SansB", D + "DejaVuSans-Bold.ttf"))
from reportlab.pdfbase.pdfmetrics import registerFontFamily
registerFontFamily("Sans", normal="Sans", bold="SansB", italic="Sans", boldItalic="SansB")

BLACK = colors.black
GRAY = colors.Color(0.85, 0.85, 0.85)
OHM = "\u03a9"

H1 = ParagraphStyle("H1", fontName="SansB", fontSize=26, leading=32, spaceAfter=10, textColor=BLACK)
H2 = ParagraphStyle("H2", fontName="SansB", fontSize=20, leading=25, spaceBefore=4, spaceAfter=10, textColor=BLACK)
BODY = ParagraphStyle("B", fontName="Sans", fontSize=14, leading=20, spaceAfter=8, textColor=BLACK)
BIG = ParagraphStyle("BIG", parent=BODY, fontSize=16, leading=22)
STEP = ParagraphStyle("S", parent=BODY, spaceAfter=0)
CELL = ParagraphStyle("C", fontName="Sans", fontSize=12, leading=15, textColor=BLACK)
CELLB = ParagraphStyle("CB", parent=CELL, fontName="SansB")
SMALL = ParagraphStyle("SM", parent=BODY, fontSize=12, leading=16)
WARN = ParagraphStyle("W", parent=BODY, fontName="SansB", fontSize=15, leading=21)

def P(t, s=BODY): return Paragraph(t, s)

class Box(Flowable):
    """Empty checkbox."""
    def __init__(self, size=0.26 * inch): super().__init__(); self.s = size; self.width = self.height = size
    def draw(self):
        self.canv.setLineWidth(1.6); self.canv.setStrokeColor(BLACK); self.canv.rect(0, 0, self.s, self.s)

class Lines(Flowable):
    """Write-in lines with an optional label on the first line."""
    def __init__(self, n=2, label="Result:", gap=0.42 * inch, indent=0.95 * inch):
        super().__init__(); self.n, self.label, self.gap, self.indent = n, label, gap, indent
    def wrap(self, aw, ah): self.aw = aw; return aw, self.n * self.gap + 4
    def draw(self):
        c = self.canv; c.setStrokeColor(BLACK); c.setLineWidth(0.7)
        for i in range(self.n):
            y = (self.n - 1 - i) * self.gap + 2
            x0 = self.indent
            if i == 0 and self.label:
                c.setFont("Sans", 12); c.drawString(self.indent, y + 3, self.label)
                x0 = self.indent + c.stringWidth(self.label, "Sans", 12) + 6
            c.line(x0, y, self.aw, y)

def step(n, text, lines=0, label="Result:"):
    t = Table([[Box(), P(f"<b>{n}.</b>", STEP), P(text, STEP)]],
              colWidths=[0.42 * inch, 0.5 * inch, None])
    t.setStyle(TableStyle([("VALIGN", (0, 0), (-1, -1), "TOP"), ("LEFTPADDING", (0, 0), (-1, -1), 0),
                           ("TOPPADDING", (0, 0), (0, 0), 3), ("BOTTOMPADDING", (0, 0), (-1, -1), 2)]))
    items = [t]
    if lines: items.append(Spacer(1, 4)); items.append(Lines(lines, label))
    items.append(Spacer(1, 12))
    return KeepTogether(items)

def grid(data, widths, header=True, rowh=None, fs=None):
    rows = []
    for r, row in enumerate(data):
        st = CELLB if (header and r == 0) else CELL
        rows.append([P(str(x), st) if not isinstance(x, Flowable) else x for x in row])
    t = Table(rows, colWidths=widths, rowHeights=rowh, repeatRows=1 if header else 0)
    style = [("GRID", (0, 0), (-1, -1), 1.0, BLACK), ("VALIGN", (0, 0), (-1, -1), "MIDDLE"),
             ("LEFTPADDING", (0, 0), (-1, -1), 5), ("RIGHTPADDING", (0, 0), (-1, -1), 5),
             ("TOPPADDING", (0, 0), (-1, -1), 4), ("BOTTOMPADDING", (0, 0), (-1, -1), 4)]
    if header: style += [("BACKGROUND", (0, 0), (-1, 0), GRAY), ("LINEBELOW", (0, 0), (-1, 0), 2, BLACK)]
    t.setStyle(TableStyle(style))
    return t

class Connector(Flowable):
    """OBD2 (J1962) vehicle connector face, as seen looking into the truck's port."""
    def __init__(self, w=6.6 * inch, h=2.6 * inch): super().__init__(); self.width, self.height = w, h
    def draw(self):
        c = self.canv; w, h = self.width, self.height
        c.setStrokeColor(BLACK); c.setLineWidth(3)
        top, bot = h - 0.15 * inch, 0.45 * inch
        inset = 0.55 * inch
        p = c.beginPath()
        p.moveTo(0.1 * inch, top); p.lineTo(w - 0.1 * inch, top)
        p.lineTo(w - inset, bot); p.lineTo(inset, bot); p.close()
        c.drawPath(p, stroke=1, fill=0)
        r = 0.27 * inch
        xs = [0.75 * inch + i * (w - 1.5 * inch) / 7 for i in range(8)]
        for row, y in ((0, top - 0.55 * inch), (1, top - 1.35 * inch)):
            for i, x in enumerate(xs):
                n = i + 1 + 8 * row
                key = n in (4, 5, 16)
                c.setLineWidth(3 if key else 1.5)
                c.circle(x, y, r, stroke=1, fill=0)
                c.setFont("SansB", 17); c.drawCentredString(x, y - 6, str(n))
        c.setFont("Sans", 11)
        c.drawCentredString(w / 2, 0.12 * inch, "Thick circles: 4 and 5 = ground, 16 = battery +. Wide side is at the top.")

def build_guide(path):
    doc = BaseDocTemplate(path, pagesize=letter, leftMargin=0.75 * inch, rightMargin=0.75 * inch,
                          topMargin=0.7 * inch, bottomMargin=0.75 * inch,
                          title="OBD2 Pin Finder - 2017 Ram 3500", author="Build")
    def footer(c, d):
        c.saveState(); c.setFont("Sans", 10); pw, ph = c._pagesize
        c.drawString(0.6 * inch, 0.4 * inch, "OBD2 Pin Finder  |  2017 Ram 3500 6.7 Cummins  |  multimeter only")
        c.drawRightString(pw - 0.6 * inch, 0.4 * inch, f"Page {d.page}")
        c.restoreState()
    pf = Frame(0.75 * inch, 0.75 * inch, letter[0] - 1.5 * inch, letter[1] - 1.45 * inch, id="p")
    L = landscape(letter)
    lf = Frame(0.45 * inch, 0.6 * inch, L[0] - 0.9 * inch, L[1] - 1.0 * inch, id="l")
    doc.addPageTemplates([PageTemplate("portrait", [pf], onPage=footer, pagesize=letter),
                          PageTemplate("landscape", [lf], onPage=footer, pagesize=L)])
    s = []
    # ---------------- Page 1: title, tools, safety
    s += [P("OBD2 Pin Finder", H1),
          P("2017 Ram 3500, 6.7 Cummins. Find what each of the 16 OBD2 pins does. Use only a multimeter.", BIG),
          Spacer(1, 6), P("You need", H2)]
    for t in ["Multimeter with DC volts and ohms", "LINXINO OBD2 breakout box",
              "One clip lead (alligator) for the meter COM lead", "Flashlight", "Pencil and this guide"]:
        s.append(Table([[Box(), P(t, STEP)]], colWidths=[0.45 * inch, None],
                       style=[("VALIGN", (0, 0), (-1, -1), "MIDDLE"), ("LEFTPADDING", (0, 0), (-1, -1), 0),
                              ("BOTTOMPADDING", (0, 0), (-1, -1), 6)]))
    s += [Spacer(1, 10), P("How to use this guide", H2)]
    for i, t in enumerate(["Do the tasks in order. Do one task at a time.",
                           "Put a check in the box when you finish a step.",
                           "Write each reading on the line or in the table.",
                           "Take a photo of the filled pages. Send the photos in chat."], 1):
        s.append(step(i, t))
    s += [PageBreak(), P("Safety. Read all rules first.", H2)]
    rules = [
        "Park on level ground. Set the parking brake. Put the transmission in PARK.",
        "Run the engine only outside. Exhaust gas can kill you.",
        "Pin 16 is battery power. It is always on. Treat it as unfused. Do not connect anything to pin 16 in this test.",
        "Set the meter to DC volts before you measure volts. Never use the A or mA setting. That setting is a short circuit.",
        "Measure ohms only with the key OFF and with 0 V on the pins.",
        "Put one probe tip in one jack. Do not let a probe touch two jacks or two metal parts.",
        "Remove the probes before you start the engine.",
        "Keep wires away from the pedals and the steering column.",
        "This test only measures. It does not send anything to the truck.",
    ]
    for i, r in enumerate(rules, 1):
        s.append(step(i, r))
    s.append(PageBreak())
    # ---------------- Page: words + connector
    s += [P("Words in this guide", H2),
          grid([["Word", "Meaning"],
                ["Key OFF", "Key out (or push-button OFF). Doors closed. Wait 10 minutes so the modules go to sleep."],
                ["Key ON", "Key in RUN. Engine not running. Wait until the WAIT TO START lamp goes off."],
                ["Engine idle", "Engine running in PARK."],
                ["COM lead", "The meter lead in the COM jack. Clip it to pin 4 for all volt tests."],
                ["V lead", f"The meter lead in the V/{OHM} jack. Touch it to the pin you test."],
                ["OL", "The meter shows OL (open). No connection."]],
               [1.5 * inch, 5.5 * inch]),
          Spacer(1, 10),
          P("Lead colors: this guide does not use colors. Use the jack names COM and V on the meter. "
            "Put a label on each lead (COM and V-OHM).", BODY),
          Spacer(1, 6), P("The OBD2 port (look into the truck's port)", H2),
          Connector(), Spacer(1, 6),
          P("Top row: pins 1 to 8, left to right. Bottom row: pins 9 to 16, left to right. "
            "<b>The numbers on the breakout box are correct. Use them.</b>", BODY),
          PageBreak()]
    # ---------------- Standard pin list
    s += [P("What the standard says", H2),
          P("Pins 4, 5, 6, 14, and 16 are standard on all US trucks after 1996. "
            "The truck maker can use pins 1, 3, 8, 9, 11, 12, and 13 for its own purposes.", BODY),
          P("<b>The FCA column is a guess.</b> It is not confirmed on this truck. Your meter readings are the answer.", BODY),
          grid([["Pin", "Standard use (SAE J1962)", "FCA guess (NOT confirmed)"],
                ["1", "Maker use", "Often not used"],
                ["2", "J1850 bus +", "Older trucks only. Probably not used"],
                ["3", "Maker use", "CAN-IHS high (body bus, 125 kbit/s)"],
                ["4", "Chassis ground", "Ground"],
                ["5", "Signal ground", "Ground"],
                ["6", "CAN high", "CAN-C high (engine bus, 500 kbit/s)"],
                ["7", "K-line", "Older trucks: SCI. Maybe not used"],
                ["8", "Maker use", "Unknown"],
                ["9", "Maker use", "Unknown"],
                ["10", "J1850 bus -", "Probably not used"],
                ["11", "Maker use", "CAN-IHS low (body bus)"],
                ["12", "Maker use", "Older trucks: SCI. Unknown"],
                ["13", "Maker use", "Unknown"],
                ["14", "CAN low", "CAN-C low (engine bus)"],
                ["15", "L-line", "Probably not used"],
                ["16", "Battery + (always on)", "About 12.6 V"]],
               [0.7 * inch, 2.9 * inch, 3.4 * inch], rowh=None),
          PageBreak()]
    # ---------------- Task 1
    s += [P("Task 1: Get ready", H2),
          step(1, "Put a label on each meter lead: COM and V-OHM."),
          step(2, "Look into the truck's OBD2 port with the flashlight. Find the positions that have a metal pin. "
                  "Write the pin numbers below.", 2, "Metal pins at:"),
          step(3, "Connect the breakout box to the OBD2 port. Do not connect a scan tool."),
          step(4, "Put the breakout box on the seat. Make sure no jack touches metal."),
          step(5, "Set the meter to DC volts (V with a straight line)."),
          step(6, "Clip the COM lead to the pin 4 jack. Leave it there for all volt tests."),
          PageBreak()]
    # ---------------- Task 2
    s += [P("Task 2: Check ground and battery", H2),
          step(1, "Key OFF. Set the meter to ohms.", 0),
          step(2, f"Measure pin 4 to pin 5. Expect less than 1 {OHM}.", 1, f"{OHM}:"),
          step(3, f"Measure pin 4 to bare metal on the truck body. Expect less than 1 {OHM}.", 1, f"{OHM}:"),
          step(4, "Set the meter to DC volts. Clip the COM lead to pin 4 again."),
          step(5, "Measure pin 16. Expect 12.2 to 12.8 V. This is the battery.", 1, "Volts:"),
          step(6, "If pin 16 shows 0 V, stop. Check the fuse for the OBD2 port and the power outlet.", 1, "Notes:"),
          PageBreak()]
    # ---------------- Task 3-5
    s += [P("Task 3: Key OFF volts", H2),
          step(1, "Key OFF. Close the doors. Wait 10 minutes."),
          step(2, "Measure each pin from 1 to 16 with the V lead. Pin 4 is the reference."),
          step(3, "Write each reading in the <b>Key OFF V</b> column of the <b>All 16 pins</b> table (page 11)."),
          step(4, "Most pins show 0 V now. Pin 16 shows battery volts. Any other pin with volts is important. Write it here.",
               2, "Pins with volts:"),
          Spacer(1, 6),
          P("Task 4: Key ON volts", H2),
          step(1, "Turn the key to RUN. Do not start the engine. Wait until WAIT TO START goes off."),
          step(2, "Measure each pin from 1 to 16 again."),
          step(3, "Write each reading in the <b>Key ON V</b> column."),
          step(4, "Write the pins that changed from Key OFF.", 2, "Pins that changed:"),
          PageBreak(),
          P("Task 5: Engine idle volts", H2),
          step(1, "Remove the probes. Start the engine outside. Let it idle in PARK."),
          step(2, "Measure each pin from 1 to 16 again."),
          step(3, "Write each reading in the <b>Idle V</b> column."),
          step(4, "Pin 16 must now show about 13.5 to 14.5 V (the alternator charges). Write it.", 1, "Pin 16 volts:"),
          step(5, "Stop the engine. Key OFF."),
          PageBreak()]
    # ---------------- Task 6 CAN pairs
    s += [P("Task 6: Find the CAN pairs", H2),
          P("A CAN pair is two pins that work together: CAN high and CAN low.", BODY),
          step(1, "Look at your Key ON readings. Find two pins near 2.5 V. "
                  "The high pin reads about 2.5 to 3.5 V. The low pin reads about 1.5 to 2.5 V. "
                  "Together they add up to about 5 V.", 0),
          step(2, "Key OFF. Close the doors. Wait 10 minutes. Check that both pins now read 0 V."),
          step(3, "Set the meter to ohms. Measure between the two pins of each pair below."),
          step(4, f"Read the result: about 60 {OHM} = a CAN bus with both end resistors (normal). "
                  f"About 120 {OHM} = only one end resistor. OL = not a pair.", 0),
          grid([["Pins", f"{OHM} key OFF", "High pin V (key ON)", "Low pin V (key ON)", "Is it a pair?"],
                ["6 and 14", "", "", "", ""], ["3 and 11", "", "", "", ""], ["1 and 9", "", "", "", ""],
                ["12 and 13", "", "", "", ""], ["___ and ___", "", "", "", ""], ["___ and ___", "", "", "", ""],
                ["___ and ___", "", "", "", ""]],
               [1.25 * inch, 1.2 * inch, 1.55 * inch, 1.55 * inch, 1.45 * inch],
               rowh=[0.5 * inch] + [0.48 * inch] * 7),
          PageBreak()]
    # ---------------- Task 7 decode
    s += [P("Task 7: Name each pin", H2),
          P("Compare your readings with this table. Then write the name in the big table.", BODY),
          grid([["Key OFF", "Key ON", "Other test", "It is probably"],
                ["0 V", "0 V", f"Less than 1 {OHM} to pin 4", "Ground"],
                ["0 V", "0 V", "OL to pin 4", "Not used (or no metal pin)"],
                ["12 V", "12 V", "", "Battery + (always on)"],
                ["0 V", "12 V", "", "Ignition power (on with the key)"],
                ["0 V", "2.5 to 3.5 V", f"60 {OHM} to its partner", "CAN high"],
                ["0 V", "1.5 to 2.5 V", f"60 {OHM} to its partner", "CAN low"],
                ["0 V", "about 12 V", "Pin 7", "K-line (serial line, idle high)"],
                ["0 V", "0 to 5 V, moves", "", "Other data line. Write it down"]],
               [1.2 * inch, 1.6 * inch, 2.0 * inch, 2.2 * inch],
               rowh=[0.5 * inch] + [0.55 * inch] * 8),
          Spacer(1, 10),
          P("Notes:", BODY),
          step(1, "A meter shows an average. A CAN pin with traffic can show a steady value. That is normal."),
          step(2, "Some modules stay awake after Key OFF. If a CAN pin still shows volts, wait longer."),
          step(3, "Do the ohms test only at 0 V. Volts on the pins make the ohms reading wrong."),
          NextPageTemplate("landscape"), PageBreak()]
    # ---------------- Big 16-pin table (landscape)
    hdr = ["Pin", "Standard use", "Key OFF V", "Key ON V", "Idle V", f"{OHM} (pair)", "What it is / my notes"]
    std = {1: "Maker use", 2: "J1850 +", 3: "Maker use", 4: "Chassis gnd", 5: "Signal gnd", 6: "CAN high",
           7: "K-line", 8: "Maker use", 9: "Maker use", 10: "J1850 -", 11: "Maker use", 12: "Maker use",
           13: "Maker use", 14: "CAN low", 15: "L-line", 16: "Battery +"}
    rows = [hdr] + [[f"<b>{i}</b>", std[i], "", "", "", "", ""] for i in range(1, 17)]
    s += [P("All 16 pins", ParagraphStyle("H2s", parent=H2, spaceAfter=6, fontSize=18)),
          grid(rows, [0.55 * inch, 1.55 * inch, 0.95 * inch, 0.95 * inch, 0.9 * inch, 0.95 * inch, 4.25 * inch],
               rowh=[0.45 * inch] + [0.40 * inch] * 16),
          NextPageTemplate("portrait"), PageBreak()]
    # ---------------- Notes page
    s += [P("Notes", H2), Lines(21, "", gap=0.42 * inch, indent=0)]
    doc.build(s)

def build_log(path):
    L = landscape(letter)
    doc = BaseDocTemplate(path, pagesize=L, title="OBD2 Message ID Log", author="Build")
    def footer(c, d):
        c.saveState(); c.setFont("Sans", 10)
        c.drawString(0.45 * inch, 0.35 * inch, "OBD2 Message ID log  |  2017 Ram 3500")
        c.drawRightString(L[0] - 0.45 * inch, 0.35 * inch, f"Page {d.page}"); c.restoreState()
    f = Frame(0.45 * inch, 0.55 * inch, L[0] - 0.9 * inch, L[1] - 0.95 * inch, id="l")
    doc.addPageTemplates([PageTemplate("l", [f], onPage=footer)])
    hdr = ["Message ID", "Bus / pins", "Byte / bit that changed", "Action that caused it", "Notes"]
    w = [1.25 * inch, 1.25 * inch, 1.9 * inch, 2.4 * inch, 3.3 * inch]
    s = []
    for pg in range(2):
        s += [P("Message ID findings", ParagraphStyle("h", parent=H2, fontSize=18, spaceAfter=6)),
              grid([hdr] + [[""] * 5 for _ in range(15)], w, rowh=[0.42 * inch] + [0.415 * inch] * 15)]
        if pg == 0: s.append(PageBreak())
    doc.build(s)

if __name__ == "__main__":
    here = os.path.dirname(os.path.abspath(__file__))
    build_guide(os.path.join(here, "OBD2_pin_ID_guide.pdf"))
    build_log(os.path.join(here, "OBD2_message_ID_log.pdf"))
    print("done")
