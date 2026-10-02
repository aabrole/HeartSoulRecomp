#!/usr/bin/env python3
"""Find a known sample in a WAV dump by normalised cross-correlation.
usage: port/wav-find-sample.py dump.wav sample.bin [startframe endframe]
Decodes the sample from ROM data (BDPCM if compressed), resamples it to the
output rate at several speed ratios and reports the best match for each. A cry
or sound effect that is decoded and pitched right scores far above the rest at
the speed it should play at. Needs numpy."""
import sys, struct
import numpy as np

SPF = 701
OUT_RATE = 42060.0
DELTAS = [0, 1, 4, 9, 16, 25, 36, 49, -64, -49, -36, -25, -16, -9, -4, -1]

def load_wav(path):
    d = open(path, 'rb').read()
    body = d[44:]
    body = body[:len(body) // 8 * 8]
    return np.frombuffer(body, dtype='<f4').reshape(-1, 2).astype(np.float64)

def load_sample(path):
    d = open(path, 'rb').read()
    t, st, fr, ls, sz = struct.unpack('<HHIII', d[:16])
    data = d[16:]
    if t & 1:
        out = []
        nblocks = (sz + 63) // 64
        for b in range(nblocks):
            blk = data[b * 33:(b + 1) * 33]
            if len(blk) < 33:
                break
            s = struct.unpack('b', blk[0:1])[0]
            out.append(s)
            s = ((s + DELTAS[blk[1] & 0xF] + 128) & 0xFF) - 128
            out.append(s)
            for k in range(2, 33):
                s = ((s + DELTAS[blk[k] >> 4] + 128) & 0xFF) - 128
                out.append(s)
                s = ((s + DELTAS[blk[k] & 0xF] + 128) & 0xFF) - 128
                out.append(s)
        pcm = np.array(out[:sz], dtype=np.float64)
    else:
        pcm = np.frombuffer(data[:sz], dtype=np.int8).astype(np.float64)
    return fr / 1024.0, pcm, t

def resample(pcm, src_rate, ratio):
    step = src_rate * ratio / OUT_RATE
    n = int(len(pcm) / step) - 1
    pos = np.arange(n) * step
    i = pos.astype(int)
    f = pos - i
    return pcm[i] * (1 - f) + pcm[np.minimum(i + 1, len(pcm) - 1)] * f

def ncc(x, t):
    n = len(x) + len(t) - 1
    size = 1 << (n - 1).bit_length()
    c = np.fft.irfft(np.fft.rfft(x, size) * np.conj(np.fft.rfft(t, size)), size)[:len(x) - len(t) + 1]
    cs = np.concatenate(([0.0], np.cumsum(x * x)))
    e = cs[len(t):] - cs[:-len(t)]
    den = np.sqrt(np.maximum(e, 1e-12) * (t * t).sum())
    r = c / den
    r[e < 1e-6 * len(t)] = 0
    return r

def main():
    x = load_wav(sys.argv[1]).mean(axis=1)
    rate, pcm, t = load_sample(sys.argv[2])
    s = int(sys.argv[3]) * SPF if len(sys.argv) > 3 else 0
    e = int(sys.argv[4]) * SPF if len(sys.argv) > 4 else len(x)
    x = x[s:e]
    print('sample %s: %s, %d samples at %.0f Hz (%.2f s)' % (sys.argv[2].split('/')[-1], 'compressed' if t & 1 else 'pcm', len(pcm), rate, len(pcm) / rate))
    for ratio in (0.5, 0.9, 1.0, 1.1, 1.3571, 2.0):
        tpl = resample(pcm, rate, ratio)
        tpl = tpl - tpl.mean()
        if len(tpl) >= len(x):
            continue
        r = ncc(x, tpl)
        k = int(np.argmax(np.abs(r)))
        print('  speed x%.2f: best |r| %.3f at frame %d (median |r| %.4f)' % (ratio, abs(r[k]), (s + k) // SPF, np.median(np.abs(r))))

main()
