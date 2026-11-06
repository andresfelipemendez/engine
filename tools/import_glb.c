// Copy .glb models into the project, fixing exporter quirks cgltf rejects.
//
//     ./build tools                                     (builds out/import_glb[.exe])
//     out/import_glb <source_dir> <dest_dir>
//     e.g. out/import_glb "C:/Users/andres/Downloads/kenney_space-kit/Models/GLTF format" assets/space-kit
//
// Fix applied: UniGLTF (Kenney kits) lists a child node as a scene root, e.g.
// node 0 "tmpParent" has children [1] while scenes[0].nodes == [1]. The glTF
// spec requires scene roots to have no parent, so cgltf_parse fails with
// cgltf_result_invalid_gltf. Each offending scenes[i].nodes array is replaced
// by the list of parentless nodes; every other byte of the JSON is kept as is.
#define NOB_IMPLEMENTATION
#define NOB_STRIP_PREFIX
#include "../third_party/nob.h"

// cgltf embeds jsmn (a JSON tokenizer) as static functions in its implementation
#define CGLTF_IMPLEMENTATION
#include "../third_party/cgltf/cgltf.h"

#define GLB_MAGIC   0x46546C67u   // "glTF"
#define CHUNK_JSON  0x4E4F534Au   // "JSON"
#define MAX_NODES   4096

static uint32_t read_u32(const char *p) {
    const unsigned char *u = (const unsigned char *)p;
    return (uint32_t)u[0] | ((uint32_t)u[1] << 8) | ((uint32_t)u[2] << 16) | ((uint32_t)u[3] << 24);
}

static void append_u32(String_Builder *sb, uint32_t v) {
    char b[4] = { (char)(v & 0xFF), (char)((v >> 8) & 0xFF), (char)((v >> 16) & 0xFF), (char)((v >> 24) & 0xFF) };
    sb_append_buf(sb, b, 4);
}

// index of the token after the subtree rooted at token i
static int skip(const jsmntok_t *t, int i) {
    int pending = 1;
    while (pending > 0) {
        if (t[i].type == JSMN_OBJECT) pending += t[i].size * 2;
        else if (t[i].type == JSMN_ARRAY) pending += t[i].size;
        pending--;
        i++;
    }
    return i;
}

static bool key_is(const char *js, const jsmntok_t *t, const char *key) {
    size_t n = (size_t)(t->end - t->start);
    return (t->type == JSMN_STRING) && (strlen(key) == n) && (0 == memcmp(js + t->start, key, n));
}

// value token of `key` in the object at token obj, or -1
static int find_key(const char *js, const jsmntok_t *t, int obj, const char *key) {
    int i = obj + 1;
    for (int k = 0; k < t[obj].size; k++) {
        if (key_is(js, &t[i], key)) return i + 1;
        i = skip(t, i + 1);
    }
    return -1;
}

typedef struct { ptrdiff_t start, end; } span_t;

// rewrites the JSON chunk; returns false on malformed input, sets *changed
static bool fix_scene_roots(const char *js, size_t len, String_Builder *out, bool *changed) {
    jsmn_parser parser;
    jsmn_init(&parser);
    int count = jsmn_parse(&parser, js, len, NULL, 0);
    if (count <= 0) return false;
    jsmntok_t *t = malloc(sizeof(jsmntok_t) * (size_t)count);
    jsmn_init(&parser);
    if ((jsmn_parse(&parser, js, len, t, (size_t)count) != count) || (t[0].type != JSMN_OBJECT)) {
        free(t);
        return false;
    }

    // which nodes are somebody's child
    static bool is_child[MAX_NODES];
    memset(is_child, 0, sizeof(is_child));
    int num_nodes = 0;
    int nodes = find_key(js, t, 0, "nodes");
    if ((nodes > 0) && (t[nodes].type == JSMN_ARRAY)) {
        num_nodes = t[nodes].size;
        int n = nodes + 1;
        for (int ni = 0; ni < num_nodes; n = skip(t, n), ni++) {
            int children = find_key(js, t, n, "children");
            if (children < 0) continue;
            for (int c = 0; c < t[children].size; c++) {
                int idx = atoi(js + t[children + 1 + c].start);
                if ((idx >= 0) && (idx < MAX_NODES)) is_child[idx] = true;
            }
        }
    }

    // roots = parentless nodes, as JSON array text
    String_Builder roots = {0};
    sb_append_cstr(&roots, "[");
    for (int i = 0, first = 1; (i < num_nodes) && (i < MAX_NODES); i++) {
        if (is_child[i]) continue;
        sb_appendf(&roots, first ? "%d" : ",%d", i);
        first = 0;
    }
    sb_append_cstr(&roots, "]");

    // scenes[*].nodes arrays that list a child node get replaced by roots
    span_t spans[64];
    int num_spans = 0;
    int scenes = find_key(js, t, 0, "scenes");
    if ((scenes > 0) && (t[scenes].type == JSMN_ARRAY)) {
        int s = scenes + 1;
        for (int si = 0; si < t[scenes].size; s = skip(t, s), si++) {
            int arr = find_key(js, t, s, "nodes");
            if ((arr < 0) || (t[arr].type != JSMN_ARRAY)) continue;
            bool bad = false;
            for (int c = 0; c < t[arr].size; c++) {
                int idx = atoi(js + t[arr + 1 + c].start);
                if ((idx >= 0) && (idx < MAX_NODES) && is_child[idx]) bad = true;
            }
            if (bad && (num_spans < 64)) spans[num_spans++] = (span_t){ t[arr].start, t[arr].end };
        }
    }

    ptrdiff_t pos = 0;
    for (int i = 0; i < num_spans; i++) {
        sb_append_buf(out, js + pos, (size_t)(spans[i].start - pos));
        sb_append_buf(out, roots.items, roots.count);
        pos = spans[i].end;
    }
    sb_append_buf(out, js + pos, len - (size_t)pos);
    *changed = num_spans > 0;
    sb_free(roots);
    free(t);
    return true;
}

// copies src to dst, fixing the JSON chunk; returns -1 on error, 1 if fixed, 0 if unchanged
static int import_glb(const char *src, const char *dst) {
    String_Builder in = {0}, json = {0}, out = {0};
    int result = -1;
    if (!read_entire_file(src, &in)) goto done;
    if ((in.count < 20) || (read_u32(in.items) != GLB_MAGIC) || (read_u32(in.items + 16) != CHUNK_JSON)) {
        nob_log(ERROR, "%s: not a .glb with a leading JSON chunk", src);
        goto done;
    }
    uint32_t json_len = read_u32(in.items + 12);
    if (20 + (size_t)json_len > in.count) {
        nob_log(ERROR, "%s: truncated JSON chunk", src);
        goto done;
    }
    bool changed = false;
    if (!fix_scene_roots(in.items + 20, json_len, &json, &changed)) {
        nob_log(ERROR, "%s: cannot parse JSON chunk", src);
        goto done;
    }
    if (changed) {
        while (json.count % 4) da_append(&json, ' ');   // JSON chunk is 4-byte aligned, space padded
        const char *rest = in.items + 20 + json_len;    // BIN chunk (header + payload), untouched
        size_t rest_len = in.count - 20 - json_len;
        append_u32(&out, GLB_MAGIC);
        append_u32(&out, 2);
        append_u32(&out, (uint32_t)(12 + 8 + json.count + rest_len));
        append_u32(&out, (uint32_t)json.count);
        append_u32(&out, CHUNK_JSON);
        sb_append_buf(&out, json.items, json.count);
        sb_append_buf(&out, rest, rest_len);
        if (!write_entire_file(dst, out.items, out.count)) goto done;
    } else {
        if (!write_entire_file(dst, in.items, in.count)) goto done;
    }
    result = changed ? 1 : 0;
done:
    sb_free(in); sb_free(json); sb_free(out);
    return result;
}

static bool ends_with(const char *s, const char *suffix) {
    size_t n = strlen(s), m = strlen(suffix);
    return (n >= m) && (0 == strcmp(s + n - m, suffix));
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s <source_dir> <dest_dir>\n", argv[0]);
        return 1;
    }
    const char *src_dir = argv[1], *dst_dir = argv[2];
    File_Paths entries = {0};
    if (!read_entire_dir(src_dir, &entries)) return 1;
    if (!mkdir_if_not_exists(dst_dir)) return 1;
    int imported = 0, fixed = 0, failed = 0;
    for (size_t i = 0; i < entries.count; i++) {
        if (!ends_with(entries.items[i], ".glb")) continue;
        const char *src = temp_sprintf("%s/%s", src_dir, entries.items[i]);
        const char *dst = temp_sprintf("%s/%s", dst_dir, entries.items[i]);
        int r = import_glb(src, dst);
        if (r < 0) { failed++; continue; }
        imported++;
        fixed += r;
    }
    printf("imported %d models into %s (%d fixed, %d failed)\n", imported, dst_dir, fixed, failed);
    return failed ? 1 : 0;
}
