//------------------------------------------------------------------------------
//  shader_reload.c -- runtime shader recompilation (included by game.c).
//
//  Runs sokol-shdc with -f bare for the current backend, then rebuilds the
//  sg_shader from the build-time desc with only the shader source swapped.
//  The desc's attributes / uniform layout come from the generated header, so
//  interface changes (new inputs/uniforms) still need ./build.
//------------------------------------------------------------------------------

#if defined(_WIN32)
#include "platform/win32.h"
#elif defined(__APPLE__)
#include "platform/macos.h"
#else
#include "platform/linux.h"
#endif

// reads a whole text file into a malloc'd NUL-terminated buffer, or NULL
static char* read_text_file(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* buf = (size >= 0) ? malloc((size_t)size + 1) : NULL;
    if (buf) {
        size_t n = fread(buf, 1, (size_t)size, f);
        buf[n] = 0;
    }
    fclose(f);
    return buf;
}

// "src/shaders/model.glsl" -> "model"
static void file_stem(const char* path, char* out, size_t out_size) {
    const char* base = strrchr(path, '/');
    base = base ? base + 1 : path;
    const char* dot = strrchr(base, '.');
    size_t n = dot ? (size_t)(dot - base) : strlen(base);
    if (n >= out_size) n = out_size - 1;
    memcpy(out, base, n);
    out[n] = 0;
}

// compiles glsl_path for the current backend; returns a valid sg_shader or
// SG_INVALID_ID with the error text in err. `program` is the @program name.
static sg_shader shader_reload(const platform_api_t* p, const char* glsl_path, const char* program,
                               const sg_shader_desc* base, char* err, size_t err_size) {
    const char* lang;
    const char* ext;
    switch (sg_query_backend()) {
        case SG_BACKEND_D3D11:  lang = "hlsl5";   ext = "hlsl"; break;
        case SG_BACKEND_GLCORE: lang = "glsl430"; ext = "glsl"; break;
        case SG_BACKEND_METAL_MACOS: lang = "metal_macos"; ext = "metal"; break;
        default:
            snprintf(err, err_size, "backend not supported by shader reload");
            return (sg_shader){ SG_INVALID_ID };
    }

    // output straight into out/ (no mkdir needed): out/shader_<stem>_<program>_<lang>_{vertex,fragment}.<ext>
    char stem[64], out_prefix[128];
    file_stem(glsl_path, stem, sizeof(stem));
    snprintf(out_prefix, sizeof(out_prefix), "out/shader_%s", stem);
    const char* argv[] = { SHDC_PATH, "-i", glsl_path, "-o", out_prefix, "-l", lang, "-f", "bare", NULL };
    int code = p->run_process(argv, err, err_size);
    if (code != 0) {
        if (err[0] == 0) snprintf(err, err_size, "%s exited with code %d", SHDC_PATH, code);
        return (sg_shader){ SG_INVALID_ID };
    }

    char vs_path[256], fs_path[256];
    snprintf(vs_path, sizeof(vs_path), "%s_%s_%s_vertex.%s", out_prefix, program, lang, ext);
    snprintf(fs_path, sizeof(fs_path), "%s_%s_%s_fragment.%s", out_prefix, program, lang, ext);
    char* vs = read_text_file(vs_path);
    char* fs = read_text_file(fs_path);
    if (!vs || !fs) {
        snprintf(err, err_size, "missing shdc output %s / %s", vs_path, fs_path);
        free(vs); free(fs);
        return (sg_shader){ SG_INVALID_ID };
    }

    // sokol compiles the source inside sg_make_shader and keeps no pointer to it
    sg_shader_desc desc = *base;
    desc.vertex_func.source = vs;
    desc.fragment_func.source = fs;
    sg_shader shd = sg_make_shader(&desc);
    free(vs); free(fs);
    if (sg_query_shader_state(shd) != SG_RESOURCESTATE_VALID) {
        sg_destroy_shader(shd);
        snprintf(err, err_size, "sg_make_shader failed (see sokol log)");
        return (sg_shader){ SG_INVALID_ID };
    }
    err[0] = 0;
    return shd;
}
