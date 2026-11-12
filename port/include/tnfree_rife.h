/* tnfree_rife.h -- the C interface of tnfree-rife.dll, the optional video smoothing helper
 * (rife/tnfree_rife.cpp).  The DLL wraps RIFE 4.6 frame interpolation (rife-ncnn-vulkan) and
 * runs it on the graphics card through Vulkan.  tnfree.exe never links it: port/video/videosmooth.c
 * loads it with SDL_LoadObject only when video smoothing is wanted, so a PC without Vulkan
 * (the DLL then fails to load) just has the option greyed out.
 *
 * Pictures are 24-bit RGB, 3 bytes a pixel, rows packed (pitch = width * 3).
 * All calls for one handle must come from the same thread. */
#ifndef TNFREE_RIFE_H
#define TNFREE_RIFE_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32
#define TNFREE_RIFE_CALL __cdecl
#else
#define TNFREE_RIFE_CALL
#endif

#define TNFREE_RIFE_API_VERSION 1

typedef struct tnfree_rife tnfree_rife;

/* TNFREE_RIFE_API_VERSION of the DLL (tnfree.exe refuses another one) */
typedef int (TNFREE_RIFE_CALL *tnfree_rife_version_fn)(void);
/* the Vulkan graphics cards, 0 if there is none (starts Vulkan on the first call) */
typedef int (TNFREE_RIFE_CALL *tnfree_rife_gpu_count_fn)(void);
/* the card ncnn would choose (a discrete one before an integrated one), -1 if none */
typedef int (TNFREE_RIFE_CALL *tnfree_rife_default_gpu_fn)(void);
/* a card's name; *type = 0 discrete, 1 integrated, 2 virtual, 3 CPU (lavapipe and the like) */
typedef const char *(TNFREE_RIFE_CALL *tnfree_rife_gpu_name_fn)(int gpu, int *type);
/* loads the model (flownet.param + flownet.bin in model_dir, a UTF-8 path) on card `gpu`
 * (-1: the default card).  NULL on failure, with the reason in err. */
typedef tnfree_rife *(TNFREE_RIFE_CALL *tnfree_rife_open_fn)(const char *model_dir, int gpu,
                                                              char *err, int err_size);
/* the picture at time t (0 = a, 1 = b, 0.5 = halfway) between the w x h pictures a and b,
 * into out (w * h * 3 bytes).  0 on success. */
typedef int (TNFREE_RIFE_CALL *tnfree_rife_interpolate_fn)(tnfree_rife *r, const unsigned char *a,
                                                           const unsigned char *b, int w, int h,
                                                           float t, unsigned char *out);
typedef void (TNFREE_RIFE_CALL *tnfree_rife_close_fn)(tnfree_rife *r);
/* stops Vulkan (after the last close) */
typedef void (TNFREE_RIFE_CALL *tnfree_rife_shutdown_fn)(void);

#ifdef TNFREE_RIFE_BUILD
#ifdef _WIN32
#define TNFREE_RIFE_EXPORT __declspec(dllexport)
#else
#define TNFREE_RIFE_EXPORT __attribute__((visibility("default")))
#endif
TNFREE_RIFE_EXPORT int TNFREE_RIFE_CALL tnfree_rife_version(void);
TNFREE_RIFE_EXPORT int TNFREE_RIFE_CALL tnfree_rife_gpu_count(void);
TNFREE_RIFE_EXPORT int TNFREE_RIFE_CALL tnfree_rife_default_gpu(void);
TNFREE_RIFE_EXPORT const char *TNFREE_RIFE_CALL tnfree_rife_gpu_name(int gpu, int *type);
TNFREE_RIFE_EXPORT tnfree_rife *TNFREE_RIFE_CALL tnfree_rife_open(const char *model_dir, int gpu,
                                                                  char *err, int err_size);
TNFREE_RIFE_EXPORT int TNFREE_RIFE_CALL tnfree_rife_interpolate(tnfree_rife *r, const unsigned char *a,
                                                                const unsigned char *b, int w, int h,
                                                                float t, unsigned char *out);
TNFREE_RIFE_EXPORT void TNFREE_RIFE_CALL tnfree_rife_close(tnfree_rife *r);
TNFREE_RIFE_EXPORT void TNFREE_RIFE_CALL tnfree_rife_shutdown(void);
#endif

#ifdef __cplusplus
}
#endif

#endif
