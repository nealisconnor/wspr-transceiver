# Lab 6 — WSPR Transmitter Integration

**ELEC3607 — WSPR Transceiver Project**

## Overview

This testing adds WSPR transmission capability to the existing Lab 6 receiver setup. Two new C files are added to the lab's `lab6-implementation/` directory alongside the existing decoder files (`wsprd.c`, `parec.c`, etc.):

so basically you have to ensure wsprd.c the old file can run and those are all the files that are needed for that + encode file and transmit file. (also additionally si5351 related program just to connect the i2c and the wav gen file to use the encode file to make a wav file.)

| New File | Purpose |
|----------|---------|
| `wspr_encode.c` | Encodes callsign, grid locator, and power into 162 WSPR symbols |
| `wspr_transmit.c` | Drives Si5351A CLK2 with the encoded symbols as 4-FSK tones |
| `wspr_wav_gen.c` | Generates a test WAV file to verify the encoder + decoder pipeline |

These files use the existing lab infrastructure — the Si5351A on I²C bus, the `k9an-wsprd` decoder, and the `si5351_prog` receiver initialiser in the `si5351/` subdirectory.

## Setup

All files go in `~/elec3607-lab/labs/lab6-implementation/` on the board:
(wsprcan in lab 5 pretty much)
```
lab6-implementation/
├── wsprd.c              (existing — decoder)
├── parec.c              (existing — PulseAudio capture)
├── wsprwait             (existing — timing script)
├── wspr_encode.c        (NEW — encoder)
├── wspr_transmit.c      (NEW — transmitter)
├── wspr_wav_gen.c       (NEW — test WAV generator)
├── Makefile             (updated — builds new targets)
├── si5351/
│   ├── si5351prog.c     (existing — receiver CLK0/CLK1 init)
│   ├── Si5351A-RevB-Registers.h
│   └── Makefile
└── (other existing lab files: fano.c, jelinek.c, nhash.c, etc in lab 5 wsprcan.)
```

## Building

```bash
# Build decoder + transmitter + WAV generator
cd  into whereever all the files are
make

# Build Si5351 receiver initialiser (separate Makefile)
cd si5351/  # just into si5351 folder
make
cd ..
```
the si5351 is just what we used in the previous labs nothing changed I just add the file here in case. Same header etc, code slightly different from yours but I think it should just work fine?
The steps above here is basically just...
make the si5351 file
then make the entire lab6-implementation file which has both the encode, transmission and wav gen. Technically in the real thing we won't need wave gen but I don't think it would make it worse if we have it anyway.

## Testing (No RF Hardware Needed)

Verify the encoder and decoder work together using a WAV file:

```bash
# Generate a test WAV with encoded WSPR message
# the ending here is based on what you tested in your previous receiver and what not so can change it
./wspr_wav_gen VK2AAX QF56 30



# Decode it — should print "VK2AAX QF56 30"
./k9an-wsprd -f 7.0386 wspr_test.wav
```

Expected output:
```
test  43 -2.0   7.040102 -2  VK2AAX QF56  30
<DecodeFinished>
```

You can play around with the values a bit, in wsprd the formula uses 1.5 kHz in line 108.

You can change the tone setting in the wav gen file... to test around.

This proves the full encode → audio → decode pipeline works without any RF hardware.

## Hardware RF Loopback Test

Once the board is wired (antenna, attenuator, mixer):


Below are instructions on the real board we can figure that out later but it's something like that, and requires 2 terminals as both need to be running (the transmitting and decoding.)
**Terminal 1 — Receiver setup + Transmit:**
```bash
cd ~/elec3607-lab/labs/lab6-implementation/

# Step 1: Initialise receiver (CLK0/CLK1 via PLL_A)
./si5351/si5351_prog

# Step 2: Transmit WSPR on CLK2 (via PLL_B)
./wspr_transmit VK2AAX QF56 30 --now
```

**Terminal 2 — Decode (start BEFORE transmitting):**
```bash
cd ~/elec3607-lab/labs/lab6-implementation/

# Capture 114 seconds of audio then decode
./wsprwait
./k9an-wsprd -f 7.0386
```

## How the Code Works

### Encoder (`wspr_encode.c`)

Encodes a message into 162 symbols (values 0–3):

1. Packs callsign (28 bits) + grid/power (22 bits) = 50 data bits
2. Convolutional encoding (rate ½) → 162 coded bits
3. Bit-reversal interleaving
4. Merges with sync vector: `symbol = sync[i] + 2 × data[i]`

### Transmitter (`wspr_transmit.c`)

Calls `encode_wspr()` from `wspr_encode.c`, then for each of the 162 symbols:

1. Looks up the tone frequency: `f = 7,040,100 + symbol × 1.5 Hz`
2. Computes Si5351 PLL_B register values (P1, P2, P3) per AN619
3. Writes registers to Si5351A over I²C
4. Waits 682 ms (one symbol period)

### Frequency Plan

| Clock | Frequency | Role |
|-------|-----------|------|
| CLK0 (PLL_A) | 7.0386 MHz | Receiver LO (I) |
| CLK1 (PLL_A) | 7.0386 MHz, 90° shifted | Receiver LO (Q) |
| CLK2 (PLL_B) | 7.04010–7.04015 MHz | Transmitter (4 tones) |

The mixer produces: `CLK2 − CLK0 = 1500 Hz audio`, which is where the decoder searches.

### Tone Spacing Note

WSPR specifies 1.4648 Hz (12000/8192) tone spacing. The Si5351A achieves ~1.5 Hz due to its PLL integer register constraint (`b` must be an integer in `a + b/c`, see AN619 §3.2). The decoder handles this — verified by the WAV test.

## Why +1500 Hz Offset?

The transmitter outputs at 7.04010 MHz, NOT at the dial frequency of 7.0386 MHz. This is because the receiver mixer subtracts the LO:

```
Audio = TX_freq - LO_freq
     = 7,040,100 - 7,038,600
     = 1,500 Hz
```

The decoder (`wsprd.c` line 108) searches for signals at 1500 Hz audio. This is the WSPR standard — signals appear in a 200 Hz window (1400–1600 Hz) centred at 1500 Hz above the dial frequency.

From the lab spec:
> "The upper and lower frequencies are 7.0400-7.0402 MHz so we expect the downconverted signal to be centered around 1.5 kHz."

If the transmitter sent at 7.0386 MHz (same as LO), the mixer output would be 0 Hz (DC) — silence. The signal MUST be offset by 1500 Hz so the mixer produces an audible tone for the decoder.

## Why 1.5 Hz Tone Spacing Instead of 1.4648 Hz?

The WSPR standard specifies exactly 1.4648 Hz (12000/8192) between the 4 FSK tones. However, the Si5351A PLL cannot achieve this exact spacing due to hardware register constraints.

The Si5351 frequency is set by: `f = xtal × (a + b/c) / N`, where `b` and `c` are **integers** stored in 20-bit registers (AN619 §3.2, P2[19:0] and P3[19:0]).

With our configuration (27 MHz crystal, c = 1,000,000, N = 126):
- Minimum frequency step = 27,000,000 / (1,000,000 × 126) = **0.214 Hz**
- To get 1.4648 Hz: 1.4648 / 0.214 = **6.84 steps** — not a whole number!
- Round to 7 steps: 7 × 0.214 = **1.500 Hz** (closest achievable)

Even using the maximum c = 1,048,575 (20-bit limit), the step count is still not an integer. This is a fundamental hardware limitation of the Si5351A's fractional-N PLL — you cannot write a fractional value to an integer register.

The ~2.4% spacing error is well within the decoder's tolerance. This was verified by generating a WAV file with 1.5 Hz spacing and successfully decoding it with `k9an-wsprd`.
