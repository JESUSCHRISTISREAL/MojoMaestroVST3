# Single source of truth for the faceplate layout. Logical px, window 480 x 392, origin top-left, centre line x = 240.
# S is the work scale of the silkscreen mask (masks.py); the shipped pictures are stored at 3x (see build_assets.py).
# Running this file writes ../layout.json.
import json, math
W, H, S = 480, 392, 2
CX = W / 2
KNOB   = dict(cx=CX, cy=190.0, r=52.0)           # big knob body radius
RING   = (60.0, 67.0)                            # printed tick ring radii
SW_X   = (96.0, W - 96.0)                        # slide-switch centre x, mirrored about CX
SW_Y   = (158.0, 190.0, 222.0)                   # cap travel positions (3), centre = knob cy
SLOT   = dict(w=12.0, y0=152.0, y1=228.0)        # slot length = travel + slot width
SMALL_Y = 332.0; SMALL_R = 15.0
SMALL_X = [CX + (i - 2) * 84.0 for i in range(5)]
VFD = dict(cx=240.0, cy=283.0, w=212.0, h=28.0)   # pink dot-matrix display recess
SCREW = 14.0                                     # screw centre inset from each corner
TITLE_Y, SUB_Y, AMOUNT_Y = 41.0, 76.0, 100.0
SIDE_EDGE = 72.0                                 # side labels end here (left) / start at W-72 (right)
SMALL_LABEL_Y = 363.0
MARGIN = 24.0
RS = 4
data = dict(W=W,H=H,S=S,RS=RS,CX=CX,KNOB=KNOB,RING=RING,SW_X=SW_X,SW_Y=SW_Y,SLOT=SLOT,SMALL_Y=SMALL_Y,
            SMALL_R=SMALL_R,SMALL_X=SMALL_X,SCREW=SCREW,VFD=VFD)
import os
json.dump(data, open(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'layout.json'), 'w'), indent=1)
