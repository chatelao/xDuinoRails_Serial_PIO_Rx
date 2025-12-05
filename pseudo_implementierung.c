#include "pico/stdlib.h"
#include "pico/platform.h"

// ----------------------------------------------------------------------------
// KONFIGURATION & MAKROS
// ----------------------------------------------------------------------------
#define OVERSAMPLE_RATE 8
#define WHEEL_SIZE      16
#define WHEEL_MASK      0x0F
#define OFFSET_START    4
#define OFFSET_NEXT_BIT 8

// High-Speed Majority Vote Makro (Branchless)
// Input: 3 aufeinanderfolgende Samples (a, b, c)
// Output: Gefilterter Zustand
#define MAJORITY_VOTE(a, b, c) (((a) & (b)) | ((b) & (c)) | ((a) & (c)))

// ----------------------------------------------------------------------------
// DATENSTRUKTUREN
// ----------------------------------------------------------------------------
typedef struct {
    uint8_t current_byte;
    uint8_t bit_counter;
} channel_ctx_t;

// Context Variables (SRAM)
static channel_ctx_t channels[32];
static uint32_t timing_wheel[WHEEL_SIZE];
static uint32_t active_mask = 0;
static uint32_t wheel_pos = 0;
static uint32_t last_voted_input = 0xFFFFFFFF; // Für Flankenerkennung (gefiltert)

// NEU: Speicher für den Pipeline-Übergang zwischen DMA-Blöcken
static uint32_t prev_raw_1 = 0xFFFFFFFF; // Sample t-1
static uint32_t prev_raw_2 = 0xFFFFFFFF; // Sample t-2

// ... (push_fifo und push_error Implementierungen wie gehabt) ...

// ----------------------------------------------------------------------------
// ROBUSTER CORE ALGORITHMUS (Majority Voting Enabled)
// ----------------------------------------------------------------------------

void __not_in_flash_func(process_dma_stream_robust)(uint32_t *dma_buffer, size_t buf_len) {
    
    // Lokale Variablen für Speed (Register caching)
    uint32_t p1 = prev_raw_1;
    uint32_t p2 = prev_raw_2;
    uint32_t voted_input;

    for (size_t i = 0; i < buf_len; i++) {
        // 1. PIPELINE LOAD: Neues Sample holen
        uint32_t current_raw = dma_buffer[i];

        // 2. MAJORITY VOTE FILTER
        // Wir betrachten das Fenster: [t-2], [t-1], [t]
        // Das Ergebnis 'voted_input' ist mathematisch gesehen der Zustand bei t-1
        // aber bereinigt um Glitches.
        voted_input = MAJORITY_VOTE(p2, p1, current_raw);

        // Pipeline weiterschieben
        p2 = p1;
        p1 = current_raw;

        // Ab hier arbeiten wir NUR noch mit dem sauberen 'voted_input'
        // Die Logik ist identisch zum vorherigen Code, aber "sieht" keine Glitches mehr.

        // ------------------------------------------------------------
        // SCHRITT A: Globale Überwachung (Scanner)
        // ------------------------------------------------------------
        
        // Flankenerkennung auf dem GEFILTERTEN Signal
        uint32_t falling_edges = (last_voted_input & ~voted_input);
        uint32_t new_starts = falling_edges & ~active_mask;

        if (new_starts) {
            active_mask |= new_starts;
            
            // Planung: Da 'voted_input' effektiv t-1 entspricht (Latenz von 1 Sample),
            // sind wir zeitlich leicht versetzt. Das ist bei 8x Oversampling egal,
            // erhöht aber die Robustheit.
            uint32_t target = (wheel_pos + OFFSET_START) & WHEEL_MASK;
            timing_wheel[target] |= new_starts;

            uint32_t work_bits = new_starts;
            while (work_bits) {
                int ch = __builtin_ctz(work_bits);
                channels[ch].bit_counter = 0;
                channels[ch].current_byte = 0;
                work_bits &= ~(1u << ch);
            }
        }

        // ------------------------------------------------------------
        // SCHRITT B: Geplante Aufgaben (Worker)
        // ------------------------------------------------------------
        
        uint32_t tasks_todo = timing_wheel[wheel_pos];
        timing_wheel[wheel_pos] = 0;

        while (tasks_todo) {
            int ch = __builtin_ctz(tasks_todo);
            
            // Wir lesen den Pin-Zustand aus dem gefilterten Wort
            uint32_t pin_state = (voted_input >> ch) & 1;
            channel_ctx_t *ctx = &channels[ch];

            if (ctx->bit_counter == 0) {
                // Start-Bit Validierung (doppelt hält besser)
                // Durch Majority Vote ist 'pin_state' hier schon sehr stabil.
                if (pin_state == 0) {
                    ctx->bit_counter = 1;
                    timing_wheel[(wheel_pos + OFFSET_NEXT_BIT) & WHEEL_MASK] |= (1u << ch);
                } else {
                    active_mask &= ~(1u << ch); // Abbruch
                }
            }
            else if (ctx->bit_counter <= 8) {
                if (pin_state) {
                    ctx->current_byte |= (1u << (ctx->bit_counter - 1));
                }
                ctx->bit_counter++;
                timing_wheel[(wheel_pos + OFFSET_NEXT_BIT) & WHEEL_MASK] |= (1u << ch);
            }
            else { // Stop Bit
                if (pin_state == 1) {
                    push_fifo(ch, ctx->current_byte);
                } else {
                    // Framing Error: Trotz Filter immer noch Low? Dann ist es ein echter Fehler.
                    push_error(ch, 0xFE); 
                }
                active_mask &= ~(1u << ch);
            }
            tasks_todo &= ~(1u << ch);
        }

        // ------------------------------------------------------------
        // SCHRITT C: Stepping
        // ------------------------------------------------------------
        last_voted_input = voted_input;
        wheel_pos = (wheel_pos + 1) & WHEEL_MASK;
    }

    // Pipeline-Status für den nächsten Aufruf sichern
    prev_raw_1 = p1;
    prev_raw_2 = p2;
}
