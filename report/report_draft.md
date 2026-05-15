> DRAFT — compile into ELEC3607_A1_ieee.tex using IEEEtran class
> Figures: PART_A/PartA_Pictures/ — reference paths listed per figure

---

# WSPR-SDR Modifications: RF Loopback Transmitter and Shared-BPF Transceiver Design

**[Author 1], [Author 2]**
School of Electrical and Information Engineering, University of Sydney

---

## Abstract

This paper describes two modifications to a WSPR software-defined radio (SDR) receiver
built around the AUP-ZU3 FPGA platform. The first modification (Part A) implements a
4-FSK WSPR transmitter using the Si5351A CLK2 output routed through a 40 dB attenuator
chain directly into the receiver front end, demonstrating a complete transmit–receive
loopback at 7.040 MHz. The WSPR message VK2AAR QF56 3 dBm was encoded and transmitted
as 162 4-FSK symbols over 110.6 seconds, and decoded by the on-board k9an-wsprd
software at SNR = 38 dB. The second modification (Part B) designs a hardware transceiver
path: a BS170 MOSFET common-source amplifier with a resonant LC tank to deliver
100 mW into 50 Ω at 7 MHz, and a TI TS5A23159 dual-channel analog switch that
re-uses the existing 7 MHz Chebyshev bandpass filter for both receive and transmit
paths under GPIO control.

**Keywords** — WSPR, SDR, Si5351A, 4-FSK, convolutional encoding, RF loopback,
BS170, common-source amplifier, T/R switch, bandpass filter, transceiver

---

## I. Introduction

The WSPR-SDR is a 7 MHz weak-signal receiver designed to decode Weak Signal
Propagation Reporter (WSPR) transmissions — a mode using 4-FSK at 1.4648 Hz tone
spacing, capable of decoding signals as weak as −28 dB SNR. The board interfaces with
an Avnet UltraZed-EG (AUP-ZU3) FPGA carrier card running PetaLinux.

**Receive signal path.** The complete receive chain proceeds as follows. An RF signal
enters the 50 Ω SMA port (BPFIN) and passes through a 7-element Chebyshev bandpass
filter centred at 7 MHz, formed by inductors L1, L2, L3 and associated capacitors.
The filtered signal feeds a Tayloe mixer (TI TS3A5017 quad switch), which is driven
by two quadrature clocks — CLK0 and CLK1 from a Silicon Labs Si5351A clock
synthesiser on PLL\_A at 7.038600 MHz, 90° apart — to produce in-phase (I) and
quadrature (Q) baseband audio signals. A TL972 low-noise audio amplifier buffers the
IQ outputs (IOUT/QOUT). The amplified audio is captured by a Plugable USB sound card
at 48 kHz and piped via PulseAudio to the k9an-wsprd software decoder on the AUP-ZU3.

The Si5351A also provides a third output, CLK2, on a separate PLL (PLL\_B). In the
unmodified receiver CLK2 is unused. Part A re-purposes CLK2 as a software-controlled
WSPR transmitter. Part B designs dedicated hardware — a power amplifier and T/R
switching network — to upgrade the board to a full transceiver.

---

## II. Part A: WSPR RF Loopback Transmitter

### A. Hardware Modification

The CLK2 output (Si5351A, PLL\_B) produces a 3.3 V LVCMOS square wave at
approximately 7.040 MHz. Its equivalent output power into 50 Ω is ≈ 0 dBm (1 mW).
The WSPR-SDR receiver front end is designed for over-the-air signals in the
−100 to −70 dBm range; directly connecting CLK2 to the BPF input would saturate
the mixer and amplifier by approximately 70–100 dB. Attenuation is therefore required.

Two SMA barrel attenuators (ATT1 = 20 dB, ATT2 = 20 dB) were connected in series
between the CLK2 test point (TP6) and the BPFIN SMA connector using short coaxial
patch cables and alligator-clip jumpers to the WSPR-SDR board, as shown in Fig. 1
and Fig. 2. The total attenuation is 40 dB:

$$P_{BPFIN} = P_{CLK2} - 40\,\mathrm{dB} = 0\,\mathrm{dBm} - 40\,\mathrm{dB} = -40\,\mathrm{dBm}$$

This places the injected signal at −40 dBm (100 nW), well within the linear range of
the front end while still providing a high-SNR loopback for verification.

> **Fig. 1** — `PART_A/PartA_Pictures/SimpleschematicPartA.png`
> Caption: RF loopback schematic. CLK2 (TP6, ≈ 0 dBm) drives two cascaded 20 dB
> SMA attenuators (ATT1, ATT2) for 40 dB total, injecting −40 dBm into the BPFIN port.

> **Fig. 2** — `PART_A/PartA_Pictures/IMG_0655.jpeg` or `IMG_0656.jpeg`
> Caption: Modified hardware setup. The AUP-ZU3 (top) connects to the WSPR-SDR
> daughter board (bottom right). The 40 dB SMA attenuator chain is visible at left,
> bridging CLK2 TP6 to the BPFIN SMA connector.

### B. WSPR Encoder — wspr\_encode.c

The WSPR protocol encodes a callsign, 4-character Maidenhead grid locator, and
transmit power level into 162 4-FSK symbols through a five-stage pipeline.

**Stage 1 — Callsign packing (28 bits).** The callsign is normalised to six
characters and encoded as a mixed-radix integer:

$$N = c_0 \cdot 36 \cdot 10 \cdot 27^3 + c_1 \cdot 10 \cdot 27^3 + c_2 \cdot 27^3
+ (c_3{-}10) \cdot 27^2 + (c_4{-}10) \cdot 27 + (c_5{-}10)$$

where $c_0 \in [0,36]$ (alphanumeric + space), $c_1 \in [0,35]$, $c_2 \in [0,9]$
(digit only), and $c_{3\text{–}5} \in [10,36]$ (alpha only). *(wspr\_encode.c L57–L92)*

**Stage 2 — Grid and power packing (22 bits).** The Maidenhead locator and power
level are combined:

$$M = (179 - 10(l_0{-}\texttt{'A'}) - (l_2{-}\texttt{'0'})) \cdot 180
    + 10(l_1{-}\texttt{'A'}) + (l_3{-}\texttt{'0'})$$
$$M \leftarrow M \cdot 128 + P_{dBm} + 64$$

*(wspr\_encode.c L98–L109)*

**Stage 3 — 50-bit packing.** $N$ (28 bits) and $M$ (22 bits) are concatenated
into a 50-bit value and zero-padded to 81 bits for the convolutional encoder input.
*(wspr\_encode.c L115–L123)*

**Stage 4 — Convolutional FEC (rate ½, K=32).** Each input bit is shifted into a
32-bit register; two parity bits are generated using generator polynomials
`0xF2D05351` and `0xE4613C47`, producing 162 coded bits. The rate-½ code provides
the ≤ −28 dB SNR decoding floor. *(wspr\_encode.c L129–L146)*

**Stage 5 — Interleaving and sync.** The 162 coded bits are permuted by an 8-bit
bit-reversal interleaver to spread burst errors, then combined with the 162-bit
pseudo-random sync vector:

$$\texttt{symbol}[i] = \texttt{SYNC\_VECTOR}[i] + 2 \times \texttt{interleaved}[i]$$

This yields 162 symbols $\in \{0,1,2,3\}$ — the 4-FSK alphabet.
*(wspr\_encode.c L179–L203)*

Encoder correctness was verified by comparing the output for `VK2AAX QF56 37`
against a reference Python encoder — all 162 symbols matched exactly.

### C. Transmitter — wspr\_transmit.c

CLK2 uses PLL\_B independently, ensuring frequency changes do not disturb the
receiver LO on PLL\_A. The clock frequency plan is:

| Clock | PLL | Frequency | Role |
|-------|-----|-----------|------|
| CLK0 | PLL\_A | 7.038600 MHz | Rx LO (I) |
| CLK1 | PLL\_A | 7.038600 MHz, 90° | Rx LO (Q) |
| CLK2 | PLL\_B | 7.040100–7.040104 MHz | Tx |

**Frequency offset rationale.** If CLK2 were set to the same frequency as the LO
(7.0386 MHz), the mixer would produce a DC output. The transmitter must be offset so
that the mixer produces an audio tone at 1500 Hz — the centre of the k9an-wsprd
decoder search window:

$$f_{audio} = f_{TX} - f_{LO} = 7{,}040{,}100 - 7{,}038{,}600 = 1{,}500\,\mathrm{Hz}$$

**PLL\_B register calculation (AN619).** For each of the four tone frequencies
$f_k$, the VCO frequency is $f_{VCO} = f_k \times N$ where $N = 126$ (MS2 integer
divider). The PLL\_B feedback multiplier $M = f_{VCO}/f_{xtal}$ is decomposed with
denominator $c = 1{,}000{,}000$:

```c
a = floor(M);
b = round((M - a) * c);
P1 = 128*a + floor(128*b/c) - 512;
P2 = 128*b - c*floor(128*b/c);
P3 = c;
```

These are written to Si5351A registers `0x22`–`0x29` (MSNB) via I²C.
*(wspr\_transmit.c L185–L221)*

The four tone frequencies and their achieved values are shown in Table I.

**Table I — Tone Frequencies (MS2\_div=126, PLL\_B denom=1,000,000)**

| Tone | Target (Hz) | Actual (Hz) | Error (Hz) |
|------|-------------|-------------|------------|
| 0 | 7,040,100.000 | 7,040,100.000 | +0.0000 |
| 1 | 7,040,101.465 | 7,040,101.500 | +0.0352 |
| 2 | 7,040,102.930 | 7,040,103.000 | +0.0703 |
| 3 | 7,040,104.395 | 7,040,104.500 | +0.1055 |

The WSPR specification requires 1.4648 Hz tone spacing ($= 12000/8192$ Hz).
With the Si5351A's minimum frequency step of
$\Delta f_{min} = 27{,}000{,}000 / (1{,}000{,}000 \times 126) = 0.214\,\mathrm{Hz}$,
the nearest achievable spacing is $7 \times 0.214 = 1.500\,\mathrm{Hz}$ — a 2.4%
error (0.035 Hz per step), verified to be within the decoder's tolerance.

**Transmission loop.** For each of the 162 symbols, the Si5351A PLL\_B is
reconfigured to the corresponding tone frequency by writing 8 I²C registers, then
the CPU sleeps for one symbol period (682.667 ms). Total transmission time:
$162 \times 682.667\,\mathrm{ms} = 110.6\,\mathrm{s}$.

The transmitter waits for an even UTC minute boundary before starting, synchronised
with the wsprwait decoder script, to ensure the 114-second recording window captures
the full 110.6-second transmission. *(wspr\_transmit.c L307–L347, L486–L511)*

### D. Software Setup

The following commands were run in sequence to bring up the complete
transmit–receive system:

```bash
# Terminal 2 — configure Si5351A (CLK0/CLK1 for Rx LO; CLK2 disabled)
cd si5351/ && sudo ./i2cread

# Terminal 2 — set USB sound card as PulseAudio source; prevent card suspend
pactl set-default-source alsa_input.usb-Plugable_Plugable_USB_Audio_Device_000000000000-00.analog-stereo
parec --device=alsa_input.usb-Plugable_Plugable_USB_Audio_Device_000000000000-00.analog-stereo > /dev/null &

# Terminal 2 — start decoder (before transmitting; syncs to next even UTC minute)
./wsprwait

# Terminal 1 — transmit (simultaneously with decoder)
sudo ./wspr_transmit VK2AAR QF56 3
```

`parec` is required to keep PulseAudio from suspending the sound card during silence
between decodes; without it, the decoder records silence.

### E. Results

Two successful decode runs are presented. In both cases the message `VK2AAR QF56 3`
was recovered.

> **Fig. 3** — `PART_A/PartA_Pictures/SplitTerminal.png`
> Caption: First decode (06:16:00 UTC). Terminal 1 (left): transmitter output showing
> 162 symbols, tone frequencies, and "Transmission complete." Terminal 2 (right):
> decoder output showing successful decode at SNR = 38 dB.

> **Fig. 4** — `PART_A/PartA_Pictures/Split2.png`
> Caption: Second decode (06:24:00 UTC). SNR = 38 dB, frequency offset −1.8 ppm.
> A second spurious decode (JV05QV 60) appears from noise — normal behaviour.

**Table II — Loopback Decode Results**

| UTC Slot | SNR (dB) | Freq. Offset (ppm) | Audio Freq. (MHz) | Decoded Message |
|----------|----------|--------------------|-------------------|-----------------|
| 06:16 | 38 | −1.7 | 0.001502 | VK2AAR QF56 3 |
| 06:24 | 38 | −1.8 | 0.001502 | VK2AAR QF56 3 |

The 38 dB SNR is expected for a direct RF connection; over-the-air signals from the
lab beacon VK2APL were received at −7 to −14 dB SNR. The decoded audio frequency
(1502 Hz) confirms the 1500 Hz mixer offset design. The spurious decode in the second
run (JV05QV 60) is a statistical artifact of the decoder's search algorithm and does
not indicate an error.

---

## III. Part B: Transmit Amplifier and Shared-BPF Transceiver Path

### A. Design Objectives

Part B converts the receiver into a hardware transceiver by adding two circuits:

1. A **transmit amplifier** accepting CLK2 (0–3.3 V, ~7 MHz square wave) and
   delivering 100 mW into a 50 Ω SMA load.
2. A **T/R switching network** that routes the existing 7 MHz BPF into either the
   Rx or Tx signal path depending on a GPIO control signal, without requiring a
   second filter.

### B. Transmit Amplifier

**Power and voltage specification.** The required output swing into 50 Ω:

$$V_{pk} = \sqrt{2 P_{out} R_L} = \sqrt{2 \times 0.1 \times 50} \approx 3.16\,\mathrm{V}$$
$$V_{pp} = 2 V_{pk} \approx 6.3\,\mathrm{V}$$

A 3.3 V supply cannot produce a 6.3 V$_{pp}$ swing without a step-up network. The
5 V rail from the AUP-ZU3 is used with a resonant LC tank as the load to achieve
the required voltage boost.

**Circuit design.** A BS170 N-channel MOSFET in common-source configuration performs
the switching. CLK2 drives the gate directly: the device turns fully on when
$V_{GS} = 3.3\,\mathrm{V}$ (above $V_{GS(th)} \approx 2\,\mathrm{V}$) and fully off
at $V_{GS} = 0\,\mathrm{V}$. The drain network (L1, L2 in series with C1 in
parallel) resonates at 7 MHz:

$$f_0 = \frac{1}{2\pi\sqrt{L_{eq}\,C_1}}, \quad L_{eq} = L_1 + L_2$$

The resonant tank converts the pulsed drain current into a near-sinusoidal voltage
at the fundamental frequency, boosting the output amplitude to approximately 6.3 V$_{pp}$
across the 50 Ω load. A 100 nF decoupling capacitor (C5) between the 5 V supply and
ground reduces supply-rail noise without affecting the RF signal path.

> **Fig. 5** — *[extract from Part\_B\_explanation.docx: BS170 amplifier schematic]*
> Caption: Tx amplifier schematic. BS170 common-source stage with resonant drain
> tank L1, L2, C1 tuned to 7 MHz. Load is the BWSMA-KE-P001 SMA connector (50 Ω
> inbuilt impedance). C5 = 100 nF supply decoupling.

### C. T/R Switching Network

**Device selection.** The TI TS5A23159 is a dual-channel single-pole double-throw
(SPDT) analog switch — functionally a 2×(1P2T) configuration. Each channel
has a common pin (COM), a normally-closed contact (NC, connected when IN = 0) and a
normally-open contact (NO, connected when IN = 1). Both channels share a single
control input (IN1/IN2 wired together, driven by GPIO23 on the AUP-ZU3).

**Switching logic.** The BPF is placed between COM1 and COM2. The antenna SMA
connects to NC1; the Rx chain (LNA input) connects to NC2; the Tx amplifier output
connects to NO1; and the Tx output to the antenna connects through NO2.

*Receive mode (GPIO23 = 0):*

$$\text{Antenna} \xrightarrow{\text{NC1}\to\text{COM1}} \text{BPF IN}
  \to \text{BPF OUT} \xrightarrow{\text{COM2}\to\text{NC2}} \text{RF IN (LNA)}$$

The NO1–COM1 path is open, so Tx amplifier output is isolated from the antenna.

*Transmit mode (GPIO23 = 1):*

$$\text{TX RF} \xrightarrow{\text{NO1}\to\text{COM1}} \text{BPF IN}
  \to \text{BPF OUT} \xrightarrow{\text{COM2}\to\text{NO2}} \text{Antenna}$$

The NC1–COM1 path is open, so the antenna is isolated from the LNA input.

> **Fig. 6** — *[extract from Part\_B\_explanation.docx: TS5A23159 functional diagram]*
> Caption: TS5A23159 2-channel analog switch functional diagram.

> **Fig. 7** — *[extract from Part\_B\_explanation.docx: full T/R switch schematic]*
> Caption: Full schematic of the T/R switching network. GPIO23 = 0 selects Rx path;
> GPIO23 = 1 selects Tx path. The existing 7 MHz BPF is shared between both paths.

### D. KiCad Implementation

The Tx amplifier and T/R switch were implemented in KiCad 6.0 using the existing
`bbb.kicad_sch` as a base schematic. Component symbols and PCB footprints were
sourced from Element14 and imported into a project-specific library. An Electrical
Rules Check (ERC) was run and a bill of materials (BOM) was generated to confirm
component selection.

In the PCB editor, component footprints were imported from the schematic netlist and
placed manually, then connections were completed using a combination of manual and
autorouting. A Design Rules Check (DRC) confirmed all connections were clean; Gerber
and drill files were exported for potential fabrication.

> **Fig. 8** — *[extract from Part\_B\_explanation.docx: KiCad PCB layout screenshot]*
> Caption: KiCad PCB layout showing the Tx amplifier and T/R switch added to the
> existing WSPR-SDR board. New components are annotated.

**Design limitations.** Routing was not fully optimal — net lengths could be reduced
and via count lowered to minimise parasitic inductance at 7 MHz. The BS170 is a
through-hole package; an SMD equivalent exists but its symbol and footprint were not
available in the Element14 library at the time of design.

---

## IV. Conclusion

Two modifications to the WSPR-SDR receiver were implemented and tested. Part A
demonstrated a complete WSPR transmit–receive loopback: CLK2 was attenuated 40 dB
and injected into the BPF input, and the WSPR message VK2AAR QF56 3 was decoded in
two consecutive runs with SNR = 38 dB and a frequency offset of −1.7 to −1.8 ppm,
confirming correct encoder, frequency synthesis, and synchronisation operation.
Part B designed the hardware transceiver path — a BS170 common-source amplifier
targeting 6.3 V$_{pp}$ (100 mW) at 7 MHz and a TS5A23159 dual analog switch
enabling the existing BPF to serve both Rx and Tx paths under a single GPIO control
signal. The KiCad schematic and PCB layout are ready for fabrication, with the
identified improvement of migrating the BS170 footprint to an SMD package.

---

## References

[1] Silicon Laboratories, "Si5351A/B/C-B I²C Clock Generator Datasheet," Rev. 1.3,
    2018. [Online]. Available: https://www.silabs.com/documents/public/data-sheets/Si5351-B.pdf

[2] Silicon Laboratories, "AN619: Manually Generating an Si5351 Register Map,"
    Application Note, Rev. 0.3, 2016.

[3] Texas Instruments, "TS5A23159 Dual-Channel 1-Ω SPDT Analog Switch Datasheet,"
    SCDS139C, 2004.

[4] ON Semiconductor, "BS170 N-Channel Enhancement Mode Field Effect Transistor
    Datasheet," Rev. 4, 2014.

[5] K. Fleisher (k9an), "wsprd WSPR Decoder," [Software]. Available:
    https://github.com/k9an/wsprd

[6] J. Ahlstrom, "An All-Digital Transceiver for HF," *QEX*, pp. 1–6, Nov./Dec. 2010.

---

## Appendix A: wspr\_encode.c — Key Functions

```c
// Stage 4 — convolutional FEC (rate 1/2, K=32)
// Generator polynomials: 0xF2D05351, 0xE4613C47
for (i = 0; i < 81; i++) {
    reg = (reg << 1) | input_bit[i];
    p1 = parity32(reg & 0xF2D05351);
    p2 = parity32(reg & 0xE4613C47);
    coded[2*i]   = p1;
    coded[2*i+1] = p2;
}

// Stage 5 — interleaving + sync merge
for (i = 0; i < 162; i++) {
    symbol[i] = SYNC_VECTOR[i] + 2 * interleaved[i];
}
```

*(Full listing: PART\_A/CODE.zip — wspr\_encode.c)*

## Appendix B: wspr\_transmit.c — PLL\_B Register Calculation

```c
// AN619 register mapping for PLL_B (c = 1,000,000)
a = (int)floor(M);
b = (int)round((M - a) * (double)c);
P1 = 128*a + (int)floor(128.0*b/c) - 512;
P2 = 128*b - c * (int)floor(128.0*b/c);
P3 = c;
// Write P1, P2, P3 to Si5351A registers 0x22–0x29 via I2C
```

*(Full listing: PART\_A/CODE.zip — wspr\_transmit.c)*

## Appendix C: Figures Requiring Extraction from Part\_B Docx

The following schematics are embedded in `PART_A/PART_B_explanation.docx` as
inline images. They must be exported and placed in a `figs/` directory before
compiling the LaTeX report:

| Figure | Description |
|--------|-------------|
| Fig. 5 | BS170 Tx amplifier schematic with component values |
| Fig. 6 | TS5A23159 functional diagram |
| Fig. 7 | Full T/R switch schematic |
| Fig. 8 | KiCad PCB layout screenshot |

To export: open `Part_B_explanation.docx` in Word/LibreOffice → right-click each
image → Save as Picture → save to `PART_A/PartA_Pictures/` or `figs/`.
