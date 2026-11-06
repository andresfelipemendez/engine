//------------------------------------------------------------------------------
//  game.c -- unity root of the game library (out/game.dll / game.so).
//  Hot reloaded by the host; all persistent state lives in game_state_t at
//  the start of the host-owned memory block.
//
//  glTF model viewer for the Kenney space-kit (assets/space-kit/*.glb).
//  Left/Right: previous/next model, mouse drag: orbit, wheel: zoom.
//------------------------------------------------------------------------------
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_glue.h"
#include "sokol_debugtext.h"
#include "cgltf.h"
#include "dbgui.h"
#include "game_api.h"
#include "shaders/model.glsl.h"
#include "shader_reload.c"

#define ASSET_DIR "assets/space-kit/"
#define MAX_MODELS (256)
#define MAX_NAME (64)
#define SHADER_PATH "src/shaders/model.glsl"
#define WATCH_INTERVAL (30)   // frames between file checks (~2x per second)
#if !defined(GAME_STATE_HASH)
#define GAME_STATE_HASH 0ull  // build.c passes a hash of the game_state_t definition
#endif

//== math (column-major, m[col*4+row], matches GLSL) ===========================
typedef struct { float m[16]; } mat4_t;

static mat4_t mat4_mul(mat4_t a, mat4_t b) {
    mat4_t r;
    for (int c = 0; c < 4; c++) {
        for (int row = 0; row < 4; row++) {
            float s = 0.0f;
            for (int k = 0; k < 4; k++) {
                s += a.m[k*4 + row] * b.m[c*4 + k];
            }
            r.m[c*4 + row] = s;
        }
    }
    return r;
}

static mat4_t mat4_perspective(float fovy_rad, float aspect, float znear, float zfar) {
    mat4_t r = {0};
    float f = 1.0f / tanf(fovy_rad * 0.5f);
    r.m[0] = f / aspect;
    r.m[5] = f;
    r.m[10] = (zfar + znear) / (znear - zfar);
    r.m[11] = -1.0f;
    r.m[14] = (2.0f * zfar * znear) / (znear - zfar);
    return r;
}

static mat4_t mat4_lookat(const float eye[3], const float center[3]) {
    float f[3] = { center[0]-eye[0], center[1]-eye[1], center[2]-eye[2] };
    float fl = sqrtf(f[0]*f[0] + f[1]*f[1] + f[2]*f[2]);
    f[0] /= fl; f[1] /= fl; f[2] /= fl;
    // s = normalize(cross(f, up)), up = (0,1,0)
    float s[3] = { -f[2], 0.0f, f[0] };
    float sl = sqrtf(s[0]*s[0] + s[2]*s[2]);
    s[0] /= sl; s[2] /= sl;
    // u = cross(s, f)
    float u[3] = { s[1]*f[2] - s[2]*f[1], s[2]*f[0] - s[0]*f[2], s[0]*f[1] - s[1]*f[0] };
    mat4_t r = {{
        s[0], u[0], -f[0], 0.0f,
        s[1], u[1], -f[1], 0.0f,
        s[2], u[2], -f[2], 0.0f,
        -(s[0]*eye[0] + s[1]*eye[1] + s[2]*eye[2]),
        -(u[0]*eye[0] + u[1]*eye[1] + u[2]*eye[2]),
         (f[0]*eye[0] + f[1]*eye[1] + f[2]*eye[2]),
        1.0f,
    }};
    return r;
}

static mat4_t mat4_identity(void) {
    return (mat4_t){{ 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 }};
}

//== glTF loading ==============================================================
typedef struct {
    float pos[3];
    float normal[3];
    float color[4];
} vertex_t;

typedef struct {
    sg_buffer vbuf;
    sg_buffer ibuf;
    int num_indices;
    float center[3];
    float radius;
} gpu_model_t;

// Loads a .glb, flattens all mesh nodes into one vertex/index buffer with the
// node world transform and material base color baked into the vertices.
static bool load_model(const char* path, gpu_model_t* out) {
    cgltf_options opts = {0};
    cgltf_data* data = NULL;
    if (cgltf_parse_file(&opts, path, &data) != cgltf_result_success) {
        return false;
    }
    if (cgltf_load_buffers(&opts, data, path) != cgltf_result_success) {
        cgltf_free(data);
        return false;
    }

    // count totals first so we allocate once
    size_t total_verts = 0, total_indices = 0;
    for (size_t ni = 0; ni < data->nodes_count; ni++) {
        const cgltf_mesh* mesh = data->nodes[ni].mesh;
        if (!mesh) continue;
        for (size_t pi = 0; pi < mesh->primitives_count; pi++) {
            const cgltf_primitive* prim = &mesh->primitives[pi];
            if ((prim->type != cgltf_primitive_type_triangles) || !prim->indices) continue;
            for (size_t ai = 0; ai < prim->attributes_count; ai++) {
                if (prim->attributes[ai].type == cgltf_attribute_type_position) {
                    total_verts += prim->attributes[ai].data->count;
                }
            }
            total_indices += prim->indices->count;
        }
    }
    if ((total_verts == 0) || (total_indices == 0)) {
        cgltf_free(data);
        return false;
    }

    vertex_t* verts = calloc(total_verts, sizeof(vertex_t));
    uint32_t* indices = malloc(total_indices * sizeof(uint32_t));
    size_t vbase = 0, icur = 0;
    float bmin[3] = {  1e30f,  1e30f,  1e30f };
    float bmax[3] = { -1e30f, -1e30f, -1e30f };

    for (size_t ni = 0; ni < data->nodes_count; ni++) {
        const cgltf_node* node = &data->nodes[ni];
        if (!node->mesh) continue;
        float world[16];
        cgltf_node_transform_world(node, world);

        for (size_t pi = 0; pi < node->mesh->primitives_count; pi++) {
            const cgltf_primitive* prim = &node->mesh->primitives[pi];
            if ((prim->type != cgltf_primitive_type_triangles) || !prim->indices) continue;

            const cgltf_accessor* pos_acc = NULL;
            const cgltf_accessor* nrm_acc = NULL;
            for (size_t ai = 0; ai < prim->attributes_count; ai++) {
                if (prim->attributes[ai].type == cgltf_attribute_type_position) pos_acc = prim->attributes[ai].data;
                if (prim->attributes[ai].type == cgltf_attribute_type_normal)   nrm_acc = prim->attributes[ai].data;
            }
            if (!pos_acc) continue;

            float color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
            if (prim->material && prim->material->has_pbr_metallic_roughness) {
                memcpy(color, prim->material->pbr_metallic_roughness.base_color_factor, sizeof(color));
            }

            for (size_t vi = 0; vi < pos_acc->count; vi++) {
                vertex_t* v = &verts[vbase + vi];
                float p[3] = {0}, n[3] = { 0.0f, 1.0f, 0.0f };
                cgltf_accessor_read_float(pos_acc, vi, p, 3);
                if (nrm_acc) cgltf_accessor_read_float(nrm_acc, vi, n, 3);
                for (int r = 0; r < 3; r++) {
                    // position: full transform; normal: rotation/scale only (renormalized in shader)
                    v->pos[r]    = world[0*4+r]*p[0] + world[1*4+r]*p[1] + world[2*4+r]*p[2] + world[3*4+r];
                    v->normal[r] = world[0*4+r]*n[0] + world[1*4+r]*n[1] + world[2*4+r]*n[2];
                    if (v->pos[r] < bmin[r]) bmin[r] = v->pos[r];
                    if (v->pos[r] > bmax[r]) bmax[r] = v->pos[r];
                }
                memcpy(v->color, color, sizeof(color));
            }
            for (size_t ii = 0; ii < prim->indices->count; ii++) {
                indices[icur++] = (uint32_t)(vbase + cgltf_accessor_read_index(prim->indices, ii));
            }
            vbase += pos_acc->count;
        }
    }
    cgltf_free(data);

    out->vbuf = sg_make_buffer(&(sg_buffer_desc){
        .data = { verts, vbase * sizeof(vertex_t) },
        .label = "model-vertices",
    });
    out->ibuf = sg_make_buffer(&(sg_buffer_desc){
        .usage.index_buffer = true,
        .data = { indices, icur * sizeof(uint32_t) },
        .label = "model-indices",
    });
    out->num_indices = (int)icur;
    float ext2 = 0.0f;
    for (int r = 0; r < 3; r++) {
        out->center[r] = 0.5f * (bmin[r] + bmax[r]);
        float e = 0.5f * (bmax[r] - bmin[r]);
        ext2 += e * e;
    }
    out->radius = sqrtf(ext2);
    free(verts);
    free(indices);
    return true;
}

static void unload_model(gpu_model_t* m) {
    sg_destroy_buffer(m->vbuf);
    sg_destroy_buffer(m->ibuf);
    *m = (gpu_model_t){0};
}

//== game state (lives in host memory, survives reloads) =======================
#if defined(GAME_TEST_COMPILE_ERROR)
#error "GAME_TEST_COMPILE_ERROR: deliberate build failure for tests/hot_reload_test.c"
#endif

typedef struct {
#if defined(GAME_TEST_STATE_PAD)
    char test_pad[GAME_TEST_STATE_PAD];  // changes state_size, for tests/hot_reload_test.c
#endif
    sg_shader shader;
    sg_pipeline pip;
    int64_t shader_mtime;
    int64_t model_mtime;
    sg_pass_action pass_action;
    char names[MAX_MODELS][MAX_NAME];
    int num_models;
    int cur;
    bool loaded;
    gpu_model_t model;
    float yaw, pitch, zoom;
    bool dragging;
    int frame;
} game_state_t;

static game_state_t* get_state(game_memory_t* mem) {
    return (game_state_t*)mem->base;
}

static void select_model(game_state_t* s, const platform_api_t* p, int index) {
    if (s->num_models == 0) return;
    s->cur = (index % s->num_models + s->num_models) % s->num_models;
    if (s->loaded) {
        unload_model(&s->model);
        s->loaded = false;
    }
    char path[256];
    snprintf(path, sizeof(path), ASSET_DIR "%s", s->names[s->cur]);
    s->model_mtime = p->file_mtime(path);
    s->loaded = load_model(path, &s->model);
    if (!s->loaded) {
        p->log("[model] failed to load %s", path);
    }
}

static int find_model(const game_state_t* s, const char* name) {
    for (int i = 0; i < s->num_models; i++) {
        if (0 == strcmp(s->names[i], name)) return i;
    }
    return -1;
}

// refreshes the model list (selection stays on the same file name) and
// reloads the current model when its file changed; a failed reload keeps
// the model already on screen
static void poll_models(game_state_t* s, const platform_api_t* p) {
    char names[MAX_MODELS][MAX_NAME];
    int count = p->list_dir(ASSET_DIR, ".glb", names, MAX_MODELS);
    bool changed = (count != s->num_models);
    for (int i = 0; !changed && (i < count); i++) {
        changed = (0 != strcmp(names[i], s->names[i]));
    }
    if (changed) {
        char current[MAX_NAME] = "";
        if (s->num_models > 0) {
            strcpy(current, s->names[s->cur]);
        }
        for (int i = 0; i < count; i++) {
            strcpy(s->names[i], names[i]);
        }
        s->num_models = count;
        p->log("[model] list %d", count);
        int index = find_model(s, current);
        if (index >= 0) {
            s->cur = index;     // same file, already loaded
        } else if (count > 0) {
            select_model(s, p, (s->cur < count) ? s->cur : count - 1);
        } else if (s->loaded) {
            unload_model(&s->model);
            s->loaded = false;
        }
        if (count > 0) {
            p->log("[model] selected %s", s->names[s->cur]);
        }
    }

    if (s->num_models == 0) return;
    char path[256];
    snprintf(path, sizeof(path), ASSET_DIR "%s", s->names[s->cur]);
    int64_t mtime = p->file_mtime(path);
    if ((mtime == -1) || (mtime == s->model_mtime)) return;
    s->model_mtime = mtime;
    gpu_model_t fresh;
    if (!load_model(path, &fresh)) {
        p->log("[model] failed to reload %s, keeping the old one", s->names[s->cur]);
        return;
    }
    if (s->loaded) {
        unload_model(&s->model);
    }
    s->model = fresh;
    s->loaded = true;
    p->log("[model] reloaded %s", s->names[s->cur]);
}

static sg_pipeline make_pipeline(sg_shader shd) {
    return sg_make_pipeline(&(sg_pipeline_desc){
        .shader = shd,
        .layout = {
            .attrs = {
                [ATTR_model_position].format = SG_VERTEXFORMAT_FLOAT3,
                [ATTR_model_normal].format   = SG_VERTEXFORMAT_FLOAT3,
                [ATTR_model_color0].format   = SG_VERTEXFORMAT_FLOAT4,
            }
        },
        .index_type = SG_INDEXTYPE_UINT32,
        .cull_mode = SG_CULLMODE_BACK,
        .face_winding = SG_FACEWINDING_CCW,  // glTF front faces are CCW
        .depth = {
            .compare = SG_COMPAREFUNC_LESS_EQUAL,
            .write_enabled = true,
        },
        .label = "model-pipeline",
    });
}

// recompiles the model shader when its source changed; on any failure the
// current shader + pipeline stay in use
static void poll_shader(game_state_t* s, const platform_api_t* p) {
    int64_t mtime = p->file_mtime(SHADER_PATH);
    if ((mtime == -1) || (mtime == s->shader_mtime)) return;
    s->shader_mtime = mtime;

    char err[4096];
    sg_shader shd = shader_reload(p, SHADER_PATH, "model", model_shader_desc(sg_query_backend()), err, sizeof(err));
    if (shd.id == SG_INVALID_ID) {
        p->log("[shader] error " SHADER_PATH ":\n%s", err);
        return;
    }
    sg_pipeline pip = make_pipeline(shd);
    if (sg_query_pipeline_state(pip) != SG_RESOURCESTATE_VALID) {
        p->log("[shader] error " SHADER_PATH ":\npipeline creation failed (see sokol log)");
        sg_destroy_pipeline(pip);
        sg_destroy_shader(shd);
        return;
    }
    sg_destroy_pipeline(s->pip);
    sg_destroy_shader(s->shader);
    s->shader = shd;
    s->pip = pip;
    p->log("[shader] reloaded " SHADER_PATH);
}

//== game API ==================================================================
static void game_init(game_memory_t* mem, const platform_api_t* p) {
    game_state_t* s = get_state(mem);
    s->yaw = 0.6f;
    s->pitch = 0.4f;
    s->zoom = 1.0f;
    s->shader = sg_make_shader(model_shader_desc(sg_query_backend()));
    s->pip = make_pipeline(s->shader);
    s->shader_mtime = p->file_mtime(SHADER_PATH);
    s->pass_action = (sg_pass_action){
        .colors[0] = { .load_action = SG_LOADACTION_CLEAR, .clear_value = { 0.08f, 0.09f, 0.12f, 1.0f } },
    };
    s->num_models = p->list_dir(ASSET_DIR, ".glb", s->names, MAX_MODELS);
    if (s->num_models == 0) {
        p->log("[game] no .glb files found in " ASSET_DIR " (run from the project root)");
    }
    select_model(s, p, 0);
    p->log("[game] init");
}

static void game_reloaded(game_memory_t* mem, const platform_api_t* p) {
    p->log("[game] reloaded frame=%d", get_state(mem)->frame);
}

static void game_frame(game_memory_t* mem, const platform_api_t* p) {
    game_state_t* s = get_state(mem);
    s->frame++;
    if (p->verbose && ((s->frame % 60) == 0)) {
        p->log("[game] frame=%d", s->frame);
    }
    if ((s->frame % WATCH_INTERVAL) == 0) {
        poll_shader(s, p);
        poll_models(s, p);
    }

    _dbgui_update();
    const float w = sapp_widthf(), h = sapp_heightf();
    sg_begin_pass(&(sg_pass){ .action = s->pass_action, .swapchain = sglue_swapchain() });
    if (s->loaded) {
        const gpu_model_t* m = &s->model;
        float dist = m->radius * 2.5f * s->zoom;
        float eye[3] = {
            m->center[0] + dist * cosf(s->pitch) * sinf(s->yaw),
            m->center[1] + dist * sinf(s->pitch),
            m->center[2] + dist * cosf(s->pitch) * cosf(s->yaw),
        };
        mat4_t proj = mat4_perspective(1.0f, w / h, m->radius * 0.05f, dist + m->radius * 4.0f);
        mat4_t view = mat4_lookat(eye, m->center);
        mat4_t model = mat4_identity();
        mat4_t mvp = mat4_mul(proj, mat4_mul(view, model));

        vs_params_t params;
        memcpy(params.mvp, mvp.m, sizeof(params.mvp));
        memcpy(params.model, model.m, sizeof(params.model));

        sg_apply_pipeline(s->pip);
        sg_apply_bindings(&(sg_bindings){
            .vertex_buffers[0] = m->vbuf,
            .index_buffer = m->ibuf,
        });
        sg_apply_uniforms(UB_vs_params, &SG_RANGE(params));
        sg_draw(0, m->num_indices, 1);
    }

    // overlay text (row 2+ to stay below the imgui menu bar)
    sdtx_canvas(w * 0.5f, h * 0.5f);
    sdtx_origin(1.0f, 2.0f);
    if (s->num_models > 0) {
        sdtx_printf("%s  (%d/%d)\n", s->names[s->cur], s->cur + 1, s->num_models);
    }
    sdtx_puts("left/right: model  drag: orbit  wheel: zoom");
    sdtx_draw();

    _dbgui_draw();
    sg_end_pass();
    sg_commit();
}

static void game_event(game_memory_t* mem, const platform_api_t* p, const sapp_event* e) {
    game_state_t* s = get_state(mem);
    if (_dbgui_event_with_retval(e)) {
        return;  // imgui wants this input
    }
    switch (e->type) {
        case SAPP_EVENTTYPE_KEY_DOWN:
            if (e->key_code == SAPP_KEYCODE_RIGHT) select_model(s, p, s->cur + 1);
            if (e->key_code == SAPP_KEYCODE_LEFT)  select_model(s, p, s->cur - 1);
            if (e->key_code == SAPP_KEYCODE_ESCAPE) sapp_request_quit();
            break;
        case SAPP_EVENTTYPE_MOUSE_DOWN:
            if (e->mouse_button == SAPP_MOUSEBUTTON_LEFT) s->dragging = true;
            break;
        case SAPP_EVENTTYPE_MOUSE_UP:
            if (e->mouse_button == SAPP_MOUSEBUTTON_LEFT) s->dragging = false;
            break;
        case SAPP_EVENTTYPE_MOUSE_MOVE:
            if (s->dragging) {
                s->yaw -= e->mouse_dx * 0.01f;
                s->pitch += e->mouse_dy * 0.01f;
                if (s->pitch >  1.5f) s->pitch =  1.5f;
                if (s->pitch < -1.5f) s->pitch = -1.5f;
            }
            break;
        case SAPP_EVENTTYPE_MOUSE_SCROLL:
            s->zoom *= (e->scroll_y > 0.0f) ? 0.9f : 1.1f;
            if (s->zoom < 0.2f) s->zoom = 0.2f;
            if (s->zoom > 5.0f) s->zoom = 5.0f;
            break;
        default:
            break;
    }
}

static void game_shutdown(game_memory_t* mem, const platform_api_t* p) {
    (void)p;
    game_state_t* s = get_state(mem);
    if (s->loaded) {
        unload_model(&s->model);
        s->loaded = false;
    }
    sg_destroy_pipeline(s->pip);
    sg_destroy_shader(s->shader);
}

GAME_EXPORT const game_api_t* game_get_api(void) {
    static const game_api_t api = {
        .version = GAME_API_VERSION,
        .state_size = sizeof(game_state_t),
        .state_layout = GAME_STATE_HASH,
        .init = game_init,
        .reloaded = game_reloaded,
        .frame = game_frame,
        .event = game_event,
        .shutdown = game_shutdown,
    };
    return &api;
}
