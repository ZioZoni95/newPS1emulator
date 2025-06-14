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
    GP0_MODE_COMMAND,       // Expecting GP0 command words
    GP0_MODE_IMAGE_LOAD,    // Expecting pixel data for CPU->VRAM transfer
    GP0_MODE_VRAM_TO_CPU    // Preparing pixel data for VRAM->CPU transfer
} Gp0Mode;

// GP0 Command Buffer
#define MAX_GPU_COMMAND_WORDS 16
typedef struct {
    uint32_t buffer[MAX_GPU_COMMAND_WORDS];
    uint8_t count;
} CommandBuffer;


// --- Main Gpu State Structure ---
typedef struct Gpu Gpu;

typedef struct Gpu {
    // --- GPUSTAT Fields & Related State ---
    uint8_t page_base_x;
    uint8_t page_base_y;
    uint8_t semi_transparency;
    TextureDepth texture_depth;
    bool dithering;
    bool draw_to_display;
    bool force_set_mask_bit;
    bool preserve_masked_pixels;
    Field field;
    bool texture_disable;
    bool rectangle_texture_x_flip;
    bool rectangle_texture_y_flip;
    HorizontalResRaw hres_raw;
    VerticalRes vres;
    VMode vmode;
    DisplayDepth display_depth;
    bool interlaced;
    bool display_disabled;
    bool interrupt;
    GpuDmaSetting dma_setting;

    // --- Texture Window State (GP0(E2)) ---
    uint8_t texture_window_x_mask;
    uint8_t texture_window_y_mask;
    uint8_t texture_window_x_offset;
    uint8_t texture_window_y_offset;

    // --- Drawing Area & Offset State (GP0(E3), GP0(E4), GP0(E5)) ---
    uint16_t drawing_area_left;
    uint16_t drawing_area_top;
    uint16_t drawing_area_right;
    uint16_t drawing_area_bottom;
    int16_t drawing_x_offset;
    int16_t drawing_y_offset;

    // --- Display Configuration State (GP1(05-07)) ---
    uint16_t display_vram_x_start;
    uint16_t display_vram_y_start;
    uint16_t display_horiz_start;
    uint16_t display_horiz_end;
    uint16_t display_line_start;
    uint16_t display_line_end;

    // --- GP0 Port State ---
    CommandBuffer gp0_command_buffer;
    uint32_t gp0_words_remaining;
    uint8_t gp0_current_opcode;
    Gp0Mode gp0_mode;
    void (*gp0_command_method)(Gpu*);

    // --- VRAM Load State (GP0(A0)) ---
    uint16_t vram_load_x;
    uint16_t vram_load_y;
    uint16_t vram_load_w;
    uint16_t vram_load_h;
    uint32_t vram_load_count;

    // --- VRAM Read State (GP0(C0)) ---
    uint32_t gp0_read_remaining_words;
    uint16_t vram_x_start;
    uint16_t vram_y_start;
    uint16_t vram_x_current;
    uint16_t vram_y_current;
    uint16_t vram_transfer_width;
    uint16_t vram_transfer_height;
    uint32_t vram_to_cpu_buffer_index;
    
    // --- VRAM, Renderer & GPUREAD Temporary Register ---
    Vram vram;
    Renderer renderer;
    uint32_t gpu_read; // Temporary storage for data read via GPUREAD port

} Gpu;

// --- Function Prototypes ---
void gpu_init(Gpu* gpu);
void gpu_gp0(Gpu* gpu, uint32_t command);
void gpu_gp1(Gpu* gpu, uint32_t command);
uint32_t gpu_read_status(Gpu* gpu);
uint32_t gpu_read_data(Gpu* gpu);

// Helpers for VRAM access
uint16_t vram_read16(Vram* vram, uint32_t address);
void vram_write16(Vram* vram, uint32_t address, uint16_t value);


#endif // GPU_H