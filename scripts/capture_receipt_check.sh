#!/usr/bin/env bash
# ===========================================================================
# capture_receipt_check.sh -- verify a capture receipt against its own capture.
#
#   usage: capture_receipt_check.sh <uitest.log> <name> <out_crop.png> <capture.png> [occurrence]
#
# Reads the Nth (default: last) `[capture-receipt] name=<name> ...` line from
# a windowed run's log and the capture the sidecar took during that receipt's
# hold (capture_receipt_hold.sh). Then, receipt against ITS OWN capture only:
#   1. owned=1, and the receipt's pid is the pid the sidecar captured for
#   2. capture px == frame_pt x backing           same window, no shadow
#   3. crop_px lies inside the capture             bounds
#   4. name == capture_marker ONLY: the crop is EXACTLY the marker's pixels --
#      fill #10C020 everywhere, and a 6pt (x backing) square of #E020A0 in
#      each corner, checked at each corner's centre and just inside its edges;
#      the pixel one step OUTSIDE each crop edge's midpoint is NOT the fill.
#      Colours are compared in sRGB after converting the capture through its
#      embedded display profile (raw capture values are display-space). That
#      proves the transform lands on known pixels at known offsets. Arbitrary
#      panels (strokes, rounded corners, children) get 1-3 only: the transform
#      they ride is the one the marker proved, on the same window state.
# Never a reference image; never a shifted or resampled row.
# ===========================================================================
set -u
LOG="${1:?usage: capture_receipt_check.sh <uitest.log> <name> <out_crop.png> <capture.png> [occurrence]}"
NAME="${2:?name}"; OUT="${3:?out crop}"; CAP="${4:?capture png}"; OCC="${5:-last}"

if [ "$OCC" = "last" ]; then
    line="$(grep -F "[capture-receipt] name=$NAME " "$LOG" | tail -1)"
else
    line="$(grep -F "[capture-receipt] name=$NAME " "$LOG" | sed -n "${OCC}p")"
fi
if [ -z "$line" ]; then echo "capture_receipt_check: no receipt #$OCC for '$NAME' in $LOG" >&2; exit 2; fi
# One field, by its exact name (a word boundary before it): the line carries
# both `backing=` and `letterbox_scale=`, and a loose match on "scale" read
# the letterbox's 1.0 as the backing scale.
field() { echo "$line" | sed -nE "s/.*[[:space:]]$1=([^ ]+).*/\1/p"; }
OWNED="$(field owned)"; PID="$(field pid)"; FRAME="$(field frame_pt)"; CROP="$(field crop_px)"
# The backing scale. Receipts before d3f64b2's successor spelled it `scale=`
# (the FIRST of two `scale=` fields on the line; the last was the letterbox's):
# read `backing=` and fall back to the first `scale=`.
SCALE="$(field backing)"
[ -n "$SCALE" ] || SCALE="$(echo "$line" | sed -nE 's/.*[[:space:]]scale=([^ ]+) content_px=.*/\1/p')"
if [ -z "$SCALE" ]; then echo "capture_receipt_check: receipt has no backing scale" >&2; exit 2; fi
if [ "$OWNED" != "1" ]; then echo "FAIL 1: receipt says the window is not ours (owned=$OWNED)" >&2; exit 3; fi
if [ -f "$CAP.pid" ] && [ "$(cat "$CAP.pid")" != "$PID" ]; then
    echo "FAIL 1: the capture was taken for pid $(cat "$CAP.pid") but the receipt is pid $PID" >&2; exit 3
fi

python3 - "$CAP" "$OUT" "$FRAME" "$SCALE" "$CROP" "$NAME" <<'PY'
import sys, io
from PIL import Image, ImageCms
cap,out,frame,scale,crop,name=sys.argv[1:7]
# A screencapture carries the DISPLAY's ICC profile; its pixel values are in
# that display's space, not sRGB (the first run read the painted #E020A0 as
# (206,55,156)). Convert through the embedded profile to sRGB before any
# colour comparison; the crop written out is the converted image, so its
# values can be read directly. Geometry is untouched by this.
def to_srgb(im):
    icc=im.info.get('icc_profile')
    rgb=im.convert('RGB')
    if not icc: return rgb, 'no profile (values taken as sRGB)'
    src=ImageCms.ImageCmsProfile(io.BytesIO(icc))
    return ImageCms.profileToProfile(rgb, src, ImageCms.createProfile('sRGB'), outputMode='RGB'), ImageCms.getProfileDescription(src).strip()
fw,fh=[float(v) for v in frame.split(',')[2].split('x')]
scale=float(scale)
cx,cy=[int(v) for v in crop.split(',')[:2]]; cw,ch=[int(v) for v in crop.split(',')[2].split('x')]
im,profile=to_srgb(Image.open(cap)); W,H=im.size
exp=(round(fw*scale),round(fh*scale))
if (W,H)!=exp:
    print(f"FAIL 2: capture is {W}x{H} px, receipt frame {fw}x{fh} pt at {scale}x = {exp[0]}x{exp[1]} -- not the same window, or a shadow was included"); sys.exit(4)
if cx<0 or cy<0 or cx+cw>W or cy+ch>H:
    print(f"FAIL 3: crop {cx},{cy} {cw}x{ch} lies outside the {W}x{H} capture"); sys.exit(5)
c=im.crop((cx,cy,cx+cw,cy+ch)); c.save(out)
if name!='capture_marker':
    print(f"OK 1-3 ({name}: bounds/pid/frame; pixel proof is the marker's): crop {cw}x{ch} at {cx},{cy} -> {out}"); sys.exit(0)
FILL=(0x10,0xC0,0x20); INK=(0xE0,0x20,0xA0)
near=lambda p,q: all(abs(a-b)<=4 for a,b in zip(p,q))
k=round(6*scale)  # corner square in pixels
bad=[]
# corner squares: centre and the pixel just inside each edge of the square
for (ox,oy) in ((0,0),(cw-k,0),(0,ch-k),(cw-k,ch-k)):
    for (px,py) in ((ox+k//2,oy+k//2),(ox+1,oy+1),(ox+k-2,oy+k-2)):
        if not near(c.getpixel((px,py)),INK): bad.append(('corner',px,py,c.getpixel((px,py))))
# fill: centre, and the pixel just inside each edge's midpoint, and just outside each corner square along both axes
for (px,py) in ((cw//2,ch//2),(cw//2,1),(cw//2,ch-2),(1,ch//2),(cw-2,ch//2),(k+1,k//2),(k//2,k+1),(cw-k-2,k//2),(k//2,ch-k-2)):
    if not near(c.getpixel((px,py)),FILL): bad.append(('fill',px,py,c.getpixel((px,py))))
outside=[(cx+cw//2,cy-1),(cx+cw//2,cy+ch),(cx-1,cy+ch//2),(cx+cw,cy+ch//2)]
out_fill=[(x,y) for x,y in outside if 0<=x<W and 0<=y<H and near(im.getpixel((x,y)),FILL)]
if bad:
    print("FAIL 4: marker pixels not where the receipt says:"); [print("   ",b) for b in bad[:8]]; sys.exit(6)
if out_fill:
    print(f"FAIL 4: fill continues outside the crop at {out_fill} -- the crop edge is not the marker's edge"); sys.exit(7)
print(f"OK 1-4: marker crop {cw}x{ch} at {cx},{cy} -> {out}: corners and fill at the receipt's offsets, edges exact (colours via '{profile}' -> sRGB)")
PY
