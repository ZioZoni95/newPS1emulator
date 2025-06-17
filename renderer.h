#ifndef RENDERER_H
#define RENDERER_H

#include <stdint.h>
#include <stdbool.h>

// --- OpenGL Includes ---
// Make sure you have GLEW (or GLAD) headers included correctly in your project setup
#define GLEW_STATIC // Or define dynamically if preferred
#include <GL/glew.h> // Or your GLAD/other GL header

// --- Renderer-Specific Data Types ---

// Represents a 2D vertex position in PSX VRAM coordinates (signed 16-bit)
typedef struct {
    GLshort x, y; // OpenGL types (GLshort is int16_t)
} RendererPosition;

// Represents an RGB color (unsigned 8-bit per component)
typedef struct {
    GLubyte r, g, b; // OpenGL types (GLubyte is uint8_t)
} RendererColor;

//activating and defining the RendererTexCoord struct.
// Using GLshort is better than GLubyte because texture coordinates can be > 255.
typedef struct {
    GLshort u, v; // Using GLshort to match VRAM's coordinate space (0-1023)
} RendererTexCoord;
// Add RendererTexCoord struct later if needed:
// typedef struct { GLubyte u, v; } RendererTexCoord;

// --- Renderer State ---

// Maximum number of vertices the renderer can buffer before forcing a draw call.
// Adjust as needed for performance/memory trade-offs. (Guide uses 64*1024)
#define VERTEX_BUFFER_LEN (64 * 1024)

// Structure holding the state of the OpenGL renderer
typedef struct {
    // OpenGL Object IDs
    GLuint vao;             // Vertex Array Object: Groups VBO bindings and attribute pointers
    GLuint position_buffer; // Vertex Buffer Object (VBO) storing vertex positions
    GLuint color_buffer;    // Vertex Buffer Object (VBO) storing vertex colors
    GLuint texcoord_buffer; // VBO for texture coordinates
    
    // GLuint texcoord_buffer; // VBO for texture coordinates (future implementation)
    GLuint shader_program;  // ID of the compiled and linked GLSL shader program
    GLuint vram_texture_id; // VRAM 
    // Shader Uniform Location
    GLint uniform_offset_loc; // Location ID of the 'offset' uniform in the vertex shader

    // CPU-Side Buffers (Temporary storage before uploading to GPU)
    // These hold the data pushed by the GPU command handlers.
    RendererPosition positions_data[VERTEX_BUFFER_LEN]; // CPU buffer for vertex positions
    RendererColor colors_data[VERTEX_BUFFER_LEN];       // CPU buffer for vertex colors
    //CPU-side buffer for texture coordinates, matching the others.
    RendererTexCoord texcoords_data[VERTEX_BUFFER_LEN];
    // State Tracking
    uint32_t vertex_count;      // Number of vertices currently buffered in the CPU-side arrays
    bool initialized;           // Flag indicating if the renderer has been successfully initialized
} Renderer;

// --- Function Prototypes ---

/**
 * @brief Initializes the OpenGL renderer.
 * Compiles shaders, links program, creates VAO and VBOs, sets initial GL state.
 * Must be called after an OpenGL context is created.
 * @param renderer Pointer to the Renderer struct to initialize.
 * @return True if initialization was successful, false otherwise.
 */
bool renderer_init(Renderer* renderer);

/**
 * @brief Buffers a triangle's vertex data for later drawing.
 * Copies position and color data into the renderer's CPU-side buffers.
 * If the buffer is full, it forces a draw call before adding the new triangle.
 * @param renderer Pointer to the Renderer instance.
 * @param pos Array of 3 vertex positions.
 * @param col Array of 3 vertex colors.
 */
void renderer_push_triangle(Renderer* renderer, RendererPosition pos[3], RendererColor col[3]);

/**
 * @brief Buffers a quadrilateral's vertex data (as two triangles) for later drawing.
 * Decomposes the quad into two triangles and copies their vertex data.
 * If the buffer is full, it forces a draw call before adding the new quad.
 * @param renderer Pointer to the Renderer instance.
 * @param pos Array of 4 vertex positions (in PSX order).
 * @param col Array of 4 vertex colors (corresponding to positions).
 */
void renderer_push_quad(Renderer* renderer, RendererPosition pos[4], RendererColor col[4]);

/**
 * @brief Uploads buffered vertex data to the GPU and performs the OpenGL draw call.
 * Uses glBufferSubData to update VBOs with data from CPU buffers.
 * Issues a glDrawArrays call to render the buffered primitives (as triangles).
 * Resets the vertex count after drawing.
 * @param renderer Pointer to the Renderer instance.
 */
void renderer_draw(Renderer* renderer);

/**
 * @brief Helper function to draw buffered primitives and swap the window buffers.
 * Typically called once per frame from the main loop.
 * @param renderer Pointer to the Renderer instance.
 * // Removed SDL_Window* - swap happens in main loop
 */
void renderer_display(Renderer* renderer);

/**
 * @brief Sets the drawing offset uniform in the vertex shader.
 * Forces a draw of currently buffered primitives before updating the offset.
 * @param renderer Pointer to the Renderer instance.
 * @param x The signed horizontal drawing offset.
 * @param y The signed vertical drawing offset.
 */
void renderer_set_draw_offset(Renderer* renderer, int16_t x, int16_t y);

/**
 * @brief Destroys OpenGL resources (VBOs, VAO, Shader Program).
 * Should be called before the OpenGL context is destroyed.
 * @param renderer Pointer to the Renderer instance to destroy.
 */
void renderer_destroy(Renderer* renderer);

/**
 * @brief Checks for OpenGL errors using glGetError() and prints them.
 * Useful for debugging OpenGL calls.
 * @param location A string indicating where the check is being performed.
 */
void check_gl_error(const char* location);

//added 17/06/25
void renderer_push_textured_quad(Renderer* renderer, RendererPosition pos[4], RendererTexCoord tex[4], uint16_t clut, uint16_t tpage);


#endif // RENDERER_H