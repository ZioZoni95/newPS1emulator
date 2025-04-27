#include "renderer.h"
#include <stdio.h>
#include <stdlib.h> // For malloc, free, exit
#include <string.h> // For memcpy, memset

// Make sure GLEW/GLAD is included if not done in the header
// #define GLEW_STATIC
// #include <GL/glew.h>

// --- Helper: Check for OpenGL Errors ---
void check_gl_error(const char* location) {
    GLenum error;
    while ((error = glGetError()) != GL_NO_ERROR) {
        const char* error_str;
        switch (error) {
            case GL_INVALID_ENUM: error_str = "INVALID_ENUM"; break;
            case GL_INVALID_VALUE: error_str = "INVALID_VALUE"; break;
            case GL_INVALID_OPERATION: error_str = "INVALID_OPERATION"; break;
            case GL_STACK_OVERFLOW: error_str = "STACK_OVERFLOW"; break;
            case GL_STACK_UNDERFLOW: error_str = "STACK_UNDERFLOW"; break;
            case GL_OUT_OF_MEMORY: error_str = "OUT_OF_MEMORY"; break;
            case GL_INVALID_FRAMEBUFFER_OPERATION: error_str = "INVALID_FRAMEBUFFER_OPERATION"; break;
            default: error_str = "UNKNOWN_ERROR"; break;
        }
        fprintf(stderr, "OpenGL Error at %s: %s (0x%04x)\n", location, error_str, error);
    }
}


// --- GLSL Shader Source ---
// Based on Guide Section 5.3 and 5.4

// Vertex Shader: Transforms PSX VRAM coordinates and colors to OpenGL format.
const char* vertex_shader_source =
    "#version 330 core\n"
    // Input attributes from VBOs (locations match glVertexAttribIPointer setup)
    "layout (location = 0) in ivec2 vertex_position; // PSX VRAM coords (int16)\n"
    "layout (location = 1) in uvec3 vertex_color;    // PSX BGR color (uint8)\n"
    "\n"
    // Uniform: A single value passed to the shader for a batch of vertices
    "uniform ivec2 offset; // Drawing offset (applied to vertex_position)\n"
    "\n"
    // Output: Color passed to the fragment shader (interpolated)
    "out vec3 color;\n"
    "\n"
    "void main() {\n"
    // Apply the drawing offset
    "    ivec2 p = vertex_position + offset;\n"
    "\n"
    // Convert X coordinate from PSX VRAM (0..1023) to OpenGL NDC (-1.0..+1.0)
    "    float xpos = (float(p.x) / 512.0) - 1.0;\n"
    "\n"
    // Convert Y coordinate from PSX VRAM (0..511, top-to-bottom) to OpenGL NDC (-1.0..+1.0, bottom-to-top)
    "    float ypos = 1.0 - (float(p.y) / 256.0); // Flip Y axis\n"
    "\n"
    // Set the final position for this vertex. Z=0 (2D), W=1 (position).
    "    gl_Position = vec4(xpos, ypos, 0.0, 1.0);\n"
    "\n"
    // Convert color from 8-bit BGR to 32-bit float RGB [0.0..1.0]
    "    color = vec3(float(vertex_color.r) / 255.0,\n" // PSX BGR maps to vertex_color.r=B, .g=G, .b=R? No, use BGR struct. Corrected color mapping.
    "                   float(vertex_color.g) / 255.0,\n"
    "                   float(vertex_color.b) / 255.0);\n"
    "}\n";

// Fragment Shader: Determines the final color of each pixel fragment.
const char* fragment_shader_source =
    "#version 330 core\n"
    // Input: Color interpolated from the vertex shader outputs
    "in vec3 color;\n"
    "\n"
    // Output: Final color of the fragment (RGBA)
    "out vec4 frag_color;\n"
    "\n"
    "void main() {\n"
    // Set fragment color using the interpolated vertex color, alpha = 1.0 (opaque)
    "    frag_color = vec4(color, 1.0);\n"
    "}\n";


// --- OpenGL Helper Functions ---

// Compiles a shader from source code.
// Based on Guide Section 5.5
static GLuint compile_shader(const char* source, GLenum shader_type) {
    GLuint shader = glCreateShader(shader_type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);

    GLint status = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status == GL_FALSE) {
        GLint log_len = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_len);
        char* log_buffer = (char*)malloc(log_len + 1);
        if (log_buffer) {
            glGetShaderInfoLog(shader, log_len, NULL, log_buffer);
            log_buffer[log_len] = '\0';
            fprintf(stderr, "Shader Compilation Error (%s):\n%s\n",
                (shader_type == GL_VERTEX_SHADER) ? "Vertex" : "Fragment",
                log_buffer);
            free(log_buffer);
        } else {
            fprintf(stderr, "Shader Compilation Error (%s) - Failed to allocate log buffer\n",
                (shader_type == GL_VERTEX_SHADER) ? "Vertex" : "Fragment");
        }
        glDeleteShader(shader); // Delete the failed shader object
        check_gl_error("compile_shader (error path)");
        return 0; // Return 0 on failure
    }
    printf("Shader compiled successfully (Type: %s)\n", (shader_type == GL_VERTEX_SHADER) ? "Vertex" : "Fragment");
    check_gl_error("compile_shader (success path)");
    return shader;
}

// Links vertex and fragment shaders into a shader program.
// Based on Guide Section 5.5
static GLuint link_program(GLuint vertex_shader, GLuint fragment_shader) {
    GLuint program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);

    GLint status = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (status == GL_FALSE) {
        GLint log_len = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &log_len);
        char* log_buffer = (char*)malloc(log_len + 1);
        if (log_buffer) {
            glGetProgramInfoLog(program, log_len, NULL, log_buffer);
            log_buffer[log_len] = '\0';
            fprintf(stderr, "Shader Program Linking Error:\n%s\n", log_buffer);
            free(log_buffer);
        } else {
            fprintf(stderr, "Shader Program Linking Error - Failed to allocate log buffer\n");
        }
        glDeleteProgram(program); // Delete the failed program object
        // Shaders are still attached if linking failed, detach and delete them
        glDetachShader(program, vertex_shader);
        glDetachShader(program, fragment_shader);
        // Don't delete shaders here if they were passed in, caller might reuse
        check_gl_error("link_program (error path)");
        return 0; // Return 0 on failure
    }

    // Shaders can be detached and deleted after successful linking
    glDetachShader(program, vertex_shader);
    glDetachShader(program, fragment_shader);
    // Caller should delete the individual shaders if they are no longer needed
    // glDeleteShader(vertex_shader); // Optional: Delete here if not needed elsewhere
    // glDeleteShader(fragment_shader);

    printf("Shader program linked successfully (ID: %u)\n", program);
    check_gl_error("link_program (success path)");
    return program;
}


// --- Renderer Implementation ---

bool renderer_init(Renderer* renderer) {
    printf("Initializing Renderer...\n");
    renderer->initialized = false;
    renderer->vertex_count = 0;
    // Clear CPU-side buffers initially (optional but good practice)
    memset(renderer->positions_data, 0, sizeof(renderer->positions_data));
    memset(renderer->colors_data, 0, sizeof(renderer->colors_data));


    // Compile Shaders
    printf("Compiling vertex shader...\n");
    GLuint vs = compile_shader(vertex_shader_source, GL_VERTEX_SHADER);
    printf("Compiling fragment shader...\n");
    GLuint fs = compile_shader(fragment_shader_source, GL_FRAGMENT_SHADER);
    if (vs == 0 || fs == 0) {
        fprintf(stderr, "Renderer Init Failed: Shader compilation error.\n");
        if (vs != 0) glDeleteShader(vs); // Clean up if one succeeded
        if (fs != 0) glDeleteShader(fs);
        return false;
    }

    // Link Program
    printf("Linking shader program...\n");
    renderer->shader_program = link_program(vs, fs);
    // Delete individual shaders now that they are linked into the program
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (renderer->shader_program == 0) {
        fprintf(stderr, "Renderer Init Failed: Shader linking error.\n");
        return false;
    }
    check_gl_error("After linking program");


    // Get Uniform Location for the drawing offset
    renderer->uniform_offset_loc = glGetUniformLocation(renderer->shader_program, "offset");
    if (renderer->uniform_offset_loc < 0) {
        // This isn't fatal, but offset won't work. Check for GL errors too.
        fprintf(stderr, "Warning: Could not find uniform 'offset'. Draw offset will not work.\n");
        check_gl_error("glGetUniformLocation offset"); // Check if there was an error other than not found
    } else {
        printf("Found uniform 'offset' at location: %d\n", renderer->uniform_offset_loc);
        // Set initial offset to 0,0
        glUseProgram(renderer->shader_program); // Need to bind program to set uniform
        glUniform2i(renderer->uniform_offset_loc, 0, 0);
        glUseProgram(0); // Unbind program
    }
    check_gl_error("After getting/setting offset uniform");


    // --- Create Vertex Array Object (VAO) ---
    // VAO stores the links between VBOs and shader attributes.
    // Based on Guide Section 5.6
    printf("Creating VAO...\n");
    glGenVertexArrays(1, &renderer->vao);
    glBindVertexArray(renderer->vao); // Bind the VAO to make it active
    printf("VAO created (ID: %u) and bound.\n", renderer->vao);
    check_gl_error("After creating/binding VAO");


    // --- Create and Configure Position Vertex Buffer Object (VBO) ---
    printf("Creating Position VBO...\n");
    glGenBuffers(1, &renderer->position_buffer);
    glBindBuffer(GL_ARRAY_BUFFER, renderer->position_buffer); // Bind the new buffer to the GL_ARRAY_BUFFER target
    printf("Position VBO created (ID: %u) and bound.\n", renderer->position_buffer);

    // Allocate buffer storage on the GPU. We'll upload data later using glBufferSubData.
    // GL_DYNAMIC_DRAW is a hint that the data will be modified frequently.
    glBufferData(GL_ARRAY_BUFFER,               // Target buffer type
                 VERTEX_BUFFER_LEN * sizeof(RendererPosition), // Total buffer size in bytes
                 NULL,                         // Initial data (none)
                 GL_DYNAMIC_DRAW);             // Usage hint
    printf("Position VBO allocated %lu bytes.\n", VERTEX_BUFFER_LEN * sizeof(RendererPosition));
    check_gl_error("After position VBO glBufferData");

    // --- Link Position VBO to Shader Attribute ---
    // Get the location of the 'vertex_position' attribute in the shader (should be 0 as per layout qualifier)
    GLint pos_attrib_loc = glGetAttribLocation(renderer->shader_program, "vertex_position");
     if (pos_attrib_loc < 0) { fprintf(stderr, "Warning: Could not find attribute 'vertex_position'.\n"); }
     else { printf("Attribute 'vertex_position' found at location %d.\n", pos_attrib_loc); }

    // Enable this vertex attribute array
    glEnableVertexAttribArray(pos_attrib_loc); // Use the obtained location

    // Specify how OpenGL should interpret the data in the VBO for this attribute
    glVertexAttribIPointer(pos_attrib_loc,       // Attribute location in the shader
                           2,                  // Number of components per vertex (x, y)
                           GL_SHORT,           // Data type of each component (signed 16-bit int)
                           0, // Stride (0 = tightly packed) --> Or sizeof(RendererPosition)? Set 0 for now.
                           (void*)0);          // Offset of the first component in the buffer
    printf("Position VBO linked to vertex shader attribute location %d.\n", pos_attrib_loc);
    check_gl_error("After setting position attribute pointer");


    // --- Create and Configure Color Vertex Buffer Object (VBO) ---
    printf("Creating Color VBO...\n");
    glGenBuffers(1, &renderer->color_buffer);
    glBindBuffer(GL_ARRAY_BUFFER, renderer->color_buffer);
    printf("Color VBO created (ID: %u) and bound.\n", renderer->color_buffer);

    // Allocate storage
    glBufferData(GL_ARRAY_BUFFER, VERTEX_BUFFER_LEN * sizeof(RendererColor), NULL, GL_DYNAMIC_DRAW);
    printf("Color VBO allocated %lu bytes.\n", VERTEX_BUFFER_LEN * sizeof(RendererColor));
    check_gl_error("After color VBO glBufferData");

    // --- Link Color VBO to Shader Attribute ---
    GLint col_attrib_loc = glGetAttribLocation(renderer->shader_program, "vertex_color");
     if (col_attrib_loc < 0) { fprintf(stderr, "Warning: Could not find attribute 'vertex_color'.\n"); }
     else { printf("Attribute 'vertex_color' found at location %d.\n", col_attrib_loc); }

    glEnableVertexAttribArray(col_attrib_loc);

    // Specify data format for the color attribute
    glVertexAttribIPointer(col_attrib_loc,       // Attribute location
                           3,                  // Number of components (r, g, b)
                           GL_UNSIGNED_BYTE,   // Data type (unsigned 8-bit int)
                           0, // Stride (0 = tightly packed) --> Or sizeof(RendererColor)? Set 0 for now.
                           (void*)0);          // Offset
    printf("Color VBO linked to vertex shader attribute location %d.\n", col_attrib_loc);
    check_gl_error("After setting color attribute pointer");


    // --- Unbind ---
    glBindVertexArray(0); // Unbind the VAO
    glBindBuffer(GL_ARRAY_BUFFER, 0); // Unbind the VBO from the target
    printf("VAO and VBO unbound.\n");


    // --- Initial GL State ---
    // Set the default clear color to black
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    check_gl_error("After glClearColor");

    // Potentially enable depth testing if needed later
    // glEnable(GL_DEPTH_TEST);

    renderer->initialized = true;
    printf("Renderer Initialized Successfully.\n");
    return true;
}

// Buffers a triangle's vertex data
void renderer_push_triangle(Renderer* renderer, RendererPosition pos[3], RendererColor col[3]) {
    if (!renderer->initialized) {
        fprintf(stderr, "Renderer Error: push_triangle called before initialization.\n");
        return;
    }

    if (renderer->vertex_count + 3 > VERTEX_BUFFER_LEN) {
        printf("Renderer Info: Vertex buffer full (%u verts), forcing draw before push_triangle.\n", renderer->vertex_count);
        renderer_draw(renderer);
        if (renderer->vertex_count + 3 > VERTEX_BUFFER_LEN) {
             fprintf(stderr,"Renderer Error: Cannot push triangle, buffer still full after draw.\n");
             return;
        }
    }

    // Copy data to CPU-side buffers
    printf("Renderer: Buffering Triangle (Start Index: %u)\n", renderer->vertex_count);
    memcpy(&renderer->positions_data[renderer->vertex_count], pos, 3 * sizeof(RendererPosition));
    memcpy(&renderer->colors_data[renderer->vertex_count], col, 3 * sizeof(RendererColor));

    renderer->vertex_count += 3;
}

// Buffers a quad's vertex data (as two triangles)
void renderer_push_quad(Renderer* renderer, RendererPosition pos[4], RendererColor col[4]) {
     if (!renderer->initialized) {
        fprintf(stderr, "Renderer Error: push_quad called before initialization.\n");
        return;
     }

     if (renderer->vertex_count + 6 > VERTEX_BUFFER_LEN) {
        printf("Renderer Info: Vertex buffer full (%u verts), forcing draw before push_quad.\n", renderer->vertex_count);
        renderer_draw(renderer);
        if (renderer->vertex_count + 6 > VERTEX_BUFFER_LEN) {
            fprintf(stderr,"Renderer Error: Cannot push quad, buffer still full after draw.\n");
            return;
        }
     }

    printf("Renderer: Buffering Quad (Start Index: %u)\n", renderer->vertex_count);
    // Decompose quad into two triangles (using the order that seemed correct for the logo)
    // Triangle 1: V0, V1, V2
    renderer->positions_data[renderer->vertex_count + 0] = pos[0];
    renderer->colors_data[renderer->vertex_count + 0]    = col[0];
    renderer->positions_data[renderer->vertex_count + 1] = pos[1];
    renderer->colors_data[renderer->vertex_count + 1]    = col[1];
    renderer->positions_data[renderer->vertex_count + 2] = pos[2];
    renderer->colors_data[renderer->vertex_count + 2]    = col[2];

    // Triangle 2: V2, V1, V3 (or V0, V2, V3? Let's stick to guide's visual implication: 2,1,3 or 0,2,3)
    // Using 0, 2, 3 for simplicity unless visual bugs appear.
    renderer->positions_data[renderer->vertex_count + 3] = pos[0]; // V0
    renderer->colors_data[renderer->vertex_count + 3]    = col[0]; // C0
    renderer->positions_data[renderer->vertex_count + 4] = pos[2]; // V2
    renderer->colors_data[renderer->vertex_count + 4]    = col[2]; // C2
    renderer->positions_data[renderer->vertex_count + 5] = pos[3]; // V3
    renderer->colors_data[renderer->vertex_count + 5]    = col[3]; // C3

    renderer->vertex_count += 6;
}

// Uploads buffered data and performs the OpenGL draw call.
void renderer_draw(Renderer* renderer) {
     if (!renderer->initialized) {
         fprintf(stderr, "Renderer Error: Draw called before initialization.\n");
         return;
     }
     if (renderer->vertex_count == 0) {
        // printf("Renderer: Draw called with 0 vertices, skipping.\n"); // Optional info
        return; // Nothing to draw
     }

    printf("Renderer: Drawing %u vertices...\n", renderer->vertex_count);

    glUseProgram(renderer->shader_program); check_gl_error("draw - glUseProgram");
    glBindVertexArray(renderer->vao); check_gl_error("draw - glBindVertexArray");

    // --- Upload Buffered Vertex Data via glBufferSubData ---
    printf("  Uploading position data (%lu bytes)...\n", renderer->vertex_count * sizeof(RendererPosition));
    glBindBuffer(GL_ARRAY_BUFFER, renderer->position_buffer); check_gl_error("draw - glBindBuffer pos");
    glBufferSubData(GL_ARRAY_BUFFER, 0, renderer->vertex_count * sizeof(RendererPosition), renderer->positions_data);
    check_gl_error("draw - glBufferSubData pos");

    printf("  Uploading color data (%lu bytes)...\n", renderer->vertex_count * sizeof(RendererColor));
    glBindBuffer(GL_ARRAY_BUFFER, renderer->color_buffer); check_gl_error("draw - glBindBuffer col");
    glBufferSubData(GL_ARRAY_BUFFER, 0, renderer->vertex_count * sizeof(RendererColor), renderer->colors_data);
    check_gl_error("draw - glBufferSubData col");

    glBindBuffer(GL_ARRAY_BUFFER, 0); // Unbind GL_ARRAY_BUFFER target
    // ------------------------------------------------------

    // Draw the buffered primitives (interpreted as triangles)
    printf("  Issuing glDrawArrays...\n");
    glDrawArrays(GL_TRIANGLES,      // Mode: interpret vertices as triangles
                 0,                 // Starting index in the enabled arrays
                 renderer->vertex_count); // Number of vertices to render
    check_gl_error("draw - glDrawArrays");

    // --- Unbind ---
    glBindVertexArray(0);
    glUseProgram(0);

    // Reset the CPU buffer count for the next batch
    renderer->vertex_count = 0;
    printf("Renderer: Draw finished, vertex count reset.\n");
}

// Draws buffered primitives and requests buffer swap (swap happens in main loop)
void renderer_display(Renderer* renderer) {
    if (!renderer->initialized) return;
    printf("Renderer: Display requested.\n");
    // Draw any remaining buffered vertices
    renderer_draw(renderer);
    // Actual swap (SDL_GL_SwapWindow) happens in main.c/main loop
    // printf("Renderer: Display finished (swap should happen in main loop).\n");
}

// Sets the drawing offset uniform. Forces a draw first.
// Based on Guide Section 5.10
void renderer_set_draw_offset(Renderer* renderer, int16_t x, int16_t y) {
     if (!renderer->initialized) return;

     // Draw primitives with the *old* offset before changing it
     printf("Renderer: Setting Draw Offset (%d, %d), forcing draw first.\n", x, y);
     renderer_draw(renderer);

     // Bind the shader program to set the uniform
     glUseProgram(renderer->shader_program); check_gl_error("set_draw_offset - glUseProgram");
     // Update the uniform value
     glUniform2i(renderer->uniform_offset_loc, (GLint)x, (GLint)y);
     check_gl_error("set_draw_offset - glUniform2i");
     // Unbind the program
     glUseProgram(0);
}

// Cleans up OpenGL resources
void renderer_destroy(Renderer* renderer) {
    if (!renderer->initialized) return;
    printf("Destroying Renderer...\n");

    // Delete OpenGL objects
    printf("  Deleting shader program (ID: %u)\n", renderer->shader_program);
    glDeleteProgram(renderer->shader_program); check_gl_error("destroy - glDeleteProgram");

    printf("  Deleting VBOs (Pos: %u, Col: %u)\n", renderer->position_buffer, renderer->color_buffer);
    glDeleteBuffers(1, &renderer->position_buffer); check_gl_error("destroy - glDeleteBuffers pos");
    glDeleteBuffers(1, &renderer->color_buffer); check_gl_error("destroy - glDeleteBuffers col");
    // Add texcoord buffer deletion later if implemented

    printf("  Deleting VAO (ID: %u)\n", renderer->vao);
    glDeleteVertexArrays(1, &renderer->vao); check_gl_error("destroy - glDeleteVertexArrays");

    renderer->initialized = false;
    printf("Renderer Destroyed.\n");
}