# xDuinoRails_Serial_PIO_Rx

Das Projekt soll 32 Railcom Ports sowie 32 Besetztmelde ADC auf einem RP2350 mit 4 CD4051 Analogmultiplexern kombinieren.


# RP2350B (QFN-80) Pinout Definition
**Project:** 32-Channel High-Density UART Receiver & Mixed Signal Controller
**Target:** RP2350B (QFN-80 Package)
**UART Config:** 250 kBaud, 8N1
**Sampling:** 2 MHz (8x Oversampling via PIO)

## 1. Digital Input (32x Soft-UART)
Organized in 4 Groups of 8 Bits for optimized PIO/DMA fetching ("Time-Packing").

| GPIO | Signal Label | PIO Grp | Bit | Description |
| :--- | :--- | :--- | :--- | :--- |
| **00** | `UART_RX_00` | A | 0 | Group A LSB |
| **01** | `UART_RX_01` | A | 1 | |
| **02** | `UART_RX_02` | A | 2 | |
| **03** | `UART_RX_03` | A | 3 | |
| **04** | `UART_RX_04` | A | 4 | |
| **05** | `UART_RX_05` | A | 5 | |
| **06** | `UART_RX_06` | A | 6 | |
| **07** | `UART_RX_07` | A | 7 | Group A MSB |
| **08** | `UART_RX_08` | B | 0 | Group B LSB |
| **09** | `UART_RX_09` | B | 1 | |
| **10** | `UART_RX_10` | B | 2 | |
| **11** | `UART_RX_11` | B | 3 | |
| **12** | `UART_RX_12` | B | 4 | |
| **13** | `UART_RX_13` | B | 5 | |
| **14** | `UART_RX_14` | B | 6 | |
| **15** | `UART_RX_15` | B | 7 | Group B MSB |
| **24** | `UART_RX_16` | C | 0 | Group C LSB |
| **25** | `UART_RX_17` | C | 1 | |
| **26** | `UART_RX_18` | C | 2 | |
| **27** | `UART_RX_19` | C | 3 | |
| **28** | `UART_RX_20` | C | 4 | |
| **29** | `UART_RX_21` | C | 5 | |
| **30** | `UART_RX_22` | C | 6 | |
| **31** | `UART_RX_23` | C | 7 | Group C MSB |
| **32** | `UART_RX_24` | D | 0 | Group D LSB |
| **33** | `UART_RX_25` | D | 1 | |
| **34** | `UART_RX_26` | D | 2 | |
| **35** | `UART_RX_27` | D | 3 | |
| **36** | `UART_RX_28` | D | 4 | |
| **37** | `UART_RX_29` | D | 5 | |
| **38** | `UART_RX_30` | D | 6 | |
| **39** | `UART_RX_31` | D | 7 | Group D MSB |

## 2. Communication & Control
Interfaces for external bus and analog multiplexer control.

| GPIO | Signal Label | Function | Direction | Notes |
| :--- | :--- | :--- | :--- | :--- |
| **16** | `RS485_TX` | UART1 | OUT | High-Speed Uplink |
| **17** | `RS485_RX` | UART1 | IN | High-Speed Uplink |
| **18** | `RS485_DE` | GPIO | OUT | Driver Enable (Active High) |
| **19** | `SYS_LED_ERR`| GPIO | OUT | **SPARE** (Debug/Status) |
| **20** | `MUX_S0` | PIO1 | OUT | Mux Address Bit 0 |
| **21** | `MUX_S1` | PIO1 | OUT | Mux Address Bit 1 |
| **22** | `MUX_S2` | PIO1 | OUT | Mux Address Bit 2 |
| **23** | `MUX_ENABLE` | GPIO | OUT | **SPARE** (Mux Chip Enable) |

## 3. Precision Analog (Isolated)
Located on the far side of the die (Pins 40+) to minimize digital noise coupling.

| GPIO | Signal Label | HW Resource | Notes |
| :--- | :--- | :--- | :--- |
| **40** | `ADC_IN_0` | ADC 4 | Mux Group 1 Output |
| **41** | `ADC_IN_1` | ADC 5 | Mux Group 2 Output |
| **42** | `ADC_IN_2` | ADC 6 | Mux Group 3 Output |
| **43** | `ADC_IN_3` | ADC 7 | Mux Group 4 Output |

## 4. Configuration / Spare
Low-noise area suitable for I2C or static config.

| GPIO | Signal Label | Function | Notes |
| :--- | :--- | :--- | :--- |
| **44** | `CFG_0` | I2C_SDA / GPIO | **SPARE** (Low Noise) |
| **45** | `CFG_1` | I2C_SCL / GPIO | **SPARE** (Low Noise) |
| **46** | `AUX_0` | GPIO | **SPARE** |
| **47** | `AUX_1` | GPIO | **SPARE** |
