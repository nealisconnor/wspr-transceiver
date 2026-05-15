Part B’s aim is to convert the receiver into a transceiver. It accomplishes two main tasks –

1. The creation of a Tx amplifier that can take the CLK2 signal as an input and drive 100mW into a load with 50 ohm impedance
2. The creation of a circuit that re-uses the BPF that is already on the board for both the receiver (Rx) and the transmitter (Tx) paths

**The Tx amplifier**

![](data:image/png;base64...)

The SMA (BWSMA-KE-P001) has an inbuilt 50 ohm impedance. The amplifier circuit was built using a BS170 MOSFET used in common source configuration . The output voltage is -

![](data:image/png;base64...)

The 3.3V source can’t effectively produce a linear swing of 6.3Vpp at the 50 ohm impedance without some impedance transformation or resonant boosting. So, we use a 5V supply (5V specifically is used since we can get this voltage from the AUP-ZU3 board)

What happens in the amplifier circuit?

The input CLK2 is a square wave with a frequency near 7MHz oscillating between 0 and 3.3V. The square wave drives the gate turning the MOSFET ON and OFF. When ON, current flows through the inductors L1, L2 and C1. The network L1,L2 and C1 is tuned to 7MHz.

![](data:image/png;base64...)

The decoupling capacitor C5 = 100nF doesn’t play a part in amplification. It’s role is to reduce noise between the 5V supply and GND.

The square CLK signal is converted into a sinusoidal signal Tx-RF and the output voltage is magnified to around 6.3Vpp.

![](data:image/png;base64...)

**The Analog switch**

We use an analog switch to control the Tx and Rx signal flow paths through the BPF depending on whether the GPIO pin sets the circuit to transmit mode or receive mode.

![](data:image/png;base64...)Functional diagram of the TI TS5A23159 2-channel Analog Switch -

![](data:image/png;base64...)

When IN1 = 0, COM1 = NC1 When IN1 = 1, COM1 = NO1

When IN2 = 0, COM2 = NC2 When IN2 = 1, COM2 = NO2

We use this switching behaviour to create the TX to SMA path (with BPF in between) and SMA to RX (with BPF in between)

![](data:image/png;base64...)

When GPIO23 = 0, we are in Rx mode. The RF signal will travel from the SMA connector via the NC1 pin to the COM1 pin. Then it enters BPF IN. The filtered output BPF OUT will travel to COM2 and then to NC2. NC2 is connected to the rest of the Rx circuit via RF IN. Note that TX RF does not interfere with the rest of the circuit since the NO1 to COM1 path is open at this time.

When GPIO23 = 1, we are in Tx mode, The RF signal (after amplification) (TX RF) will enter the switch via NO1, travel to COM1, through the BPF to COM2 and then to NO2 and then to the SMA connector. Note that this transmitted signal does not interfere with the rest of the circuit since the SMA to NC1 to COM1 path is open at this time.

Creating the schematic in KiCad –

![](data:image/png;base64...)

The Tx Amplifier and Tx/Rx switch circuits were built in the KiCad schematic editor using components from Element14. I ensured that all elements used in the ciruit had symbols and footprints available for download. The symbol libraries were downloaded into a project specific library in Kicad 6.0 format.

The existing bbb.kicad\_sch was imported and used as a starting point.Then the new components were placed and wired. ERC (Electrical Rules Check) was run and a netlist was generated manually to compare the new connections and confirm their correctness.

Then the title block was updated and BOM (Bill of Materials) file was generated

**Placing components in the PCB editor –**

**![](data:image/png;base64...)**

The components footprint libraries were imported into the pcb editor and the pcb layout was updated from the schematic.

The connections between components were made using a combination of manual and auto routing .

A DRC (Design Rules Check) was run to ensure all connections were clean, then the gerber and drill files were exported (these are sent through to the manufacturer if the pcb is to be manufactured).

What I would do to improve this design –

* More oprtimal routing (shorter nets and fewer vias)
* Using only SMD components (the BS170 is a throughhole component – there is an SMD version but the symbol and footprint libraries weren’t available on element14)
