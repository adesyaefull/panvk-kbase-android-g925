#!/usr/bin/env python3
"""Host verification for BC shadow-image device dumps.

References: Pillow BCn decoder (BC1-3), a spec-direct decoder below
(BC4/BC5 unorm+snorm), and bc_ref for BC6H/BC7.
usage: bc_verify.py <dump_dir>
"""
import math
import struct
import subprocess
import sys

from PIL import Image

IW, IH, LAYERS = 20, 12, 2
MIPS = [(20, 12, 5, 3), (10, 6, 3, 2)]  # w, h, blocks_w, blocks_h
FMTS = ["BC1_RGB_UNORM", "BC1_RGB_SRGB", "BC1_RGBA_UNORM", "BC1_RGBA_SRGB",
        "BC2_UNORM", "BC2_SRGB", "BC3_UNORM", "BC3_SRGB", "BC4_UNORM",
        "BC4_SNORM", "BC5_UNORM", "BC5_SNORM", "BC6H_UFLOAT", "BC6H_SFLOAT",
        "BC7_UNORM", "BC7_SRGB"]


def block_bytes(name):
    return 8 if name.startswith(("BC1", "BC4")) else 16


def rgtc_channel(b, snorm):
    if snorm:
        e0 = struct.unpack("b", b[0:1])[0]
        e1 = struct.unpack("b", b[1:2])[0]
        f0, f1 = max(e0 / 127.0, -1.0), max(e1 / 127.0, -1.0)
    else:
        e0, e1 = b[0], b[1]
        f0, f1 = e0 / 255.0, e1 / 255.0
    bits = int.from_bytes(b[2:8], "little")
    out = []
    for t in range(16):
        i = (bits >> (3 * t)) & 7
        if i == 0:
            v = f0
        elif i == 1:
            v = f1
        elif e0 > e1:
            v = (f0 * (8 - i) + f1 * (i - 1)) / 7.0
        elif i == 6:
            v = -1.0 if snorm else 0.0
        elif i == 7:
            v = 1.0
        else:
            v = (f0 * (6 - i) + f1 * (i - 1)) / 5.0
        out.append(v)
    return out


def decode_layer(name, data, bw, bh, w, h):
    """Return list of (r,g,b,a) floats (linear for unorm, raw for sRGB
    encoded 0..1 values) for a w x h texel region."""
    bb = block_bytes(name)
    px = [[None] * (bw * 4) for _ in range(bh * 4)]
    if name.startswith(("BC1", "BC2", "BC3")):
        n = int(name[2])
        im = Image.frombytes("RGBA", (bw * 4, bh * 4), data, "bcn", n)
        d = list(im.getdata())
        for y in range(bh * 4):
            for x in range(bw * 4):
                r, g, b, a = d[y * bw * 4 + x]
                px[y][x] = [r / 255.0, g / 255.0, b / 255.0, a / 255.0]
        if name.startswith("BC1_RGB_"):
            for row in px:
                for p in row:
                    p[3] = 1.0
    elif name.startswith(("BC4", "BC5")):
        snorm = name.endswith("SNORM")
        for by in range(bh):
            for bx in range(bw):
                blk = data[(by * bw + bx) * bb:(by * bw + bx + 1) * bb]
                r = rgtc_channel(blk[0:8], snorm)
                g = rgtc_channel(blk[8:16], snorm) if bb == 16 else [0.0] * 16
                for t in range(16):
                    px[by * 4 + t // 4][bx * 4 + t % 4] = [r[t], g[t], 0.0, 1.0]
    elif name.startswith(("BC6H", "BC7")):
        decoder = "bc6u" if name == "BC6H_UFLOAT" else \
                  "bc6s" if name == "BC6H_SFLOAT" else "bc7"
        raw = subprocess.check_output(
            ["./bc_ref", decoder, str(bw), str(bh)], input=data)
        values = struct.unpack("%df" % (len(raw) // 4), raw)
        for y in range(bh * 4):
            for x in range(bw * 4):
                i = (y * bw * 4 + x) * 4
                px[y][x] = list(values[i:i + 4])
    return [px[y][x] for y in range(h) for x in range(w)]


def srgb_decode(v):
    v = min(max(v, 0.0), 1.0)
    return v / 12.92 if v < 0.04045 else ((v + 0.055) / 1.055) ** 2.4


def unorm(v, bits):
    maximum = (1 << bits) - 1
    return math.floor(min(max(v, 0.0), 1.0) * maximum + 0.5) / maximum


def snorm(v, bits):
    maximum = (1 << (bits - 1)) - 1
    return round(min(max(v, -1.0), 1.0) * maximum) / maximum


def shadow_pixel(name, pixel):
    r, g, b, a = pixel
    if name.startswith(("BC1", "BC2", "BC3", "BC7")):
        r, g, b = (unorm(c, 8) for c in (r, g, b))
        a = unorm(a, 8)
        if name.endswith("SRGB"):
            r, g, b = (srgb_decode(c) for c in (r, g, b))
        if name.startswith("BC1_RGB_"):
            a = 1.0
        return [r, g, b, a]
    if name == "BC6H_UFLOAT":
        return [ufloat(r, 6), ufloat(g, 6), ufloat(b, 5), 1.0]
    if name == "BC6H_SFLOAT":
        return [struct.unpack("<e", struct.pack("<e", c))[0]
                for c in (r, g, b)] + [1.0]
    if name.startswith("BC4"):
        channel = snorm if name.endswith("SNORM") else unorm
        return [channel(r, 8), 0.0, 0.0, 1.0]
    channel = snorm if name.endswith("SNORM") else unorm
    return [channel(r, 8), channel(g, 8), 0.0, 1.0]


def ufloat(value, mantissa_bits):
    """Round a non-negative value to the 5-bit-exponent B10G11R11 format."""
    if value <= 0.0:
        return 0.0
    if not math.isfinite(value):
        return value

    exponent = math.floor(math.log2(value))
    biased = exponent + 15
    if biased <= 0:
        quantum = 2.0 ** (1 - 15 - mantissa_bits)
        return round(value / quantum) * quantum
    if biased >= 31:
        return math.inf

    mantissa = round((value / (2.0 ** exponent) - 1.0) *
                     (1 << mantissa_bits))
    if mantissa == (1 << mantissa_bits):
        mantissa = 0
        exponent += 1
        biased += 1
    if biased >= 31:
        return math.inf
    return (1.0 + mantissa / (1 << mantissa_bits)) * (2.0 ** exponent)


def compare(name, dev, ref):
    """Max error in the unit the format is judged in."""
    worst = 0.0
    for d, r in zip(dev, ref):
        for c in range(4):
            scale = 255.0 / max(1.0, abs(r[c])) if name.startswith("BC6H") \
                    else 255.0
            err = abs(d[c] - r[c]) * scale
            worst = max(worst, err)
    return worst


def tol(name):
    if name.startswith(("BC1", "BC2", "BC3", "BC6H", "BC7")):
        return 5.0
    return 1.0


def expected(name, blocks, sub):
    bb = block_bytes(name)
    out = []
    off = 0
    per_mip = []
    for (w, h, bw, bh) in MIPS:
        layers = []
        for _ in range(LAYERS):
            layers.append(bytearray(blocks[off:off + bw * bh * bb]))
            off += bw * bh * bb
        per_mip.append(layers)
    if sub is not None:
        lay = per_mip[0][1]
        for sy in range(2):
            for sx in range(2):
                s = sub[(sy * 2 + sx) * bb:(sy * 2 + sx + 1) * bb]
                di = ((1 + sy) * 5 + (2 + sx)) * bb
                lay[di:di + bb] = s
    for mi, (w, h, bw, bh) in enumerate(MIPS):
        for l in range(LAYERS):
            out += decode_layer(name, bytes(per_mip[mi][l]), bw, bh, w, h)
    return [shadow_pixel(name, pixel) for pixel in out]


def load_f4(path):
    raw = open(path, "rb").read()
    f = struct.unpack("%df" % (len(raw) // 4), raw)
    return [list(f[i:i + 4]) for i in range(0, len(f), 4)]


def main():
    d = sys.argv[1]
    fails = 0
    for name in FMTS:
        blocks = open("%s/%s.in" % (d, name), "rb").read()
        sub = open("%s/%s.sub" % (d, name), "rb").read()
        e1 = expected(name, blocks, None)
        e2 = expected(name, blocks, sub)
        w1 = compare(name, load_f4("%s/%s.up" % (d, name)), e1)
        w2 = compare(name, load_f4("%s/%s.up2" % (d, name)), e2)
        ok = w1 <= tol(name) and w2 <= tol(name)
        fails += not ok
        print("VERIFY %s upload_maxerr=%.4g subupdate_maxerr=%.4g tol=%g %s"
              % (name, w1, w2, tol(name), "PASS" if ok else "FAIL"))
    print("BC_VERIFY_FAILS=%d" % fails)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
