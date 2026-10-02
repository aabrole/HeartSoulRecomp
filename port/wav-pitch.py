#!/usr/bin/env python3
"""Are the loudest tones in a WAV dump in tune?
usage: port/wav-pitch.py dump.wav name:startframe-endframe ...   (needs numpy)
For each window, takes the strongest spectral peak between 100 and 2000 Hz and
measures how far it is from the nearest equal-tempered note (A = 440 Hz).
Random pitch gives a mean distance near 25 cents and about 30% within 15."""
import sys
import numpy as np

SPF = 701
RATE = 42060.0  # the engine makes 701 samples per frame at 60 frames a second

def load_wav(path):
    d = open(path, 'rb').read()
    body = d[44:]
    body = body[:len(body) // 8 * 8]
    return np.frombuffer(body, dtype='<f4').reshape(-1, 2).astype(np.float64)

def check(name, x):
    mono = x.mean(axis=1)
    n = 8192
    win = np.hanning(n)
    f = np.fft.rfftfreq(n, 1.0 / RATE)
    lo = np.searchsorted(f, 100)
    hi = np.searchsorted(f, 2000)
    devs = []
    for i in range(0, len(mono) - n, n // 2):
        seg = mono[i:i + n]
        if np.sqrt((seg ** 2).mean()) < 0.01:
            continue
        p = np.abs(np.fft.rfft(seg * win))
        k = lo + int(np.argmax(p[lo:hi]))
        a, b, c = np.log(p[k - 1] + 1e-12), np.log(p[k] + 1e-12), np.log(p[k + 1] + 1e-12)
        delta = 0.5 * (a - c) / (a - 2 * b + c)
        freq = (k + delta) * RATE / n
        cents = 1200 * np.log2(freq / 440.0)
        devs.append(cents - 100 * round(cents / 100))
    devs = np.array(devs)
    if len(devs) == 0:
        print('%-18s no signal' % name)
        return
    print('%-18s %4d windows  mean |dev| %.1f cents  median dev %+.1f  within 15 cents: %.0f%%'
          % (name, len(devs), np.abs(devs).mean(), np.median(devs), 100.0 * (np.abs(devs) <= 15).mean()))

x = load_wav(sys.argv[1])
for a in sys.argv[2:]:
    name, rng = a.split(':')
    s, e = [int(v) for v in rng.split('-')]
    check(name, x[s * SPF:e * SPF])
