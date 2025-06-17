/**
 * gpu.h
 * Header file for the PlayStation GPU emulation state and functions.
 */
#ifndef GPU_H
#define GPU_H

#include <stdint.h>
#include <stdbool.h>
#include "renderer.h" // Includes OpenGL renderer definitions
#include "vram.h"     // Includes VRAM definitions

// --- GPU Data Types & Enums ---

// Texture Color Depth (from STAT[8:7])
typedef enum {
    T4Bit = 0,  // 4 bits per pixel (uses CLUT)
    T8Bit = 1,  // 8 bits per pixel (uses CLUT)
    T15Bit = 2  // 15 bits BGR (Direct color)
    // 3 is reserved/invalid
} TextureDepth;

// Field type for interlaced video output (from STAT[13])
typedef enum {
    Bottom = 0, // Bottom field (even lines) or Progressive frame
    Top = 1     // Top field (odd lines)
} Field;

// Horizontal Resolution helper (from STAT[18:16])
typedef struct {
    uint8_t hr1; // Bits 17:16
    uint8_t hr2; // Bit 18
} HorizontalResRaw;

// Vertical Resolution (from STAT[19])
typedef enum {
    Y240Lines = 0, // 240 lines (NTSC Progressive / PAL Progressive)
    Y480Lines = 1  // 480 lines (NTSC Interlaced)
} VerticalRes;

// Video Mode (NTSC/PAL) (from STAT[20])
typedef enum {
    Ntsc = 0, // NTSC (60Hz)
    Pal = 1   // PAL (50Hz)
} VMode;

// Display Area Color Depth (from STAT[21])
typedef enum {
    D15Bits = 0, // 15 bits BGR
    D24Bits = 1  // 24 bits RGB (Requires specific setup/MDEC?)
} DisplayDepth;

// DMA Direction/Mode setting (from STAT[30:29])
typedef enum {
    GPU_DMA_Off = 0,      // DMA disabled/finished
    GPU_DMA_Fifo = 1,     // FIFO mode (GP0 via DMA?)
    GPU_DMA_CpuToGp0 = 2, // CPU writes are forwarded to GP0 (?)
    GPU_DMA_VRamToCpu = 3 // GPUREAD reads data from VRAM transfer
} GpuDmaSetting;

// GP0 Port Mode (internal state)
typedef enum {
    GP0_MODE_COMMAND,   // Expecting GP0 command words
    GP0_MODE_IMAGE_LOAD // Expecting pixel data words for VRAM transfer
} Gp0Mode;

// GP0 Command Buffer
#define MAX_GPU_COMMAND_WORDS 16 // Max parameters for any single command + opcode word
typedef struct {
    uint32_t buffer[MAX_GPU_COMMAND_WORDS]; // Stores words for current command
    uint8_t count;                          // Number of words currently in buffer
} CommandBuffer;


// --- Main Gpu State Structure ---
typedef struct Gpu Gpu; // Forward declaration for function pointer type

typedef struct Gpu { // Define struct Gpu
    // --- GPUSTAT Fields & Related State ---
    uint8_t page_base_x;            // STAT[3:0]   - Texture page base X (4 bits, 64-pixel steps)
    uint8_t page_base_y;            // STAT[4]     - Texture page base Y (1 bit, 256-line steps)
    uint8_t semi_transparency;      // STAT[6:5]   - Semi-transparency mode (0=B/2+F/2, 1=B+F, 2=B-F, 3=B+F/4)
    TextureDepth texture_depth;     // STAT[8:7]   - Texture page color depth
    bool dithering;                 // STAT[9]     - Enable dithering from 24-bit to 15-bit
    bool draw_to_display;           // STAT[10]    - Allow drawing to the active display area
    bool force_set_mask_bit;        // STAT[11]    - Force mask bit to 1 when writing to VRAM (GP0(E6))
    bool preserve_masked_pixels;    // STAT[12]    - Prevent drawing to pixels where mask bit is set (GP0(E6))
    Field field;                    // STAT[13]    - Current video output field (Top/Bottom/Progressive) (Updated by timing)
    // Bit 14: "Reverseflag" - Not usually set/emulated
    bool texture_disable;           // STAT[15]    - Disable all texturing when rendering (GP0(E1))

    // ***** THESE WERE MISSING / INCORRECTLY ADDED BEFORE *****
    bool rectangle_texture_x_flip;  // STAT[?] / GP0(E1)[12] - Mirror textured rects horizontally
    bool rectangle_texture_y_flip;  // STAT[?] / GP0(E1)[13] - Mirror textured rects vertically
    // **********************************************************

    HorizontalResRaw hres_raw;      // STAT[18:16] - Raw horizontal resolution bits (GP1(08))
    VerticalRes vres;               // STAT[19]    - Vertical resolution (GP1(08))
    VMode vmode;                    // STAT[20]    - Video mode (NTSC/PAL) (GP1(08))
    DisplayDepth display_depth;     // STAT[21]    - Display area color depth (GP1(08))
    bool interlaced;                // STAT[22]    - Enable interlaced video output (GP1(08))
    bool display_disabled;          // STAT[23]    - Disable video output signal (GP1(03))
    bool interrupt;                 // STAT[24]    - GPU Interrupt Request (IRQ) flag (Set by VBlank?, cleared by GP1(02))
    // Bits 25-28 are Ready flags (GPUREAD Ready, VRAM->CPU Ready, CMD Ready, DMA Ready) - Hardcoded for now
    GpuDmaSetting dma_setting;      // STAT[30:29] - DMA request direction/mode (GP1(04))
    // Bit 31: Odd/Even line signal (depends on timing)

    // --- Texture Window State (GP0(E2)) ---
    uint8_t texture_window_x_mask;   // Bits 0-4    - Texture Window X Mask (8 pixel steps)
    uint8_t texture_window_y_mask;   // Bits 5-9    - Texture Window Y Mask (8 pixel steps)
    uint8_t texture_window_x_offset; // Bits 10-14  - Texture Window X Offset (8 pixel steps)
    uint8_t texture_window_y_offset; // Bits 15-19  - Texture Window Y Offset (8 pixel steps)

    // --- Drawing Area & Offset State (GP0(E3), GP0(E4), GP0(E5)) ---
    uint16_t drawing_area_left;      // Bits 0-9    - Left boundary of drawing area
    uint16_t drawing_area_top;       // Bits 10-19  - Top boundary of drawing area
    uint16_t drawing_area_right;     // Bits 0-9    - Right boundary of drawing area
    uint16_t drawing_area_bottom;    // Bits 10-19  - Bottom boundary of drawing area
    int16_t drawing_x_offset;        // 11-bit signed X offset added to vertices
    int16_t drawing_y_offset;        // 11-bit signed Y offset added to vertices

    // --- Display Configuration State (GP1(05), GP1(06), GP1(07)) ---
    uint16_t display_vram_x_start;   // Bits 0-9    - X coordinate in VRAM for top-left of display area (must be even)
    uint16_t display_vram_y_start;   // Bits 10-18  - Y coordinate in VRAM for top-left of display area
    uint16_t display_horiz_start;    // Bits 0-11   - Horizontal start timing relative to HSYNC
    uint16_t display_horiz_end;      // Bits 12-23  - Horizontal end timing relative to HSYNC
    uint16_t display_line_start;     // Bits 0-9    - Vertical start timing relative to VSYNC
    uint16_t display_line_end;       // Bits 10-19  - Vertical end timing relative to VSYNC

    // --- GP0 Port State ---
    CommandBuffer gp0_command_buffer; // Buffer for current command parameters
    uint32_t gp0_words_remaining;     // Words remaining for current command or image load
    uint8_t gp0_current_opcode;       // Opcode of the multi-word command being processed
    Gp0Mode gp0_mode;                 // Current mode (Command or ImageLoad)
    void (*gp0_command_method)(Gpu*); // Function pointer to the current command handler

    // --- VRAM Load State (for GP0(A0)) ---
    uint16_t vram_load_x;             // Target X coordinate in VRAM for current image load
    uint16_t vram_load_y;             // Target Y coordinate in VRAM for current image load
    uint16_t vram_load_w;             // Width of the image being loaded
    uint16_t vram_load_h;             // Height of the image being loaded
    uint32_t vram_load_count;         // Counter for pixels transferred during current load

    //members to store the calculated texture page base coordinates
    uint16_t tpage_x_base;
    uint16_t tpage_y_base;

    // --- VRAM ---
    Vram vram;                         // The 1MB Video RAM buffer

    // --- Renderer ---
    Renderer renderer;                 // Handles OpenGL drawing operations

} Gpu;

// --- Function Prototypes ---
void gpu_init(Gpu* gpu);
void gpu_gp0(Gpu* gpu, uint32_t command); // Handles commands/data sent to GP0 port
void gpu_gp1(Gpu* gpu, uint32_t command); // Handles commands sent to GP1 port
uint32_t gpu_read_status(Gpu* gpu);       // Reads the GPUSTAT register value
uint32_t gpu_read_data(Gpu* gpu);         // Reads data from GPUREAD port (e.g., after Image Store)

#endif // GPU_H