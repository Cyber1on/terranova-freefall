// tnfree_rife.cpp -- tnfree-rife.dll: RIFE 4.6 frame interpolation (nihui's rife-ncnn-vulkan,
// third_party/rife-ncnn-vulkan) behind the small C interface of port/include/tnfree_rife.h.
// Freefall uses it to make the in-between pictures of the game's 15 fps movies.
#include <stdio.h>
#include <string.h>
#include <string>

#include "gpu.h"
#include "net.h"
#include "rife.h"

#define TNFREE_RIFE_BUILD
#include "../port/include/tnfree_rife.h"   // not via port/include: its platform.h is not ncnn's

#ifdef _WIN32
#include <windows.h>
#endif

struct tnfree_rife
{
    RIFE* rife;
};

static int gpu_started;     // 1 = create_gpu_instance done, -1 = it failed
static int open_count;

static int start_gpu()
{
    if (!gpu_started)
        gpu_started = ncnn::create_gpu_instance() == 0 ? 1 : -1;
    return gpu_started == 1;
}

static void set_err(char* err, int size, const char* msg)
{
    if (err && size > 0)
        snprintf(err, (size_t)size, "%s", msg);
}

#ifdef _WIN32
static std::wstring widen(const char* utf8)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    std::wstring w(n > 0 ? n : 1, L'\0');
    if (n > 0)
        MultiByteToWideChar(CP_UTF8, 0, utf8, -1, &w[0], n);
    w.resize(wcslen(w.c_str()));
    return w;
}

static bool file_exists(const std::wstring& p)
{
    return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES;
}
#else
static bool file_exists(const std::string& p)
{
    FILE* f = fopen(p.c_str(), "rb");
    if (f)
        fclose(f);
    return f != NULL;
}
#endif

extern "C" {

int TNFREE_RIFE_CALL tnfree_rife_version(void)
{
    return TNFREE_RIFE_API_VERSION;
}

int TNFREE_RIFE_CALL tnfree_rife_gpu_count(void)
{
    return start_gpu() ? ncnn::get_gpu_count() : 0;
}

int TNFREE_RIFE_CALL tnfree_rife_default_gpu(void)
{
    return tnfree_rife_gpu_count() > 0 ? ncnn::get_default_gpu_index() : -1;
}

const char* TNFREE_RIFE_CALL tnfree_rife_gpu_name(int gpu, int* type)
{
    if (gpu < 0 || gpu >= tnfree_rife_gpu_count())
        return "";
    const ncnn::GpuInfo& info = ncnn::get_gpu_info(gpu);
    if (type)
        *type = info.type();
    return info.device_name();
}

tnfree_rife* TNFREE_RIFE_CALL tnfree_rife_open(const char* model_dir, int gpu, char* err, int err_size)
{
    int count = tnfree_rife_gpu_count();
    if (count <= 0)
    {
        set_err(err, err_size, "no Vulkan graphics card");
        return NULL;
    }
    if (gpu < 0 || gpu >= count)
        gpu = ncnn::get_default_gpu_index();

#ifdef _WIN32
    std::wstring dir = widen(model_dir ? model_dir : ".");
    if (!file_exists(dir + L"/flownet.param") || !file_exists(dir + L"/flownet.bin"))
#else
    std::string dir = model_dir ? model_dir : ".";
    if (!file_exists(dir + "/flownet.param") || !file_exists(dir + "/flownet.bin"))
#endif
    {
        set_err(err, err_size, "the RIFE model (flownet.param, flownet.bin) is missing");
        return NULL;
    }

    // RIFE 4.x: one network (flownet), no TTA, no UHD mode (the movies are at most 640x480)
    // newer models need the picture padded to a larger multiple (as in TNTwise's rife-ncnn-vulkan)
    int padding = 32;
    std::string name = model_dir ? model_dir : "";
    if (name.find("v4.25-lite") != std::string::npos)
        padding = 128;
    else if (name.find("v4.25") != std::string::npos || name.find("v4.26") != std::string::npos)
        padding = 64;
    RIFE* rife = new RIFE(gpu, false, false, false, 1, false, true, padding);
    if (rife->load(dir) != 0)
    {
        delete rife;
        set_err(err, err_size, "the RIFE model could not be loaded");
        return NULL;
    }
    tnfree_rife* r = new tnfree_rife;
    r->rife = rife;
    open_count++;
    return r;
}

int TNFREE_RIFE_CALL tnfree_rife_interpolate(tnfree_rife* r, const unsigned char* a, const unsigned char* b,
                                             int w, int h, float t, unsigned char* out)
{
    if (!r || !a || !b || !out || w <= 0 || h <= 0)
        return -1;
    // RIFE reads and writes packed 8-bit RGB (elemsize 3, elempack 3) in place
    ncnn::Mat in0(w, h, (void*)a, (size_t)3, 3);
    ncnn::Mat in1(w, h, (void*)b, (size_t)3, 3);
    ncnn::Mat res(w, h, (void*)out, (size_t)3, 3);
    return r->rife->process(in0, in1, t, res);
}

void TNFREE_RIFE_CALL tnfree_rife_close(tnfree_rife* r)
{
    if (!r)
        return;
    delete r->rife;
    delete r;
    open_count--;
}

void TNFREE_RIFE_CALL tnfree_rife_shutdown(void)
{
    if (gpu_started == 1 && open_count == 0)
    {
        ncnn::destroy_gpu_instance();
        gpu_started = 0;
    }
}

} // extern "C"
