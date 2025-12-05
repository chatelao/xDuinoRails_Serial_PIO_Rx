# Implementation Roadmap: RP2350 Massively Parallel IO System

## Phase 1: Hardware Engineering (PCB & Signal Integrity)
*Goal: A low-noise platform capable of handling 32 concurrent asynchronous signals without crosstalk.*

### 1.1 Schematic Capture
- [ ] **Power Domain Separation:** Design strict filtering (Ferrite beads + LDO) for `ADC_AVDD` (3.3V).
- [ ] **Input Protection:** Add series resistors (e.g., 33Ω) for the 32 UART inputs if cable runs are long. Consider TVS arrays for industrial ESD protection.
- [ ] **RS-485:** Select a 3.3V transceiver with high slew rate support (e.g., MAX3485 or TI ISO14xx for isolation).
- [ ] **Mux Logic:** Verify logic levels of Analog Multiplexers (ensure they switch cleanly at 3.3V).

### 1.2 Layout Strategy (4-Layer Stackup)
- [ ] **Stackup Definition:** Signal / GND / Power / Signal.
- [ ] **Zoning:** Implement the "Digital Trench". No fast digital return currents under the ADC section (GPIO 40-43).
- [ ] **Routing Groups:** Route UART Groups A, B, C, D as tight bundles with GND guards between bundles to minimize crosstalk.
- [ ] **Thermal:** Ensure QFN-80 thermal pad is connected to GND plane with via-stitching.

---

## Phase 2: Low-Level Firmware (PIO & DMA Architecture)
*Goal: Move data from pins to SRAM with 0% CPU Load.*

### 2.1 Toolchain Setup
- [ ] Setup Pico SDK 2.x.

### 2.2 PIO Implementation ("The Time-Packer")
- [ ] Write PIO program for **Group Capture**: Read 8 pins $\to$ Shift ISR $\to$ Auto-push after 32 bits (4 samples).
- [ ] Instantiate this program on 4 State Machines (assigned to pin groups A, B, C, D).
- [ ] **Validation:** Verify exact 2 MHz sampling rate with oscilloscope on a debug pin.

### 2.3 DMA Fabric
- [ ] Configure 4 DMA Channels (one per UART Group).
- [ ] **Ring Buffers:** Allocate 4x circular buffers in `SRAM_4/5` (non-striped) to avoid bus contention with Core 0.
- [ ] **ADC DMA:** Setup PIO1 to drive MUX pins and trigger ADC. Setup DMA to read ADC FIFO.

---

## Phase 3: High-Performance DSP
- [ ] Software-defined UART decoding using bit-slicing and vector operations.

### 3.1 The "Delta" Decoder
- [ ] Implement the **Bit-Slicing Loop**:
    - Load 32-bit word (contains 4 time-steps for 8 channels).
    - XOR with previous state to find edges (`changed_bits`).
    - Use `ctz` (Count Trailing Zeros) to iterate only active channels.
- [ ] **State Machine Array:** Create a `struct` array for 32 UART contexts (State, Bit-Counter, Shift-Register).
- [ ] **Oversampling Logic:** Implement majority voting or center-sampling (sample at index 3, 4, 5 of the 8x oversample).

### 3.2 Performance Optimization
- [ ] **Instruction Trace:** Analyze the decode loop assembly. Ensure compiler uses RISC-V `Zbb` (Bit manipulation) instructions.
- [ ] **Tightly Coupled Memory:** Move the decode code to RAM (using `__not_in_flash_func`) to execute from SRAM, avoiding XIP cache misses.

---

## Phase 4: Application Logic
- [ ] External communciation

### 4.1 IPC (Inter-Processor Communication)
- [ ] Create thread-safe queues (FIFO)
- [ ] Implement flow control: If queues are full, drop packets or signal error LED (GPIO 19).

### 4.2 High-Speed Uplink
- [ ] Implement UART1 driver for RS-485 (GPIO 16-18).
- [ ] Protocol Handler: Packetize the 32 incoming streams into a coherent upstream format (e.g., `[Channel_ID][Data]`).

### 4.3 System Supervisor
- [ ] Monitor CPU loads (Core 1 Idle time?).
- [ ] Implement Watchdog Timer.

---

## Phase 5: Testing & Validation

- [ ] **Loopback Test:** Connect GPIO 16 (TX) to one of the inputs (Group A). Send patterns and verify receipt.
- [ ] **Stress Test:** Feed 32 asynchronous noise sources + valid signals.
- [ ] **Jitter Analysis:** Reduce baud rate divisor until bit errors occur to find safety margins.
