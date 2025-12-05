#include "pico/stdlib.h"
#include "pico/platform.h" // Für __not_in_flash_func

// ----------------------------------------------------------------------------
// KONSTANTEN & DEFINITIONEN
// ----------------------------------------------------------------------------
#define OVERSAMPLE_RATE 8
#define WHEEL_SIZE      16       // 2^4, erlaubt Maskierung mit 0x0F
#define WHEEL_MASK      0x0F
#define OFFSET_START    4
#define OFFSET_NEXT_BIT 8

// ----------------------------------------------------------------------------
// DATENSTRUKTUREN
// ----------------------------------------------------------------------------

typedef struct {
    uint8_t current_byte; // Das Byte, das wir gerade bauen
    uint8_t bit_counter;  // Zähler: 0=Start, 1-8=Daten, 9=Stop
} channel_ctx_t;

// Globale Zustandsvariablen (im SRAM für Speed)
static channel_ctx_t channels[32];
static uint32_t timing_wheel[WHEEL_SIZE]; // Array von 32-Bit Masken
static uint32_t active_mask = 0;          // Bitmaske: Welche Kanäle sind busy?
static uint32_t wheel_pos = 0;            // Aktuelle Position im Rad (TimeIndex)
static uint32_t last_input = 0xFFFFFFFF;  // Annahme: Idle ist High

// Dummy-Funktion für FIFO-Push (muss implementiert werden)
static inline void push_fifo(int ch, uint8_t byte) {
    // Implementierung: Ringbuffer schreiben
}

static inline void push_error(int ch, int error_code) {
    // Implementierung: Error Flag setzen
}

// ----------------------------------------------------------------------------
// HAUPT-ALGORITHMUS (M33 OPTIMIZED)
// ----------------------------------------------------------------------------

// ALGORITHMUS Process_DMA_Stream(InputBuffer):
void __not_in_flash_func(process_dma_stream)(uint32_t *dma_buffer, size_t buf_len) {

    // FÜR JEDES Sample "CurrentInput" in InputBuffer:
    for (size_t i = 0; i < buf_len; i++) {
        uint32_t current_input = dma_buffer[i];

        // ------------------------------------------------------------
        // SCHRITT 1: Globale Überwachung (Der "Scanner")
        // ------------------------------------------------------------

        // Logik: Signal war High (Last) UND ist jetzt Low (Current)
        uint32_t falling_edges = (last_input & ~current_input);

        // Wir interessieren uns nur für Kanäle, die NICHT schon arbeiten
        // NewStartEvents = AllFallingEdges AND NOT ActiveMask
        uint32_t new_starts = falling_edges & ~active_mask;

        // WENN NewStartEvents > 0:
        if (new_starts) {
            // 1. Markiere diese Kanäle als "Beschäftigt"
            // ActiveMask = ActiveMask OR NewStartEvents
            active_mask |= new_starts;

            // 2. Plane die Überprüfung in der Zukunft (Mitte des Startbits)
            // TargetSlot = (TimeIndex + OFFSET_START) MODULO WHEEL_SIZE
            // TimingWheel[TargetSlot] = TimingWheel[TargetSlot] OR NewStartEvents
            uint32_t target = (wheel_pos + OFFSET_START) & WHEEL_MASK;
            timing_wheel[target] |= new_starts;

            // 3. Reset der Channel-Kontexte für diese neuen Kanäle
            // FÜR JEDEN Kanal "Ch" in NewStartEvents:
            uint32_t work_bits = new_starts;
            while (work_bits) {
                // ARM M33 Optimierung: __builtin_ctz nutzt Hardware (RBIT + CLZ)
                int ch = __builtin_ctz(work_bits);
                
                channels[ch].bit_counter = 0;
                channels[ch].current_byte = 0;
                
                // Bit löschen für nächsten Schleifendurchlauf
                work_bits &= ~(1u << ch); 
            }
        }

        // ------------------------------------------------------------
        // SCHRITT 2: Geplante Aufgaben ausführen (Der "Worker")
        // ------------------------------------------------------------

        // Hole die Todo-Liste für jetzt
        // TasksTodo = TimingWheel[TimeIndex]
        uint32_t tasks_todo = timing_wheel[wheel_pos];

        // Slot im Rad leeren (für die nächste Umdrehung)
        // TimingWheel[TimeIndex] = 0
        timing_wheel[wheel_pos] = 0;

        // SOLANGE TasksTodo > 0:
        while (tasks_todo) {
            // Wähle effizient den nächsten Kanal
            // Ch = Finde_Nächstes_Set_Bit(TasksTodo)
            int ch = __builtin_ctz(tasks_todo);

            // PinState = (CurrentInput >> Ch) AND 1
            uint32_t pin_state = (current_input >> ch) & 1;
            channel_ctx_t *ctx = &channels[ch];

            // FALLUNTERSCHEIDUNG Context.BitCounter:
            // (Hier als if/else Block für Performance statt switch)
            
            // FALL 0 (Start-Bit Verifikation):
            if (ctx->bit_counter == 0) {
                // WENN PinState == 0:
                if (pin_state == 0) {
                    // Gültiges Start-Bit (ist immer noch Low)
                    // Context.BitCounter = 1
                    ctx->bit_counter = 1;
                    
                    // Planen(Ch, OFFSET_NEXT_BIT)
                    timing_wheel[(wheel_pos + OFFSET_NEXT_BIT) & WHEEL_MASK] |= (1u << ch);
                } 
                // SONST:
                else {
                    // Fehlalarm (Glitch) - Abbruch!
                    // ActiveMask = ActiveMask AND NOT BitMask(Ch)
                    active_mask &= ~(1u << ch);
                }
            }
            // FALL 1 BIS 8 (Daten-Bits):
            else if (ctx->bit_counter <= 8) {
                // Bit einschieben (LSB First)
                // WENN PinState == 1: Setze Bit
                if (pin_state) {
                    ctx->current_byte |= (1u << (ctx->bit_counter - 1));
                }

                // Context.BitCounter = Context.BitCounter + 1
                ctx->bit_counter++;
                
                // Planen(Ch, OFFSET_NEXT_BIT)
                timing_wheel[(wheel_pos + OFFSET_NEXT_BIT) & WHEEL_MASK] |= (1u << ch);
            }
            // FALL 9 (Stop-Bit):
            else {
                // WENN PinState == 1:
                if (pin_state == 1) {
                    // Gültiges Stop-Bit -> Byte ist fertig!
                    // Push_To_FIFO(Ch, Context.CurrentByte)
                    push_fifo(ch, ctx->current_byte);
                } 
                // SONST:
                else {
                    // Framing Error
                    push_error(ch, 0xFE); 
                }

                // Aufräumen: Kanal freigeben
                // ActiveMask = ActiveMask AND NOT BitMask(Ch)
                active_mask &= ~(1u << ch);
            }

            // Bit aus Todo-Liste entfernen
            // TasksTodo = TasksTodo AND NOT BitMask(Ch)
            tasks_todo &= ~(1u << ch);
        }

        // ------------------------------------------------------------
        // SCHRITT 3: Zeit fortschreiten lassen
        // ------------------------------------------------------------
        
        // LastInput = CurrentInput
        last_input = current_input;
        
        // TimeIndex = (TimeIndex + 1) MODULO WHEEL_SIZE
        wheel_pos = (wheel_pos + 1) & WHEEL_MASK;
    }
}
