### **1. System Overview (Signal Path Description)**

**Receive path:** Antenna → SMA (BPFIN) → Bandpass Filter (7-element Chebyshev, L1/L2/L3) → Tayloe mixer (TS3A5017) driven by Si5351A CLK0/CLK1 at 7.0386 MHz in quadrature → TL972 audio amplifier → QOUT → USB sound card → PulseAudio → k9an-wsprd software decoder.

**Transmit path:** Si5351A CLK2 (PLL\_B, 7.040100 MHz) → resistive attenuator → BPFIN (loopback) → same receive path above.

### **2. Hardware Modification — Attenuator and Loopback**

Instead of a simple resistor divider, used two RF attenuators in series connected between CLK2\_TP6 and the BPFIN SMA connector centre pin, with alligator clips and a wire.

**Why attenuation is needed:** The Si5351A CLK2 output is a 3.3V LVCMOS square wave — approximately 0 dBm into 50Ω. The receiver front end expects signals in the microvolt to millivolt range (typical HF amateur signals are −100 to −70 dBm over the air). Without attenuation the mixer and amplifier stages would be driven far into saturation. The attenuators bring the CLK2 output down to a level compatible with the receiver input.

**Include in report:**

* A schematic diagram of the loopback connection:
* Photo of physical PCB showing the two attenuators and alligator clip connections (IMG\_0655 and IMG\_0656)
* The combined attenuation value in dB (add the two attenuator values together, 2× 20 dB = 40 dB total)
* The resulting signal level at BPFIN:

P\_in = P\_CLK2 - Attenuation\_dB

### **3. Software: Commands Run**

List these in order with brief explanation of each:

**Step 1 Initialise Si5351A crystal (CLK0/CLK1 for receiver LO):**

`bash

cd si5351/
sudo ./i2cread

`

This programs the Si5351A over I²C bus 3 at address 0x60 using the ClockBuilder Pro register map, setting CLK0 and CLK1 to 7.0386 MHz in quadrature on PLL\_A for the receiver local oscillator. CLK2 is left disabled at this stage.

**Step 2 — Configure PulseAudio and prevent sound card suspend:**

`bash

pactl set-default-source alsa\_input.usb-Plugable\_Plugable\_USB\_Audio\_Device\_000000000000-00.analog-stereo
parec --device=alsa\_input.usb-Plugable\_Plugable\_USB\_Audio\_Device\_000000000000-00.analog-stereo > /dev/null &

`

The first command sets the USB sound card microphone input as the default PulseAudio capture source. The second keeps the device in RUNNING state, without it PulseAudio suspends the card after a few seconds of silence, causing the decoder to record silence instead of the received audio.

**Step 3 — Start decoder (Terminal 2, before transmitting):**

`bash

./wsprwait

`

This synchronises to the next even UTC minute boundary and invokes k9an-wsprd to record 114 seconds of audio and attempt decoding. Must be started before the transmitter.

**Step 4 — Transmit (Terminal 1, simultaneously):**

`bash

sudo ./wspr\_transmit VK2AAR QF56 3

`

Encodes the message, configures CLK2 on PLL\_B, and transmits 162 symbols over 110.6 seconds, synchronised to the same even UTC minute.

### **4. Encoder — wspr\_encode.c**

**Step 1 — Callsign packing (28 bits)** The callsign is normalised to 6 characters. If the second character is a digit (single-letter prefix, e.g. G4JNT) a space is prepended; otherwise it is right-padded with spaces (e.g. VK2AAR → "VK2AAR"). The 6-character field is then encoded as a mixed-radix integer:

N = c0×36×10×27×27×27 + c1×10×27×27×27 + c2×27×27×27 + (c3-10)×27×27 + (c4-10)×27 + (c5-10)

where c0 is in [0–36] (alphanumeric + space), c1 in [0–35] (alphanumeric), c2 in [0–9] (digit only), and c3–c5 in [10–36] (alpha only). This produces a 28-bit integer.

wspr\_encode.c L57–L92

![](data:image/png;base64...)

**Step 2 — Grid and power packing (22 bits)** The 4-character Maidenhead grid locator and power level are combined:

M = (179 - 10×(loc[0]-'A') - (loc[2]-'0')) × 180 + 10×(loc[1]-'A') + (loc[3]-'0')
M = M × 128 + power + 64

This produces a 22-bit integer.

wspr\_encode.c L98–L109

![](data:image/png;base64...)

**Step 3 — 50-bit packing** N (28 bits) and M (22 bits) are concatenated into a 50-bit value, zero-padded to 81 bits for the convolutional encoder input.

wspr\_encode.c L115–L123

![](data:image/png;base64...)

**Step 4 — Convolutional FEC encoding (rate ½, K=32)** Each of the 81 input bits is shifted into a 32-bit register. Two parity bits are computed using generator polynomials 0xF2D05351 and 0xE4613C47:

c

p1 = parity32(reg & POLY1);
p2 = parity32(reg & POLY2);

This produces 162 coded bits. The rate-½ code provides the error correction that allows WSPR to decode signals at SNR values as low as −28 dB.

wspr\_encode.c L129–L146

![](data:image/png;base64...)

**Step 5 — Interleaving and sync vector** The 162 coded bits are permuted using an 8-bit bit-reversal interleaver to spread burst errors. Each interleaved bit is then combined with the 162-bit pseudo-random sync vector:

c

symbol[i] = SYNC\_VECTOR[i] + 2 × interleaved[i]

This produces 162 symbols each in {0,1,2,3} — the 4-FSK alphabet.

**Verification:** The encoder output was verified by comparing against the reference Python encoder for VK2AAX QF56 37 — all 162 symbols matched exactly.

wspr\_encode.c L179–L203

![](data:image/png;base64...)

### **5. Transmitter — wspr\_transmit.c**

**Frequency plan:**

|  |  |  |  |
| --- | --- | --- | --- |
| **Clock** | **PLL** | **Frequency** | **Role** |
| CLK0 | PLL\_A | 7.0386 MHz | Receiver LO (I) |
| CLK1 | PLL\_A | 7.0386 MHz, 90° | Receiver LO (Q) |
| CLK2 | PLL\_B | 7.040100–7.040104 MHz | Transmitter |

CLK2 uses PLL\_B independently so changing its frequency does not disturb the receiver LO on PLL\_A.

**Why 7.040100 MHz and not 7.0386 MHz:** The receiver mixer computes f\_audio = f\_TX - f\_LO. If the transmitter used the same frequency as the LO the audio output would be 0 Hz (DC). The transmitter must be offset by 1500 Hz above the LO so the mixer produces an audio tone at 1500 Hz — the centre of the WSPR decoder's search window:

f\_audio = 7,040,100 - 7,038,600 = 1,500 Hz

**PLL\_B register calculation (AN619):** For each tone frequency f, the VCO frequency is computed as f\_VCO = f × N where N=126 (MS2 integer divider). The PLL feedback multiplier M = f\_VCO / f\_xtal is split into integer and fractional parts:

`c

a = floor(M)
b = round((M - a) × c) // c = 1,000,000 (denominator)
P1 = 128×a + floor(128×b/c) - 512
P2 = 128×b - c×floor(128×b/c)
P3 = c

`

**wspr\_transmit.c L185–L221**

![](data:image/png;base64...)

These are written to Si5351A registers 0x22–0x29 (MSNB) via I²C.

**Tone spacing:** WSPR specifies 12000/8192 = 1.4648 Hz tone spacing. The Si5351A cannot achieve this exactly due to integer register constraints — with c=1,000,000 and N=126 the minimum frequency step is 27,000,000/(1,000,000×126) = 0.214 Hz. The nearest achievable spacing is 7×0.214 = 1.500 Hz (0.35 Hz or ~2.4% error), which is within the decoder's tolerance as verified by the WAV test.

**Transmission loop:**

`c

for (i = 0; i < 162; i++) {
 si5351\_set\_pllb(&tones[symbols[i]]); // write 8 I2C registers
 nanosleep(682,667 µs); // one symbol period
}

`

**wspr\_transmit.c L486–L511**

![](data:image/png;base64...)

Total transmission time: 162 × 0.682667 = 110.6 seconds.

**Timing synchronisation:** The transmitter waits for an even UTC minute boundary before starting (same as the wsprwait script on the receiver side), ensuring the decoder's 114-second recording window captures the full 110.6-second transmission.

**wspr\_transmit.c L307–L347**

![](data:image/png;base64...)

### **6. wsprwait Script (Less In Depth)**

Brief explanation: wsprwait is a bash script that polls date in a loop, waiting until the second counter reaches 0 on an even UTC minute. It then invokes k9an-wsprd which records 114 seconds of PulseAudio audio, runs the WSPR decoder algorithm (FFT-based sync search, Fano convolutional decoder), and prints any decoded messages. The script then loops to wait for the next even minute.

### **7. Results and Measurements**

Include these from screenshots:

**RF loopback test (hardware):** From SplitTerminal.png:

0616 38 -1.7 0.001502 -1 VK2AAR QF56 3
<DecodeFinished>

From Split2.png:

0624 38 -1.8 0.001502 -1 VK2AAR QF56 3
0624 -8 -1.8 0.001599 -1 <...> JV05QV 60
<DecodeFinished>

Note the SNR of **38 dB** is expected for a hardwired loopback — a real over-the-air signal would be much weaker (e.g. the lab beacon VK2APL was received at −7 to −14 dB SNR). The second decode in Split2 (JV05QV) is a spurious decode from noise, which is normal.
