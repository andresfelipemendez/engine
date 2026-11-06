// Implementation TU for the libs shared library (libs.dll / libs.so).
// Loaded once by the host, so sokol's global state (device, buffers,
// pipelines) survives game library reloads.
// Backend picked via -DSOKOL_D3D11 / -DSOKOL_GLCORE.
#define SOKOL_IMPL
#define SOKOL_DLL           // export the sokol API (consumers define SOKOL_DLL to import)
#define SOKOL_NO_ENTRY      // the host owns main() and calls sapp_run()
#if defined(USE_DBG_UI)
#define SOKOL_TRACE_HOOKS   // lets sokol_gfx_imgui.h record per-frame API calls
#endif
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_log.h"
#include "sokol_glue.h"
#include "sokol_debugtext.h"

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"
