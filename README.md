# WSPR Transceiver — ELEC3607 Assignment 1

Group assignment modifying the WSPR-SDR platform to add a transmit path and redesigning the PCB as a full transceiver.

## Overview

**Part A — Software Transmitter**  
Drives the Si5351's CLK2 output to generate a WSPR-encoded signal, looped back to the receiver input at RF. A C program encodes callsign, Maidenhead locator, and power level into the WSPR protocol and controls CLK2 via I²C.

**Part B — PCB Redesign**  
Modifies the WSPR-SDR KiCad layout to add a 0.1 W PA stage and GPIO-controlled T/R switch sharing the existing bandpass filter. Components from element14 AU. JLCPCB 2-layer spec.

## Repository Structure

```
wspr-transceiver/
├── software/               # Part A — C transmitter
│   ├── wspr_transmit.c
│   ├── wspr_encode.c/h
│   └── Makefile
├── hardware/               # Part B — KiCad project
│   ├── bbb/
│     ├── bbb.kicad_pro     # Kicad project
│     ├── bbb.kicad_sch     # schematic
│     ├── bbb.kicad_pcb     # pcb layout
│     ├── Kicad_libs        # contains symbols and footprints of new components
│     ├── *.gbr             # Gerbers
│     ├── *.drl             # drill files
│     ├── *.net             # netlist
│     └── *.csv             # BOM
├── report/                 # IEEE-format PDF report
└── README.md
```

## Part A — Build & Run

```bash
cd software && make
./wspr_transmit VK2AAX QF56 3
# args: <callsign> <grid_locator> <power_dBm>
```

Targets `/dev/i2c-3`, address `0x60` (Si5351). Fit loopback jumper before running.

## Part B — KiCad

Open `hardware/bbb/bbb.kicad_pro` in KiCad 8.x. Upstream reference: [phwl/elec3607-lab pcb_2026](https://github.com/phwl/elec3607-lab/tree/main/pcb_2026).

## Hardware Target

- BeagleBone / PetaLinux
- Si5351: CLK0/CLK1 → Tayloe detector, CLK2 → TX
- I²C: `/dev/i2c-3`, `0x60`

## Team

University of Sydney — ELEC3607, Semester 1 2026.
