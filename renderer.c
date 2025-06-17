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

// Vertex Shader: Now accepts texcoords and passes them to the fragment shader.
const char* vertex_shader_source =
    "#version 330 core\n"
    "layout (location = 0) in ivec2 vertex_position;\n"
    "layout (location = 1) in uvec3 vertex_color;\n"
    "layout (location = 2) in ivec2 vertex_texcoord; // This line was likely missing\n"
    "\n"
    "uniform ivec2 offset;\n"
    "\n"
    "out vec3 color;\n"
    "out vec2 texcoord;\n"
    "\n"
    "void main() {\n"
    "    ivec2 p = vertex_position + offset;\n"
    "    float xpos = (float(p.x) / 512.0) - 1.0;\n"
    "    float ypos = 1.0 - (float(p.y) / 256.0);\n"
    "    gl_Position = vec4(xpos, ypos, 0.0, 1.0);\n"
    "\n"
    "    color = vec3(float(vertex_color.r) / 255.0,\n"
    "                 float(vertex_color.g) / 255.0,\n"
    "                 float(vertex_color.b) / 255.0);\n"
    "\n"
    // Normalize texcoords from VRAM space (e.g., 0-1023) to texture space (0.0-1.0)
    "    texcoord = vec2(float(vertex_texcoord.x) / 1024.0, float(vertex_texcoord.y) / 512.0);\n"
    "}\n";

// Fragment Shader: Now uses a texture sampler to determine pixel color.
const char* fragment_shader_source =
    "#version 330 core\n"
    "in vec2 texcoord;\n"
    "\n"
    "uniform sampler2D vram_texture;\n"
    "\n"
    "out vec4 frag_color;\n"
    "\n"
    "void main() {\n"
    "    // 1. Sample the color (including alpha) from the VRAM texture\n"
    "    vec4 tex_color = texture(vram_texture, texcoord);\n"
    "\n"
    "    // 2. If the sampled color's alpha is 0, it means it's a transparent pixel.\n"
    "    // The 'discard' keyword tells the GPU to not draw this pixel at all.\n"
    "    if (tex_color.a == 0.0) {\n"
    "        discard;\n"
    "    }\n"
    "\n"
    "    // 3. For visible pixels, use the color from the texture (tex_color.rgb),\n"
    "    //    but force the final output alpha to be 1.0 (fully opaque).\n"
    "    frag_color = vec4(tex_color.rgb, 1.0);\n"
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
    
    // Clear all CPU-side buffers initially
    memset(renderer->positions_data, 0, sizeof(renderer->positions_data));
    memset(renderer->colors_data, 0, sizeof(renderer->colors_data));
    memset(renderer->texcoords_data, 0, sizeof(renderer->texcoords_data));

    // --- 1. Compile and Link Shaders ---
    printf("Compiling Shaders...\n");
    GLuint vs = compile_shader(vertex_shader_source, GL_VERTEX_SHADER);
    GLuint fs = compile_shader(fragment_shader_source, GL_FRAGMENT_SHADER);
    if (vs == 0 || fs == 0) {
        fprintf(stderr, "Renderer Init Failed: Shader compilation error.\n");
        if (vs != 0) glDeleteShader(vs);
        if (fs != 0) glDeleteShader(fs);
        return false;
    }

    printf("Linking shader program...\n");
    renderer->shader_program = link_program(vs, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (renderer->shader_program == 0) {
        fprintf(stderr, "Renderer Init Failed: Shader linking error.\n");
        return false;
    }
    check_gl_error("After linking program");

    // --- 2. Get Shader Uniform Locations ---
    // It's good practice to do this once after linking.
    glUseProgram(renderer->shader_program); // Bind program to get/set uniforms
    
    renderer->uniform_offset_loc = glGetUniformLocation(renderer->shader_program, "offset");
    if (renderer->uniform_offset_loc < 0) {
        fprintf(stderr, "Warning: Could not find uniform 'offset'.\n");
    } else {
        printf("Found uniform 'offset' at location: %d\n", renderer->uniform_offset_loc);
        glUniform2i(renderer->uniform_offset_loc, 0, 0); // Set initial offset
    }
    
    GLint vram_texture_loc = glGetUniformLocation(renderer->shader_program, "vram_texture");
    if (vram_texture_loc < 0) {
         fprintf(stderr, "Warning: Could not find uniform 'vram_texture'.\n");
    } else {
        printf("Found uniform 'vram_texture' at location: %d\n", vram_texture_loc);
        glUniform1i(vram_texture_loc, 0); // Tell shader sampler to use texture unit 0
    }

    glUseProgram(0); // Unbind program
    check_gl_error("After getting/setting uniforms");

    // --- 3. Create Vertex Array Object (VAO) ---
    // The VAO MUST be created and bound before configuring VBOs and attribute pointers.
    printf("Creating VAO...\n");
    glGenVertexArrays(1, &renderer->vao);
    glBindVertexArray(renderer->vao);
    printf("VAO created (ID: %u) and bound.\n", renderer->vao);
    check_gl_error("After creating/binding VAO");

    // --- 4. Create and Configure ALL Vertex Buffer Objects (VBOs) ---

    // Position VBO (Location 0)
    printf("Creating Position VBO...\n");
    glGenBuffers(1, &renderer->position_buffer);
    glBindBuffer(GL_ARRAY_BUFFER, renderer->position_buffer);
    glBufferData(GL_ARRAY_BUFFER, VERTEX_BUFFER_LEN * sizeof(RendererPosition), NULL, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribIPointer(0, 2, GL_SHORT, 0, (void*)0);
    printf("Position VBO configured for attribute location 0.\n");
    check_gl_error("After configuring Position VBO");

    // Color VBO (Location 1)
    printf("Creating Color VBO...\n");
    glGenBuffers(1, &renderer->color_buffer);
    glBindBuffer(GL_ARRAY_BUFFER, renderer->color_buffer);
    glBufferData(GL_ARRAY_BUFFER, VERTEX_BUFFER_LEN * sizeof(RendererColor), NULL, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(1);
    glVertexAttribIPointer(1, 3, GL_UNSIGNED_BYTE, 0, (void*)0);
    printf("Color VBO configured for attribute location 1.\n");
    check_gl_error("After configuring Color VBO");

    // TexCoord VBO (Location 2)
    printf("Creating TexCoord VBO...\n");
    glGenBuffers(1, &renderer->texcoord_buffer);
    glBindBuffer(GL_ARRAY_BUFFER, renderer->texcoord_buffer);
    glBufferData(GL_ARRAY_BUFFER, VERTEX_BUFFER_LEN * sizeof(RendererTexCoord), NULL, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(2);
    glVertexAttribIPointer(2, 2, GL_SHORT, 0, (void*)0);
    printf("TexCoord VBO configured for attribute location 2.\n");
    check_gl_error("After configuring TexCoord VBO");

    // --- 5. Create VRAM Texture Object ---
    printf("Creating VRAM texture object...\n");
    glGenTextures(1, &renderer->vram_texture_id);
    glBindTexture(GL_TEXTURE_2D, renderer->vram_texture_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB5_A1, 1024, 512, 0, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, NULL);
    printf("VRAM texture created (ID: %u).\n", renderer->vram_texture_id);
    check_gl_error("After creating VRAM texture");

    // --- 6. Unbind objects to clean up state ---
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    printf("VAO and VBOs unbound.\n");

    // --- 7. Set Initial GL State ---
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    check_gl_error("After glClearColor");

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
        // Nothing to draw
        return;
    }

    printf("Renderer: Drawing %u vertices...\n", renderer->vertex_count);

    glUseProgram(renderer->shader_program); check_gl_error("draw - glUseProgram");
    glBindVertexArray(renderer->vao); check_gl_error("draw - glBindVertexArray");

    // --- Upload Buffered Vertex Data via glBufferSubData ---
    
    // Upload position data (no change)
    printf("  Uploading position data (%lu bytes)...\n", renderer->vertex_count * sizeof(RendererPosition));
    glBindBuffer(GL_ARRAY_BUFFER, renderer->position_buffer); check_gl_error("draw - glBindBuffer pos");
    glBufferSubData(GL_ARRAY_BUFFER, 0, renderer->vertex_count * sizeof(RendererPosition), renderer->positions_data);
    check_gl_error("draw - glBufferSubData pos");

    // Upload color data (no change)
    printf("  Uploading color data (%lu bytes)...\n", renderer->vertex_count * sizeof(RendererColor));
    glBindBuffer(GL_ARRAY_BUFFER, renderer->color_buffer); check_gl_error("draw - glBindBuffer col");
    glBufferSubData(GL_ARRAY_BUFFER, 0, renderer->vertex_count * sizeof(RendererColor), renderer->colors_data);
    check_gl_error("draw - glBufferSubData col");


    // --- PROPOSED MODIFICATION START ---
    // Add this block to upload the new texture coordinate data.
    // It's identical to the blocks for position and color.
    printf("  Uploading texcoord data (%lu bytes)...\n", renderer->vertex_count * sizeof(RendererTexCoord));
    glBindBuffer(GL_ARRAY_BUFFER, renderer->texcoord_buffer); check_gl_error("draw - glBindBuffer tex");
    glBufferSubData(GL_ARRAY_BUFFER, 0, renderer->vertex_count * sizeof(RendererTexCoord), renderer->texcoords_data);
    check_gl_error("draw - glBufferSubData tex");
    // --- PROPOSED MODIFICATION END ---


    glBindBuffer(GL_ARRAY_BUFFER, 0); // Unbind GL_ARRAY_BUFFER target
    // ------------------------------------------------------

    // Draw the buffered primitives (interpreted as triangles)
    printf("  Issuing glDrawArrays...\n");
    glDrawArrays(GL_TRIANGLES, 0, renderer->vertex_count);
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

// --- NEW FUNCTION IMPLEMENTATION ---
// This is the implementation of the new function we added to the header.
void renderer_push_textured_quad(Renderer* renderer, RendererPosition pos[4], RendererTexCoord tex[4], uint16_t clut, uint16_t tpage) {
    if (!renderer->initialized) {
        fprintf(stderr, "Renderer Error: push_textured_quad called before initialization.\n");
        return;
    }

    if (renderer->vertex_count + 6 > VERTEX_BUFFER_LEN) {
        printf("Renderer Info: Vertex buffer full, forcing draw before push_textured_quad.\n");
        renderer_draw(renderer);
    }

    // NOTE: For a more advanced renderer, you would check if 'clut' or 'tpage'
    // has changed and force a draw. For now, we will handle it simply.

    printf("Renderer: Buffering Textured Quad (Start Index: %u)\n", renderer->vertex_count);

    // Decompose quad into two triangles (0, 1, 2 and 0, 2, 3)
    // Triangle 1
    renderer->positions_data[renderer->vertex_count + 0] = pos[0];
    renderer->texcoords_data[renderer->vertex_count + 0] = tex[0];
    renderer->positions_data[renderer->vertex_count + 1] = pos[1];
    renderer->texcoords_data[renderer->vertex_count + 1] = tex[1];
    renderer->positions_data[renderer->vertex_count + 2] = pos[2];
    renderer->texcoords_data[renderer->vertex_count + 2] = tex[2];

    // Triangle 2
    renderer->positions_data[renderer->vertex_count + 3] = pos[0];
    renderer->texcoords_data[renderer->vertex_count + 3] = tex[0];
    renderer->positions_data[renderer->vertex_count + 4] = pos[2];
    renderer->texcoords_data[renderer->vertex_count + 4] = tex[2];
    renderer->positions_data[renderer->vertex_count + 5] = pos[3];
    renderer->texcoords_data[renderer->vertex_count + 5] = tex[3];

    // For now, we push placeholder colors since the color attribute is still active.
    // In a more advanced shader, we would disable the color attribute for textured draws.
    for (int i=0; i<6; ++i) {
        renderer->colors_data[renderer->vertex_count + i] = (RendererColor){128, 128, 128};
    }

    renderer->vertex_count += 6;
}
// --- END NEW FUNCTION ---
