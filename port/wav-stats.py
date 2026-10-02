#!/usr/bin/env python3
"""Objective checks on a headless WAV dump (HNS_WAV): level, clipping, NaN, DC
offset, silent frames, jumps at frame edges, stereo correlation, band energy.
usage: port/wav-stats.py file.wav [name:startframe-endframe ...] [--seconds]
One game frame is 701 stereo samples. Needs numpy."""
import sys, struct
import numpy as np

SPF = 701

def load(path):
    d = open(path, 'rb').read()
    assert d[:4] == b'RIFF' and d[8:12] == b'WAVE'
    fmt = struct.unpack('<H', d[20:22])[0]
    ch = struct.unpack('<H', d[22:24])[0]
    rate = struct.unpack('<I', d[24:28])[0]
    bits = struct.unpack('<H', d[34:36])[0]
    assert fmt == 3 and ch == 2 and bits == 32, (fmt, ch, bits)
    body = d[44:]
    body = body[:len(body) // 8 * 8]
    x = np.frombuffer(body, dtype='<f4').reshape(-1, 2).astype(np.float64)
    return rate, x

def bands(x, rate):
    mono = x.mean(axis=1)
    if len(mono) < 4096:
        return {}
    n = 4096
    hop = 2048
    win = np.hanning(n)
    acc = np.zeros(n // 2 + 1)
    cnt = 0
    for i in range(0, len(mono) - n, hop):
        acc += np.abs(np.fft.rfft(mono[i:i + n] * win)) ** 2
        cnt += 1
    acc /= max(cnt, 1)
    f = np.fft.rfftfreq(n, 1.0 / rate)
    tot = acc.sum() + 1e-30
    out = {}
    for lo, hi in ((0, 200), (200, 1000), (1000, 4000), (4000, 9000), (9000, 21100)):
        m = (f >= lo) & (f < hi)
        out['%d-%d' % (lo, hi)] = 100.0 * acc[m].sum() / tot
    return out

def spectral_flatness(x, rate):
    mono = x.mean(axis=1)
    n = 4096
    vals = []
    win = np.hanning(n)
    for i in range(0, len(mono) - n, n):
        seg = mono[i:i + n]
        if np.sqrt((seg ** 2).mean()) < 1e-4:
            continue
        p = np.abs(np.fft.rfft(seg * win)) ** 2 + 1e-20
        vals.append(np.exp(np.log(p).mean()) / p.mean())
    return float(np.median(vals)) if vals else float('nan')

def report(name, x, rate):
    n = len(x)
    if n == 0:
        print(name, 'EMPTY')
        return
    bad = int((~np.isfinite(x)).sum())
    x = np.nan_to_num(x)
    peak = np.abs(x).max()
    rms = np.sqrt((x ** 2).mean())
    dc = x.mean(axis=0)
    over = int((np.abs(x) > 1.0).sum())
    at = int((np.abs(x) >= 0.999).sum())
    # per-frame silence
    nf = n // SPF
    fr = x[:nf * SPF].reshape(nf, SPF, 2)
    frms = np.sqrt((fr ** 2).mean(axis=(1, 2)))
    silent = int((frms < 1e-4).sum())
    # discontinuities: sample-to-sample jumps at frame boundaries vs elsewhere
    d = np.abs(np.diff(x[:, 0]))
    idx = np.arange(1, n)
    atb = d[(idx % SPF) == 0]
    off = d[(idx % SPF) != 0]
    corr = float('nan')
    if x[:, 0].std() > 1e-9 and x[:, 1].std() > 1e-9:
        corr = float(np.corrcoef(x[:, 0], x[:, 1])[0, 1])
    b = bands(x, rate)
    print('%-22s %6.1fs peak %.3f rms %.4f (%.1f dBFS) dcL %+.4f dcR %+.4f >1.0: %d (%.3f%%) >=0.999: %d nan/inf %d'
          % (name, n / rate, peak, rms, 20 * np.log10(rms + 1e-12), dc[0], dc[1], over, 100.0 * over / (2 * n), at, bad))
    print('%-22s silent frames %d/%d (%.0f%%)  jump mean boundary/elsewhere %.4f/%.4f max %.3f/%.3f  LRcorr %.2f  flatness %.3f'
          % ('', silent, nf, 100.0 * silent / max(nf, 1), atb.mean() if len(atb) else 0, off.mean(), atb.max() if len(atb) else 0, off.max(), corr, spectral_flatness(x, rate)))
    print('%-22s bands %% ' % '' + '  '.join('%s:%.1f' % kv for kv in b.items()))

def main():
    path = sys.argv[1]
    args = [a for a in sys.argv[2:] if not a.startswith('--')]
    rate, x = load(path)
    report('whole file', x, rate)
    for a in args:
        name, rng = a.split(':')
        s, e = [int(v) for v in rng.split('-')]
        report(name, x[s * SPF:e * SPF], rate)
    if '--seconds' in sys.argv:
        nf = len(x) // SPF
        step = 60
        line = []
        for f0 in range(0, nf, step):
            seg = x[f0 * SPF:(f0 + step) * SPF]
            seg = np.nan_to_num(seg)
            r = np.sqrt((seg ** 2).mean())
            p = np.abs(seg).max()
            line.append('%5d:%.3f/%.2f' % (f0, r, p))
        print('per second, frame:rms/peak')
        for i in range(0, len(line), 8):
            print('  ' + '  '.join(line[i:i + 8]))

main()
