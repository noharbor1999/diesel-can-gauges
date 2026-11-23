# sniffer_usb_schematic_v1: engineering schematic, single wired OBD2 CAN sniffer (XIAO ESP32-S3 + WCMCU-230).
# Nov 22 2026, drawn for this project. On-screen color (colorblind-safe: blue/orange/gold; every net also labeled).
import matplotlib; matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle, Circle
K='black'; BLUE='#1f5fbf'; ORG='#e07000'; GOLD='#a07800'; GRAY='#e8e8e8'
fig=plt.figure(figsize=(11,8.5)); ax=fig.add_axes([0,0,1,1]); ax.set_xlim(0,11); ax.set_ylim(0,8.5); ax.axis('off'); ax.set_aspect('equal')
def W(*p,c=K,lw=1.5,ls='-'):
    xs,ys=zip(*p); ax.plot(xs,ys,color=c,lw=lw,ls=ls,solid_capstyle='butt',zorder=2)
def T(x,y,s,fs=7,ha='left',va='center',b=False,c=K):
    ax.text(x,y,s,fontsize=fs,ha=ha,va=va,color=c,fontweight='bold' if b else 'normal',zorder=5)
def dot(x,y,c=K): ax.add_patch(Circle((x,y),0.045,fc=c,ec=c,zorder=6))
def NL(x,y,s,c=K,ha='left'):
    ax.text(x,y+0.03,s,fontsize=6.5,ha=ha,va='bottom',color=c,fontweight='bold',zorder=5,
            bbox=dict(boxstyle='square,pad=0.12',fc='white',ec=c,lw=0.8))
def gnd(x,y):
    W((x,y),(x,y-0.12))
    for i,w in enumerate((0.14,0.09,0.04)): W((x-w,y-0.12-i*0.05),(x+w,y-0.12-i*0.05),lw=1.3)
def box(x,y,w,h,ref,name,sub=''):
    ax.add_patch(Rectangle((x,y),w,h,fc='white',ec=K,lw=1.7,zorder=3))
    T(x,y+h+0.08,ref,9,b=True,va='bottom'); T(x+w/2,y+h-0.22,name,7.5,ha='center',b=True)
    if sub: T(x+w/2,y+h-0.45,sub,6,ha='center')
# frame
ax.add_patch(Rectangle((0.25,0.25),10.5,8.0,fc='none',ec=K,lw=2))
ax.add_patch(Rectangle((0.32,0.32),10.36,7.86,fc='none',ec=K,lw=0.6))
T(0.55,7.9,'OBD2 CAN SNIFFER, WIRED USB VERSION (one bus at a time, listen-only)',11,b=True)

# J1 OBD2 breakout
jx,jy,jw,jh=0.8,3.3,1.7,3.6
box(jx,jy,jw,jh,'J1','OBD2 BREAKOUT BOX','truck DLC, 2017 Ram 3500')
pins=[(6.1,'6','CAN-H  bus A',ORG),(5.6,'14','CAN-L  bus A',ORG),(4.9,'3','CAN-H  bus B',GOLD),(4.4,'11','CAN-L  bus B',GOLD),(3.8,'5','SIGNAL GND',K)]
for y,p,n,c in pins:
    W((jx+jw,y),(jx+jw+0.2,y),c=c); T(jx+jw-0.07,y,f'{n}   pin {p}',6.3,ha='right')
T(jx+0.08,3.5,'pin 16 (+12 V BATT) and pin 4: NOT USED',5.8)
# bus B jumpers (alternate) dashed
xA=jx+jw+0.2

# U1 WCMCU-230
ux,uy,uw,uh=4.3,3.3,1.9,3.6
box(ux,uy,uw,uh,'U1','WCMCU-230','SN65HVD230, 3.3 V')
uL={'CANH':6.1,'CANL':5.6,'GND':3.8}
uR={'3V3':6.4,'CTX':5.7,'CRX':5.0,'GND':4.2}
for n,y in uL.items(): W((ux-0.2,y),(ux,y)); T(ux+0.07,y,n,6.5)
for n,y in uR.items(): W((ux+uw,y),(ux+uw+0.2,y)); T(ux+uw-0.07,y,n,6.5,ha='right')
# R1 terminator removed
T(ux+uw/2,4.75,'R1  120 \u03a9 terminator',6,ha='center')
T(ux+uw/2,4.55,'REMOVED (DNP)',6,ha='center',b=True)
T(ux+uw/2,4.35,'truck bus already \u224860 \u03a9',5.6,ha='center')
# CAN wires J1 -> U1 (bus A solid)
W((xA,6.1),(ux-0.2,6.1),c=ORG,lw=2); W((xA,5.6),(ux-0.2,5.6),c=ORG,lw=2)
NL(2.9,6.1,'CANH (A)',ORG); NL(2.9,5.6,'CANL (A)',ORG)
# bus B alternate dashed
W((xA,4.9),(3.5,4.9),(3.5,6.1),c=GOLD,lw=1.2,ls=(0,(3,2)))
W((xA,4.4),(3.75,4.4),(3.75,5.6),c=GOLD,lw=1.2,ls=(0,(3,2)))
T(2.75,4.65,'bus B: move the two\nCAN wires here (dashed)',5.6)
# GND J1 pin5 -> U1 GND
W((xA,3.8),(ux-0.2,3.8),lw=2); NL(2.9,3.8,'GND')
dot(3.2,3.8); gnd(3.2,3.8)

# U2 XIAO
xx,xy,xw,xh=7.6,3.3,1.9,3.6
box(xx,xy,xw,xh,'U2','XIAO ESP32-C6','Seeed Studio')
xL={'3V3':6.4,'D4 / GPIO22':5.0,'GND':4.2}
for n,y in xL.items(): W((xx-0.2,y),(xx,y)); T(xx+0.07,y,n,6.5)
T(xx+xw/2,4.75,'LED GPIO15 (onboard)',5.8,ha='center')
T(xx+xw/2,4.55,'firmware: sniffer_node_C6',5.8,ha='center')
T(xx+xw/2,3.5,'USB-C',6.5,ha='center',b=True)
# 3V3 rail
x3=ux+uw+0.2
W((x3,6.4),(xx-0.2,6.4),c=BLUE,lw=2); NL(6.55,6.4,'+3V3',BLUE)
# CTX tied to 3V3
W((x3,5.7),(6.75,5.7),(6.75,6.4),c=BLUE,lw=2); dot(6.75,6.4,BLUE)
T(6.82,6.05,'CTX tied HIGH =\nlisten-only',5.6)
# D3 not connected
# CRX -> D4
W((x3,5.0),(xx-0.2,5.0),c=GOLD,lw=2); NL(6.55,5.0,'CAN_RX',GOLD)
# GND
W((x3,4.2),(xx-0.2,4.2),lw=2); NL(6.55,4.2,'GND'); dot(7.1,4.2); gnd(7.1,4.2)
# USB to laptop
W((xx+xw/2,3.3),(xx+xw/2,2.7),lw=2)
box(xx+0.15,1.95,1.6,0.75,'J2','LAPTOP USB','')
T(xx+0.95,2.2,'SavvyCAN, GVRET',6,ha='center')
T(xx+0.95,2.05,'(powers U2 + U1)',5.6,ha='center')
NL(xx+xw/2+0.08,2.95,'USB 5 V / D+ D\u2212')

# parts list
px,py=0.55,2.95
T(px,py,'PARTS LIST',8,b=True)
rows=[('J1','OBD2 16-pin breakout box','ASIN B0CR9N9ZR8'),
      ('U1','WCMCU-230 (SN65HVD230) CAN board','R1 120 \u03a9 removed'),
      ('U2','Seeed XIAO ESP32-C6',''),
      ('J2','USB-C data cable to laptop',''),
      ('W1\u2013W6','Dupont / hookup wire, label both ends','heat-shrink labels')]
for i,(r,d,n) in enumerate(rows):
    y=py-0.22*(i+1); T(px,y,r,6.3,b=True); T(px+0.55,y,d,6.3); T(px+3.0,y,n,6.0)
# notes
nx=4.6; T(nx,2.95,'NOTES',8,b=True)
notes=['1. Key OFF while you connect or move wires.',
       '2. Run the laptop on its own battery (no inverter charging).',
       '3. CTX on 3V3 = listen-only in hardware. Firmware TX pin (D3) unused.',
       '4. Bus A = pins 6/14 (61.5 \u03a9 measured). Bus B = pins 3/11 (62.8 \u03a9).',
       '5. Key-on readings: 6=2.72 V, 14=2.34 V, 3=2.64 V, 11=2.45 V.',
       '6. Name each log for its pins, e.g. pins_6-14.csv / pins_3-11.csv.',
       '7. Blue = 3.3 V, orange = bus A, gold = bus B / RX, black = GND.']
for i,s in enumerate(notes): T(nx,2.95-0.2*(i+1),s,6.1)
# title block
tx,ty=7.6,0.4
ax.add_patch(Rectangle((tx,ty),3.0,1.3,fc=GRAY,ec=K,lw=1))
T(tx+0.1,ty+1.1,'OBD2 sniff-everything',7.5,b=True)
T(tx+0.1,ty+0.88,'Wired USB sniffer (1 node)',6.5)
T(tx+0.1,ty+0.66,'Dwg: sniffer_usb_schematic_v3   Rev C',6.2)
T(tx+0.1,ty+0.44,'Date: Nov 22 2026   Date block',6.2)
T(tx+0.1,ty+0.22,'Sheet 1 of 1',6.2)
for ext in ('pdf','png'): fig.savefig(f'sniffer_usb_schematic_v3.{ext}',dpi=170)
