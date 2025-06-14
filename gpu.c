/**
 * gpu.c
 * Implementation of the PlayStation GPU emulation.
 * Handles GPU state, command processing (GP0/GP1), VRAM access, and rendering calls.
 */
#include "gpu.h"
#include <stdio.h>
#include <stdlib.h> // For exit()
#include <string.h> // For memset
#include "renderer.h"
#include "dma.h"
// vram.h is implicitly included via gpu.h

// --- Forward Declarations for GP0 Handlers (Internal linkage) ---
static void gp0_nop(Gpu* gpu);
static void gp0_clear_cache(Gpu* gpu);
static void gp0_fill_rectangle(Gpu* gpu);
static void gp0_draw_mode(Gpu* gpu);
static void gp0_texture_window(Gpu* gpu);
static void gp0_drawing_area_top_left(Gpu* gpu);
static void gp0_drawing_area_bottom_right(Gpu* gpu);
static void gp0_drawing_offset(Gpu* gpu);
static void gp0_mask_bit_setting(Gpu* gpu);
static void gp0_quad_mono_opaque(Gpu* gpu);
static void gp0_quad_texture_blend_opaque(Gpu* gpu);
static void gp0_quad_shaded_opaque(Gpu* gpu);
static void gp0_triangle_shaded_opaque(Gpu* gpu);
static void gp0_image_load(Gpu* gpu);
static void gp0_image_store(Gpu* gpu);

// --- Forward Declarations for GP1 Handlers (Internal linkage) ---
static void gp1_reset(Gpu* gpu, uint32_t value);
static void gp1_reset_command_buffer(Gpu* gpu, uint32_t value);
static void gp1_acknowledge_irq(Gpu* gpu, uint32_t value);
static void gp1_display_enable(Gpu* gpu, uint32_t value);
static void gp1_dma_direction(Gpu* gpu, uint32_t value);
static void gp1_display_vram_start(Gpu* gpu, uint32_t value);
static void gp1_display_horizontal_range(Gpu* gpu, uint32_t value);
static void gp1_display_vertical_range(Gpu* gpu, uint32_t value);
static void gp1_display_mode(Gpu* gpu, uint32_t value);


// --- Helper Functions ---

/**
 * @brief Reads a 16-bit pixel from VRAM at given coordinates.
 * @param gpu Pointer to the Gpu instance.
 * @param x The x-coordinate in VRAM.
 * @param y The y-coordinate in VRAM.
 * @return The 16-bit pixel data.
 */
static uint16_t gpu_vram_read(Gpu* gpu, uint16_t x, uint16_t y) {
    // Prevent reading out of bounds
    if (x >= VRAM_WIDTH || y >= VRAM_HEIGHT) {
        return 0;
    }
    // Calculate the linear index in the VRAM buffer
    uint32_t index = (y * VRAM_WIDTH) + x;
    return ((uint16_t*)gpu->vram.data)[index];
}

/**
 * @brief Writes a 16-bit pixel to VRAM at given coordinates.
 * @param gpu Pointer to the Gpu instance.
 * @param x The x-coordinate in VRAM.
 * @param y The y-coordinate in VRAM.
 * @param pixel_data The 16-bit pixel data to write.
 */
static void gpu_vram_write(Gpu* gpu, uint16_t x, uint16_t y, uint16_t pixel_data) {
    // Prevent writing out of bounds
    if (x >= VRAM_WIDTH || y >= VRAM_HEIGHT) {
        return;
    }
    // Calculate the linear index in the VRAM buffer
    uint32_t index = (y * VRAM_WIDTH) + x;
    ((uint16_t*)gpu->vram.data)[index] = pixel_data;
}



/**
 * @brief Clears the GP0 command buffer.
 * @param gpu Pointer to the Gpu instance.
 */
static void clear_gp0_command_buffer(Gpu* gpu) {
    gpu->gp0_command_buffer.count = 0;
    // No need to zero the buffer content itself
}

/**
 * @brief Pushes a word onto the GP0 command buffer.
 * Handles potential buffer overflow.
 * @param gpu Pointer to the Gpu instance.
 * @param word The 32-bit command word to push.
 */
static void push_gp0_command_word(Gpu* gpu, uint32_t word) {
    if (gpu->gp0_command_buffer.count >= MAX_GPU_COMMAND_WORDS) {
        fprintf(stderr, "FATAL: GP0 Command Buffer Overflow! Opcode: 0x%02x\n", gpu->gp0_current_opcode);
        // Consider triggering a CPU exception or other error handling
        exit(EXIT_FAILURE); // Exit for now, as this indicates a major issue
    }
    gpu->gp0_command_buffer.buffer[gpu->gp0_command_buffer.count] = word;
    gpu->gp0_command_buffer.count++;
}

// --- GP1 Handler Function Definitions ---

/** GP1(0x00): Soft Reset */
static void gp1_reset(Gpu* gpu, uint32_t value) {
    printf("GPU: Soft Reset (GP1 Cmd 0x00)\n");
    (void)value; // value is unused for this command
    // Re-initialize GPU state AND the VRAM state by calling gpu_init
    gpu_init(gpu);
}

/** GP1(0x01): Reset Command Buffer */
static void gp1_reset_command_buffer(Gpu* gpu, uint32_t value) {
     printf("GPU: Reset Command Buffer (GP1 Cmd 0x01)\n");
    (void)value; // value is unused for this command
    clear_gp0_command_buffer(gpu);
    gpu->gp0_words_remaining = 0;
    gpu->gp0_mode = GP0_MODE_COMMAND; // Reset mode
    gpu->gp0_current_opcode = 0xFF; // Reset opcode tracking
    gpu->gp0_command_method = NULL; // Reset handler pointer
    // TODO: Should also clear the internal hardware FIFO if/when implemented.
}

/** GP1(0x02): Acknowledge GPU Interrupt */
static void gp1_acknowledge_irq(Gpu* gpu, uint32_t value) {
     printf("GPU: Acknowledge IRQ (GP1 Cmd 0x02)\n");
     (void)value; // value is unused for this command
     gpu->interrupt = false; // Clear the interrupt flag (STAT[24])
}

/** GP1(0x03): Display Enable */
static void gp1_display_enable(Gpu* gpu, uint32_t value) {
    // Bit 0: 0 = Enable Display, 1 = Disable Display
    gpu->display_disabled = (value & 1);
    printf("GPU: Display Enable = %s (GP1 Cmd 0x03)\n", gpu->display_disabled ? "Disabled" : "Enabled");
}

/** GP1(0x04): DMA Direction / Request settings */
static void gp1_dma_direction(Gpu* gpu, uint32_t value) {
    // Bits 0-1 select mode
    switch (value & 3) {
        case 0: gpu->dma_setting = GPU_DMA_Off; break;
        case 1: gpu->dma_setting = GPU_DMA_Fifo; break;
        case 2: gpu->dma_setting = GPU_DMA_CpuToGp0; break;
        case 3: gpu->dma_setting = GPU_DMA_VRamToCpu; break;
    }
     printf("GPU: DMA Direction = %d (GP1 Cmd 0x04)\n", gpu->dma_setting);
}

/** GP1(0x05): Start of Display area in VRAM */
static void gp1_display_vram_start(Gpu* gpu, uint32_t value) {
    // Bits 0-9: X start coordinate in VRAM (1024 width) - LSB ignored (must be even)
    // Bits 10-18: Y start coordinate in VRAM (512 height)
    gpu->display_vram_x_start = (uint16_t)(value & 0x3FE);
    gpu->display_vram_y_start = (uint16_t)((value >> 10) & 0x1FF);
    printf("GPU: Display VRAM Start X=%u Y=%u (GP1 Cmd 0x05)\n",
        gpu->display_vram_x_start, gpu->display_vram_y_start);
}

/** GP1(0x06): Display Horizontal sync and display range */
static void gp1_display_horizontal_range(Gpu* gpu, uint32_t value) {
    // Bits 0-11: Hsync Start coordinate (dotclock units)
    // Bits 12-23: Hsync End coordinate (dotclock units)
    gpu->display_horiz_start = (uint16_t)(value & 0xFFF);
    gpu->display_horiz_end = (uint16_t)((value >> 12) & 0xFFF);
    printf("GPU: Display H-Range Start=%u End=%u (GP1 Cmd 0x06)\n",
        gpu->display_horiz_start, gpu->display_horiz_end);
}

/** GP1(0x07): Display Vertical sync and display range */
static void gp1_display_vertical_range(Gpu* gpu, uint32_t value) {
    // Bits 0-9: Vsync Start coordinate (scanline units)
    // Bits 10-19: Vsync End coordinate (scanline units)
    gpu->display_line_start = (uint16_t)(value & 0x3FF);
    gpu->display_line_end = (uint16_t)((value >> 10) & 0x3FF);
     printf("GPU: Display V-Range Start=%u End=%u (GP1 Cmd 0x07)\n",
        gpu->display_line_start, gpu->display_line_end);
}

/** GP1(0x08): Display Mode */
static void gp1_display_mode(Gpu* gpu, uint32_t value) {
    // Bits 0-1: Horizontal Resolution 1 (hr1 -> STAT[17:16])
    // Bit 6:    Horizontal Resolution 2 (hr2 -> STAT[18])
    gpu->hres_raw.hr1 = (uint8_t)(value & 3);
    gpu->hres_raw.hr2 = (uint8_t)((value >> 6) & 1);
    // Bit 2: Vertical Resolution (0=240, 1=480) -> STAT[19]
    gpu->vres = ((value >> 2) & 1) ? Y480Lines : Y240Lines;
    // Bit 3: Video Mode (0=NTSC, 1=PAL) -> STAT[20]
    gpu->vmode = ((value >> 3) & 1) ? Pal : Ntsc;
    // Bit 4: Display Area Color Depth (0=15bpp, 1=24bpp) -> STAT[21]
    gpu->display_depth = ((value >> 4) & 1) ? D24Bits : D15Bits;
    // Bit 5: Interlaced output (0=off/progressive, 1=on) -> STAT[22]
    gpu->interlaced = ((value >> 5) & 1);
    // Bit 7: Unsupported "Reverseflag"
    if ((value >> 7) & 1) {
        fprintf(stderr, "Warning: GPU GP1(0x08) set unsupported Reverseflag bit\n");
    }
    printf("GPU: Display Mode set (GP1 Cmd 0x08)\n");
}


// --- GP0 Command Handler Definitions ---

/** GP0(0x00): No Operation */
static void gp0_nop(Gpu* gpu) {
    (void)gpu; // Does nothing
}

/** GP0(0x01): Clear Cache (Texture Cache Invalidation) */
static void gp0_clear_cache(Gpu* gpu) {
    printf("GP0(0x01): Clear Cache (Ignoring - No texture cache implemented)\n");
    (void)gpu;
}

/** GP0(0x02): Fill Rectangle in VRAM */
static void gp0_fill_rectangle(Gpu* gpu) {
    // TODO: Implement VRAM fill using color (word 0), coords (word 1), dimensions (word 2)
    printf("GP0(0x02): Fill Rectangle (Not Implemented Yet)\n");
    (void)gpu;
}

/** GP0(0xE1): Set Draw Mode */
static void gp0_draw_mode(Gpu* gpu) {
    uint32_t value = gpu->gp0_command_buffer.buffer[0];
    gpu->page_base_x = (uint8_t)(value & 0xF);
    gpu->page_base_y = (uint8_t)((value >> 4) & 1);
    gpu->semi_transparency = (uint8_t)((value >> 5) & 3);
    switch ((value >> 7) & 3) {
        case 0: gpu->texture_depth = T4Bit; break;
        case 1: gpu->texture_depth = T8Bit; break;
        case 2: gpu->texture_depth = T15Bit; break;
        default: printf("Warn: GP0(E1) Unknown texture depth %d\n", (value >> 7) & 3); break;
    }
    gpu->dithering = ((value >> 9) & 1);
    gpu->draw_to_display = ((value >> 10) & 1);
    gpu->texture_disable = ((value >> 11) & 1);       // Affects textured primitives
    gpu->rectangle_texture_x_flip = ((value >> 12) & 1); // Affects texture sampling
    gpu->rectangle_texture_y_flip = ((value >> 13) & 1); // Affects texture sampling
    // printf("GP0(0xE1): Draw Mode set\n"); // Optional debug
}

/** GP0(0xE2): Set Texture Window */
static void gp0_texture_window(Gpu* gpu) {
     uint32_t value = gpu->gp0_command_buffer.buffer[0];
     gpu->texture_window_x_mask    = (uint8_t)(value & 0x1F);
     gpu->texture_window_y_mask    = (uint8_t)((value >> 5) & 0x1F);
     gpu->texture_window_x_offset  = (uint8_t)((value >> 10) & 0x1F);
     gpu->texture_window_y_offset  = (uint8_t)((value >> 15) & 0x1F);
     // printf("GP0(0xE2): Texture Window set\n"); // Optional debug
}

/** GP0(0xE3): Set Drawing Area Top Left */
static void gp0_drawing_area_top_left(Gpu* gpu) {
    uint32_t value = gpu->gp0_command_buffer.buffer[0];
    gpu->drawing_area_left = (uint16_t)(value & 0x3FF);
    gpu->drawing_area_top  = (uint16_t)((value >> 10) & 0x3FF);
    // printf("GP0(0xE3): Draw Area TL set = (%u,%u)\n", gpu->drawing_area_left, gpu->drawing_area_top);
}

/** GP0(0xE4): Set Drawing Area Bottom Right */
static void gp0_drawing_area_bottom_right(Gpu* gpu) {
    uint32_t value = gpu->gp0_command_buffer.buffer[0];
    gpu->drawing_area_right = (uint16_t)(value & 0x3FF);
    gpu->drawing_area_bottom= (uint16_t)((value >> 10) & 0x3FF);
    // printf("GP0(0xE4): Draw Area BR set = (%u,%u)\n", gpu->drawing_area_right, gpu->drawing_area_bottom);
}

/** GP0(0xE5): Set Drawing Offset */
static void gp0_drawing_offset(Gpu* gpu) {
    uint32_t value = gpu->gp0_command_buffer.buffer[0];
    uint16_t x_raw = (uint16_t)(value & 0x7FF);
    uint16_t y_raw = (uint16_t)((value >> 11) & 0x7FF);
    // Sign extend 11-bit values
    int16_t offset_x = (int16_t)(x_raw << 5) >> 5;
    int16_t offset_y = (int16_t)(y_raw << 5) >> 5;
    gpu->drawing_x_offset = offset_x;
    gpu->drawing_y_offset = offset_y;
    // printf("GP0(0xE5): Draw Offset set = (%d,%d)\n", offset_x, offset_y);
    renderer_set_draw_offset(&gpu->renderer, offset_x, offset_y); // Update renderer uniform
    // --- TEMPORARY HACK from guide ---
    // printf("GP0(0xE5): Triggering display (temporary hack)\n");
    renderer_display(&gpu->renderer); // Force draw & display swap
    // -----------------------------------------
}

/** GP0(0xE6): Set Mask Bit Setting */
static void gp0_mask_bit_setting(Gpu* gpu) {
     uint32_t value = gpu->gp0_command_buffer.buffer[0];
     gpu->force_set_mask_bit = (value & 1);        // Affects drawing
     gpu->preserve_masked_pixels = ((value >> 1) & 1); // Affects drawing
     // printf("GP0(0xE6): Mask Bit Setting = Force:%d Preserve:%d\n", gpu->force_set_mask_bit, gpu->preserve_masked_pixels);
}

/** GP0(0x28): Monochrome Opaque Quad */
static void gp0_quad_mono_opaque(Gpu* gpu) {
    if (gpu->gp0_command_buffer.count < 5) {
         fprintf(stderr, "GP0(0x28) Error: Expected 5 words, got %u\n", gpu->gp0_command_buffer.count); return; }
    RendererColor c = { .r=(GLubyte)(gpu->gp0_command_buffer.buffer[0]&0xFF), .g=(GLubyte)((gpu->gp0_command_buffer.buffer[0]>>8)&0xFF), .b=(GLubyte)((gpu->gp0_command_buffer.buffer[0]>>16)&0xFF) };
    RendererColor colors[4] = {c, c, c, c};
    RendererPosition positions[4];
    for(int i=0; i<4; ++i){ uint32_t v=gpu->gp0_command_buffer.buffer[i+1]; positions[i].x=(GLshort)(int16_t)(v&0xFFFF); positions[i].y=(GLshort)(int16_t)(v>>16); }
    // printf("GP0(0x28): Mono Quad ...\n");
    renderer_push_quad(&gpu->renderer, positions, colors);
}

/** GP0(0x2C): Textured Opaque Quadrilateral with Blend */
static void gp0_quad_texture_blend_opaque(Gpu* gpu) {
    if (gpu->gp0_command_buffer.count < 9) {
         fprintf(stderr, "GP0(0x2C) Error: Expected 9 words, got %u\n", gpu->gp0_command_buffer.count); return; }
    RendererPosition p[4]; uint32_t uv[4]; uint16_t clut, texpage;
    p[0] = (RendererPosition){ .x=(GLshort)(int16_t)(gpu->gp0_command_buffer.buffer[1]&0xFFFF), .y=(GLshort)(int16_t)(gpu->gp0_command_buffer.buffer[1]>>16) }; uv[0]=gpu->gp0_command_buffer.buffer[2];
    p[1] = (RendererPosition){ .x=(GLshort)(int16_t)(gpu->gp0_command_buffer.buffer[3]&0xFFFF), .y=(GLshort)(int16_t)(gpu->gp0_command_buffer.buffer[3]>>16) }; uv[1]=gpu->gp0_command_buffer.buffer[4];
    p[2] = (RendererPosition){ .x=(GLshort)(int16_t)(gpu->gp0_command_buffer.buffer[5]&0xFFFF), .y=(GLshort)(int16_t)(gpu->gp0_command_buffer.buffer[5]>>16) }; uv[2]=gpu->gp0_command_buffer.buffer[6];
    p[3] = (RendererPosition){ .x=(GLshort)(int16_t)(gpu->gp0_command_buffer.buffer[7]&0xFFFF), .y=(GLshort)(int16_t)(gpu->gp0_command_buffer.buffer[7]>>16) }; uv[3]=gpu->gp0_command_buffer.buffer[8];
    clut = (uint16_t)(uv[0] >> 16); texpage = (uint16_t)(uv[1] >> 16);
    (void)clut; (void)texpage; (void)uv; // Suppress unused warnings for now
    RendererColor placeholder = { .r = 0x80, .g = 0x00, .b = 0x00 }; // Placeholder color
    RendererColor c[4] = {placeholder, placeholder, placeholder, placeholder};
    // printf("GP0(0x2C): Tex Quad Blend ... (Using Placeholder Color)\n");
    renderer_push_quad(&gpu->renderer, p, c); // Pass p and c (positions, colors)
}

/** GP0(0x38): Shaded Opaque Quad */
static void gp0_quad_shaded_opaque(Gpu* gpu) {
    if (gpu->gp0_command_buffer.count < 8) {
         fprintf(stderr, "GP0(0x38) Error: Expected 8 words, got %u\n", gpu->gp0_command_buffer.count); return; }
    RendererColor c[4]; RendererPosition p[4];
    for (int i = 0; i < 4; ++i) {
        uint32_t cw=gpu->gp0_command_buffer.buffer[i*2]; uint32_t vw=gpu->gp0_command_buffer.buffer[i*2+1];
        c[i].r=(GLubyte)(cw&0xFF); c[i].g=(GLubyte)((cw>>8)&0xFF); c[i].b=(GLubyte)((cw>>16)&0xFF);
        p[i].x=(GLshort)(int16_t)(vw&0xFFFF); p[i].y=(GLshort)(int16_t)(vw>>16); }
    // printf("GP0(0x38): Shaded Quad ...\n");
    renderer_push_quad(&gpu->renderer, p, c);
}

/** GP0(0x30): Shaded Opaque Triangle */
static void gp0_triangle_shaded_opaque(Gpu* gpu) {
    if (gpu->gp0_command_buffer.count < 6) {
         fprintf(stderr, "GP0(0x30) Error: Expected 6 words, got %u\n", gpu->gp0_command_buffer.count); return; }
    RendererColor c[3]; RendererPosition p[3];
    for (int i = 0; i < 3; ++i) {
        uint32_t cw=gpu->gp0_command_buffer.buffer[i*2]; uint32_t vw=gpu->gp0_command_buffer.buffer[i*2+1];
        c[i].r=(GLubyte)(cw&0xFF); c[i].g=(GLubyte)((cw>>8)&0xFF); c[i].b=(GLubyte)((cw>>16)&0xFF);
        p[i].x=(GLshort)(int16_t)(vw&0xFFFF); p[i].y=(GLshort)(int16_t)(vw>>16); }
    // printf("GP0(0x30): Shaded Triangle ...\n");
    renderer_push_triangle(&gpu->renderer, p, c);
}

/** GP0(0xA0): Copy Rectangle (CPU/DMA to VRAM) - Setup Phase */
static void gp0_image_load(Gpu* gpu) {
     if (gpu->gp0_command_buffer.count < 3) {
         fprintf(stderr, "GP0(0xA0) Error: Expected 3 words, got %u\n", gpu->gp0_command_buffer.count); return; }
    uint32_t dest_coord = gpu->gp0_command_buffer.buffer[1];
    uint32_t dimensions = gpu->gp0_command_buffer.buffer[2];
    gpu->vram_load_x = (uint16_t)(dest_coord & 0x3FF); // X coord is 10 bits
    gpu->vram_load_y = (uint16_t)((dest_coord >> 16) & 0x1FF); // Y coord is 9 bits
    gpu->vram_load_w = (uint16_t)(dimensions & 0x3FF); // Width is 10 bits
    gpu->vram_load_h = (uint16_t)((dimensions >> 16) & 0x1FF); // Height is 9 bits

    // Width and height seem to be stored as W-1, H-1 in some docs, but maybe not always?
    // Let's assume they are direct values for now. Clamp values for safety.
    if (gpu->vram_load_w == 0) gpu->vram_load_w = 1024; // Nocash says 0 means 1024?
    if (gpu->vram_load_h == 0) gpu->vram_load_h = 512;  // Nocash says 0 means 512?
    gpu->vram_load_w = (gpu->vram_load_w > VRAM_WIDTH) ? VRAM_WIDTH : gpu->vram_load_w;
    gpu->vram_load_h = (gpu->vram_load_h > VRAM_HEIGHT) ? VRAM_HEIGHT : gpu->vram_load_h;


    uint32_t image_size_pixels = (uint32_t)gpu->vram_load_w * (uint32_t)gpu->vram_load_h;
    uint32_t image_size_pixels_rounded = (image_size_pixels + 1) & ~1; // Round up for pairs
    uint32_t words_to_load = image_size_pixels_rounded / 2;            // Each word contains 2 pixels

    printf("GP0(0xA0): Setup Image Load to VRAM (%u,%u) Size=(%ux%u) -> Expecting %u words\n",
           gpu->vram_load_x, gpu->vram_load_y, gpu->vram_load_w, gpu->vram_load_h, words_to_load);

    if (words_to_load == 0 || ((uint64_t)words_to_load * 4) > VRAM_SIZE) { // Basic sanity check
        fprintf(stderr, "Warning: Invalid image load size %u words requested.\n", words_to_load);
        gpu->gp0_words_remaining = 0; gpu->gp0_mode = GP0_MODE_COMMAND; return; }

    gpu->gp0_words_remaining = words_to_load;
    gpu->gp0_mode = GP0_MODE_IMAGE_LOAD;
    gpu->vram_load_count = 0; // Reset pixel counter for this transfer
}

/** GP0(0xC0): Copy Rectangle (VRAM to CPU/DMA) */
static void gp0_image_store(Gpu* gpu) {
    // Get source coordinates from the second command word
    uint32_t src_coord = gpu->gp0_command_buffer.buffer[1];
    gpu->vram_x_start = (uint16_t)(src_coord & 0x3FF);
    gpu->vram_y_start = (uint16_t)((src_coord >> 16) & 0x1FF);

    // Get dimensions from the third command word
    uint32_t dimensions = gpu->gp0_command_buffer.buffer[2];
    gpu->vram_transfer_width = (uint16_t)(dimensions & 0x3FF);
    gpu->vram_transfer_height = (uint16_t)((dimensions >> 16) & 0x1FF);

    // Hardware requires the width to be an even number of pixels for transfers.
    // Round up to the next multiple of 2.
    gpu->vram_transfer_width = (gpu->vram_transfer_width + 1) & ~1;
    
    // Calculate how many 32-bit words we need to send.
    // Each word contains two 16-bit pixels.
    uint32_t total_pixels = (uint32_t)gpu->vram_transfer_width * (uint32_t)gpu->vram_transfer_height;
    gpu->gp0_read_remaining_words = (total_pixels + 1) / 2;

    // Set the GPU into the correct mode for the CPU to read from it.
    gpu->gp0_mode = GP0_MODE_VRAM_TO_CPU;
    
    // Initialize the transfer cursors to the start position.
    gpu->vram_x_current = gpu->vram_x_start;
    gpu->vram_y_current = gpu->vram_y_start;

    printf("GP0(0xC0): VRAM->CPU. From:(%u,%u), Size:(%u,%u), Words:%u\n",
        gpu->vram_x_start, gpu->vram_y_start, gpu->vram_transfer_width, gpu->vram_transfer_height, gpu->gp0_read_remaining_words);
}


// --- Main GPU Public Functions ---

/**
 * @brief Initializes the GPU state, including VRAM and default register values.
 */
void gpu_init(Gpu* gpu) {
    printf("GPU Initializing...\n");
    vram_init(&gpu->vram); // Init VRAM first
    // Initialize all Gpu struct members to power-on/GP1 Reset defaults
    gpu->interrupt = false; gpu->page_base_x = 0; gpu->page_base_y = 0;
    gpu->semi_transparency = 0; gpu->texture_depth = T4Bit;
    gpu->texture_window_x_mask = 0; gpu->texture_window_y_mask = 0;
    gpu->texture_window_x_offset = 0; gpu->texture_window_y_offset = 0;
    gpu->dithering = false; gpu->draw_to_display = false;
    gpu->texture_disable = false; gpu->rectangle_texture_x_flip = false;
    gpu->rectangle_texture_y_flip = false; gpu->drawing_area_left = 0;
    gpu->drawing_area_top = 0; gpu->drawing_area_right = 0;
    gpu->drawing_area_bottom = 0; gpu->drawing_x_offset = 0;
    gpu->drawing_y_offset = 0; gpu->force_set_mask_bit = false;
    gpu->preserve_masked_pixels = false; gpu->dma_setting = GPU_DMA_Off;
    gpu->display_disabled = true; gpu->display_vram_x_start = 0;
    gpu->display_vram_y_start = 0; gpu->hres_raw = (HorizontalResRaw){0, 0};
    gpu->vres = Y240Lines; gpu->vmode = Ntsc; gpu->interlaced = true;
    gpu->display_depth = D15Bits; gpu->display_horiz_start = 0x200;
    gpu->display_horiz_end = 0xc00; gpu->display_line_start = 0x10;
    gpu->display_line_end = 0x100; gpu->field = Top;
    clear_gp0_command_buffer(gpu); gpu->gp0_words_remaining = 0;
    gpu->gp0_mode = GP0_MODE_COMMAND; gpu->gp0_current_opcode = 0xFF;
    gpu->gp0_command_method = NULL;
    gpu->vram_load_x = 0; gpu->vram_load_y = 0; gpu->vram_load_w = 0;
    gpu->vram_load_h = 0; gpu->vram_load_count = 0;
    printf("GPU Initialized (State reset, VRAM initialized).\n");
}

/** Processes commands/data sent to GP0 port */
void gpu_gp0(Gpu* gpu, uint32_t command) {
    // Handle IMAGE_LOAD state first
    if (gpu->gp0_mode == GP0_MODE_IMAGE_LOAD) {
        uint16_t pixel1 = (uint16_t)(command & 0xFFFF);
        uint16_t pixel2 = (uint16_t)(command >> 16);
        uint32_t idx = gpu->vram_load_count; // Base index for pixel 1
        // Check if pixel 1 is within the logical height*width boundary
        if (idx < ((uint32_t)gpu->vram_load_w * gpu->vram_load_h)) {
             uint16_t x = gpu->vram_load_x + (uint16_t)(idx % gpu->vram_load_w);
             uint16_t y = gpu->vram_load_y + (uint16_t)(idx / gpu->vram_load_w);
             // Check against physical VRAM boundaries
             if (y < VRAM_HEIGHT && x < VRAM_WIDTH) {
                 uint32_t offset = (uint32_t)y * VRAM_WIDTH * VRAM_BPP + (uint32_t)x * VRAM_BPP;
                 vram_store16(&gpu->vram, offset, pixel1);
             } // Else: Pixel write out of VRAM bounds (optional warning)
        }
        idx++; // Index for pixel 2
        // Check if pixel 2 is within the logical height*width boundary
        if (idx < ((uint32_t)gpu->vram_load_w * gpu->vram_load_h)) {
            uint16_t x = gpu->vram_load_x + (uint16_t)(idx % gpu->vram_load_w);
            uint16_t y = gpu->vram_load_y + (uint16_t)(idx / gpu->vram_load_w);
            // Check against physical VRAM boundaries
             if (y < VRAM_HEIGHT && x < VRAM_WIDTH) {
                 uint32_t offset = (uint32_t)y * VRAM_WIDTH * VRAM_BPP + (uint32_t)x * VRAM_BPP;
                 vram_store16(&gpu->vram, offset, pixel2);
             } // Else: Pixel write out of VRAM bounds (optional warning)
        }
        gpu->vram_load_count += 2; // Increment count by 2 pixels
        gpu->gp0_words_remaining--; // Decrement remaining data words
        if (gpu->gp0_words_remaining == 0) { // Check if transfer complete
            gpu->gp0_mode = GP0_MODE_COMMAND; // Switch back to command mode
            // printf("GPU Img Load Finished.\n"); // Optional debug
        }
        return; // Done processing this data word
    }

    // Handle COMMAND mode
    if (gpu->gp0_words_remaining == 0) {
        // Start of a new command
        uint8_t opcode = (uint8_t)(command >> 24);
        uint32_t expected_len = 0; void (*handler)(Gpu*) = NULL;
        gpu->gp0_current_opcode = opcode; clear_gp0_command_buffer(gpu);

        // Determine expected length and handler based on opcode
        switch (opcode) {
            case 0x00: expected_len = 1; handler = gp0_nop; break;
            case 0x01: expected_len = 1; handler = gp0_clear_cache; break;
            case 0x02: expected_len = 3; handler = gp0_fill_rectangle; break;
            case 0x28: expected_len = 5; handler = gp0_quad_mono_opaque; break;
            case 0x2C: expected_len = 9; handler = gp0_quad_texture_blend_opaque; break;
            case 0x30: expected_len = 6; handler = gp0_triangle_shaded_opaque; break;
            case 0x38: expected_len = 8; handler = gp0_quad_shaded_opaque; break;
            case 0xA0: expected_len = 3; handler = gp0_image_load; break; // Sets up IMAGE_LOAD mode
            case 0xC0: expected_len = 3; handler = gp0_image_store; break;
            case 0xE1: expected_len = 1; handler = gp0_draw_mode; break;
            case 0xE2: expected_len = 1; handler = gp0_texture_window; break;
            case 0xE3: expected_len = 1; handler = gp0_drawing_area_top_left; break;
            case 0xE4: expected_len = 1; handler = gp0_drawing_area_bottom_right; break;
            case 0xE5: expected_len = 1; handler = gp0_drawing_offset; break;
            case 0xE6: expected_len = 1; handler = gp0_mask_bit_setting; break;
            default:
                fprintf(stderr, "GPU Error: Unhandled GP0 Opcode 0x%02x (Cmd 0x%08x)\n", opcode, command);
                expected_len = 1; handler = gp0_nop; gpu->gp0_current_opcode = 0xFF; break; }

        // Sanity check length
        if (expected_len == 0 || expected_len > MAX_GPU_COMMAND_WORDS) {
             fprintf(stderr, "GPU Error: Cmd 0x%02x invalid length %u\n", opcode, expected_len);
             expected_len = 1; handler = gp0_nop; gpu->gp0_current_opcode = 0xFF; }

        gpu->gp0_words_remaining = expected_len;
        gpu->gp0_command_method = handler;
    }

    // Buffer the current command word
    push_gp0_command_word(gpu, command);
    gpu->gp0_words_remaining--;

    // If all words for the command received, execute the handler
    if (gpu->gp0_words_remaining == 0) {
         if (gpu->gp0_command_method != NULL) {
             (gpu->gp0_command_method)(gpu); // Call the stored function pointer
         } else {
             fprintf(stderr,"GPU Error: NULL handler for GP0 opcode 0x%02x\n", gpu->gp0_current_opcode);
         }
         // If we didn't just finish setting up IMAGE_LOAD mode, reset for next command
         if (gpu->gp0_mode == GP0_MODE_COMMAND) {
             clear_gp0_command_buffer(gpu);
             gpu->gp0_current_opcode = 0xFF; // Ready for next command
         }
    }
}

/** Processes commands sent to GP1 port */
void gpu_gp1(Gpu* gpu, uint32_t command) {
    uint32_t opcode = (command >> 24) & 0xFF;
    switch (opcode) {
        case 0x00: gp1_reset(gpu, command); break;
        case 0x01: gp1_reset_command_buffer(gpu, command); break;
        case 0x02: gp1_acknowledge_irq(gpu, command); break;
        case 0x03: gp1_display_enable(gpu, command); break;
        case 0x04: gp1_dma_direction(gpu, command); break;
        case 0x05: gp1_display_vram_start(gpu, command); break;
        case 0x06: gp1_display_horizontal_range(gpu, command); break;
        case 0x07: gp1_display_vertical_range(gpu, command); break;
        case 0x08: gp1_display_mode(gpu, command); break;
        // Add cases for 0x09 (Get GPU Info), 0x10-0x1F (GPU Info responses) if needed
        default:
            fprintf(stderr, "Error: Unhandled GP1 command: Opcode 0x%02x, Value 0x%08x\n", opcode, command);
            break;
    }
}

/** Reads the GPU Status Register (GPUSTAT) */
uint32_t gpu_read_status(Gpu* gpu) {
    uint32_t r = 0;
    // --- Basic state bits (0-24) ---
    r |= (uint32_t)gpu->page_base_x << 0;
    r |= (uint32_t)gpu->page_base_y << 4;
    r |= (uint32_t)gpu->semi_transparency << 5;
    r |= (uint32_t)gpu->texture_depth << 7;
    r |= (uint32_t)gpu->dithering << 9;
    r |= (uint32_t)gpu->draw_to_display << 10;
    r |= (uint32_t)gpu->force_set_mask_bit << 11;
    r |= (uint32_t)gpu->preserve_masked_pixels << 12;
    r |= (uint32_t)gpu->field << 13;
    r |= (uint32_t)gpu->texture_disable << 15;
    uint32_t hres_raw_val = ((uint32_t)gpu->hres_raw.hr2 << 2) | (uint32_t)gpu->hres_raw.hr1;
    r |= ((hres_raw_val >> 0) & 1) << 16;
    r |= ((hres_raw_val >> 1) & 1) << 17;
    r |= ((hres_raw_val >> 2) & 1) << 18;
    r |= (gpu->vres == Y480Lines) << 19;
    r |= ((uint32_t)gpu->vmode << 20);
    r |= ((uint32_t)gpu->display_depth << 21);
    r |= ((uint32_t)gpu->interlaced << 22);
    r |= ((uint32_t)gpu->display_disabled << 23);
    r |= ((uint32_t)gpu->interrupt << 24);
    
    // --- CPU/GPU Sync Flags (26-28) ---
    // STAT[26]: Ready to receive a command. (1 if not in a data transfer or multi-word command).
    r |= (gpu->gp0_mode == GP0_MODE_COMMAND && gpu->gp0_words_remaining == 0) << 26;
    // STAT[27]: Ready to send data from VRAM to CPU. (1 if we are in VRAM_TO_CPU mode).
    r |= (gpu->gp0_mode == GP0_MODE_VRAM_TO_CPU) << 27;
    // STAT[28]: Ready to receive a DMA block.
    r |= (1 << 28); // Always ready for now.
    
    // --- DMA Direction (29-30) ---
    r |= ((uint32_t)gpu->dma_setting << 29);
    
    // Bit 25 is a derived DMA request signal, can be left 0 for now.
    // Bit 31 is odd/even line signal, can be left 0 for now.
    return r;
}

/** Reads data from the GPUREAD port (e.g., after Image Store command) */
uint32_t gpu_read_data(Gpu* gpu) {
    // This function should only be called by the CPU when the GPU is in VRAM_TO_CPU mode.
    if (gpu->gp0_mode != GP0_MODE_VRAM_TO_CPU) {
        fprintf(stderr, "Warning: GPUREAD access outside of VRAM_TO_CPU mode.\n");
        return 0;
    }

    if (gpu->gp0_read_remaining_words > 0) {
        // Read the first 16-bit pixel from the current VRAM coordinates.
        uint16_t pixel1 = gpu_vram_read(gpu, gpu->vram_x_current, gpu->vram_y_current);
        
        // Advance the coordinates for the next pixel.
        gpu->vram_x_current++;
        if (gpu->vram_x_current >= gpu->vram_x_start + gpu->vram_transfer_width) {
            gpu->vram_x_current = gpu->vram_x_start; // Wrap to next line
            gpu->vram_y_current++;
        }

        // Read the second 16-bit pixel.
        uint16_t pixel2 = gpu_vram_read(gpu, gpu->vram_x_current, gpu->vram_y_current);

        // Advance the coordinates again.
        gpu->vram_x_current++;
        if (gpu->vram_x_current >= gpu->vram_x_start + gpu->vram_transfer_width) {
            gpu->vram_x_current = gpu->vram_x_start; // Wrap to next line
            gpu->vram_y_current++;
        }

        gpu->gp0_read_remaining_words--;
        
        // If that was the last word, the transfer is complete. Return to normal command mode.
        if (gpu->gp0_read_remaining_words == 0) {
            gpu->gp0_mode = GP0_MODE_COMMAND;
            printf("GPU: VRAM->CPU transfer complete.\n");
        }
        
        // Combine the two 16-bit pixels into a single 32-bit word and return it.
        return ((uint32_t)pixel2 << 16) | pixel1;
    }

    fprintf(stderr, "Warning: GPUREAD access after transfer was complete.\n");
    return 0; // Should not happen if the CPU is well-behaved.
}