#define _GNU_SOURCE
#include <turbojpeg.h>
#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"
#include "viewporter-client-protocol.h"

#define len(x) (sizeof(x) / sizeof((x)[0]))
void update_viewport();

typedef struct {
    const char* fileName;
    FILE* fileHandle;
    long fileSize;
    int width;
    int height;
    unsigned int textureId;
    int fd;
} Image;

int testCycling = 1;
int imageIndex = 0;
Image imageArray[2];
Image* image;


struct wayland_context {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct xdg_wm_base *xdg_wm_base;
    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *xdg_toplevel;
    struct wp_viewporter *wp_viewporter;
    struct wp_viewport *viewport;
    struct wl_seat *seat;
    struct wl_keyboard *keyboard;
    Image* img;

    int running;
    int configured;

    double lastMouseX, lastMouseY;
    double mouseX,     mouseY;
    float  offsetX,    offsetY;

    int windowWidth, windowHeight;
    int isPanning;
    // int isDirty;
    int isZoom;
};
struct wayland_context ctx = { .running = 1, .configured = 0 };
void update_view(struct wayland_context* ctx, int test);

// Image img  = { .fileName = "images/smol.jpg", .fileSize = 0 };
Image img  = { .fileName = "images/architecture.jpg", .fileSize = 0 };

clock_t profiler_time;
clock_t profiler_time_sum;
#define profiler(s) { \
    printf("> profiler: %8.2fms - %s\n", ((float)(clock() - profiler_time) / CLOCKS_PER_SEC)*1000, s); \
    profiler_time = clock(); \
}

static int image_map_file_src_into(Image* image, void **buf) {
    int fd = open(image->fileName, O_RDONLY);
    if (fd == -1) {
        fprintf(stderr, "ERROR:%d: %s\n", __LINE__, strerror(errno));
        return -1;
    }

    struct stat st;
    if (fstat(fd, &st) == -1) {
        fprintf(stderr, "ERROR:%d: %s\n", __LINE__, strerror(errno));
        close(fd);
        return -1;
    }
    size_t filesize = st.st_size;

    *buf = mmap(NULL, filesize, PROT_READ, MAP_PRIVATE, fd, 0);
    if (*buf == MAP_FAILED) {
        fprintf(stderr, "ERROR:%d: %s\n", __LINE__, strerror(errno));
        close(fd);
        return -1;
    }

    close(fd);
    image->fileSize = st.st_size;
    printf("file: %s size: %ld\n", image->fileName, image->fileSize);

    return 0;
}
/*
static int image_read_file_src_into(Image* image, void **buf) {
    if (image_open(image) < 0) {
        return -1;
    }
    *buf = (void*)malloc(image->fileSize);
    if (*buf == NULL) {
        fprintf(stderr, "ERROR:%d: %s\n", __LINE__, strerror(errno));
        return -1;
    }
    if (fread(*buf, image->fileSize, 1, image->fileHandle) < 1) {
        fprintf(stderr, "ERROR:%d: %s\n", __LINE__, strerror(errno));
        return -1;
    }
    return 0;
}
*/
// https://github.com/libjpeg-turbo/libjpeg-turbo/blob/main/src/tjdecomp.c
static int image_read_jpeg_into(Image* image, void *srcBuf, void *dstBuf) {
    int ret = -1, colorspace, jpegPrecision, w, h, pixelFormat = TJPF_BGRX;
    tjhandle tjh = NULL;

    tjh = tj3Init(TJINIT_DECOMPRESS);
    tj3Set(tjh, TJPARAM_STOPONWARNING, 1);
    tj3Set(tjh, TJPARAM_NOREALLOC,     1);
    tj3Set(tjh, TJPARAM_MAXMEMORY,     0);
    tj3Set(tjh, TJPARAM_FASTDCT  ,     1);
    tj3Set(tjh, TJPARAM_FASTUPSAMPLE,  1);

    if (tj3DecompressHeader(tjh, srcBuf, image->fileSize) < 0) {
        fprintf(stderr, "ERROR:%d: %s\n", __LINE__, tj3GetErrorStr(tjh));
        goto cleanup;
    }
    profiler("tj3DecompressHeader");

    w = tj3Get(tjh, TJPARAM_JPEGWIDTH);
    h = tj3Get(tjh, TJPARAM_JPEGHEIGHT);
    jpegPrecision = tj3Get(tjh, TJPARAM_PRECISION);
    colorspace = tj3Get(tjh, TJPARAM_COLORSPACE);
    if (colorspace == TJCS_CMYK || colorspace == TJCS_YCCK) {
        fprintf(stderr, "ERROR:%d: CMYK/YCCK pixel formats not supported.\n", __LINE__);
        goto cleanup;
    }
    printf("precision: %d colorspace: %d\n", jpegPrecision, colorspace);

    // // tjscalingfactor scalingFactor = TJUNSCALED;
    tjscalingfactor scalingFactor = {1,2};
    if (tj3SetScalingFactor(tjh, scalingFactor) < 0) {
        fprintf(stderr, "ERROR:%d: %s\n", __LINE__, tj3GetErrorStr(tjh));
        goto cleanup;
    }
    w = TJSCALED(w, scalingFactor);
    h = TJSCALED(h, scalingFactor);

    ret = tj3Decompress8(tjh, srcBuf, image->fileSize, dstBuf, 0, pixelFormat);
    profiler("tj3Decompress8");
    if (ret < 0) {
        fprintf(stderr, "ERROR:%d: %s\n", __LINE__, tj3GetErrorStr(tjh));
        ret = -1;
        goto cleanup;
    }

    ret           = 0;
    image->width  = w;
    image->height = h;

  cleanup:
    munmap(srcBuf, image->fileSize);
    tj3Destroy(tjh);
    if (image->fileHandle) fclose(image->fileHandle);
    return ret;
}

static unsigned int image_read_png_into(Image* image, void *srcBuf, void *dstBuf) {
    png_image pngImage;
    memset(&pngImage, 0, (sizeof pngImage));
    pngImage.version = PNG_IMAGE_VERSION;

    if (!png_image_begin_read_from_memory(&pngImage, srcBuf, image->fileSize)) {
        fprintf(stderr, "ERROR:%d: %s\n", __LINE__, pngImage.message);
        return -1;
    }
    profiler("png_image_begin_read_from_memory");

    pngImage.format = PNG_FORMAT_RGB;
    if (!png_image_finish_read(&pngImage, 0, dstBuf, 0, NULL)) {
        fprintf(stderr, "ERROR:%d: %s\n", __LINE__, pngImage.message);
        return -1;
    }
    profiler("png_image_finish_read");

    image->width  = pngImage.width;
    image->height = pngImage.height;

    return 0;
}

int is_jpeg(const unsigned char *srcBuf, size_t size) {
    if (size < 3) return 0;
    return (srcBuf[0] == 0xFF && srcBuf[1] == 0xD8 && srcBuf[2] == 0xFF);
}

static void noop() {}
static void noop_name(void *data, struct wl_seat *seat, const char *name) {}
static void noop_keymap(void *data, struct wl_keyboard *kb, uint32_t format, int fd, uint32_t size) { close(fd); }

static void xdg_surface_handle_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial) {
    struct wayland_context *ctx = data;
    xdg_surface_ack_configure(xdg_surface, serial);
    ctx->configured = 1;
}

static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_handle_configure
};

// static void frame_callback_handler(void *data, struct wl_callback *callback, uint32_t callback_data);
// static const struct wl_callback_listener frame_listener = {
//     .done = frame_callback_handler
// };
// static void frame_callback_handler(void *data, struct wl_callback *callback, uint32_t callback_data) {
//     wl_callback_destroy(callback);
//     struct wl_callback *new_cb = wl_surface_frame(ctx.surface);
//     wl_callback_add_listener(new_cb, &frame_listener, data);
// }

static void xdg_toplevel_handle_configure(void *data, struct xdg_toplevel *t, int32_t w, int32_t h, struct wl_array *states) {
    printf("configure w: %d, h: %d\n", w, h);
}

static void xdg_toplevel_handle_close(void *data, struct xdg_toplevel *t) {
    ((struct wayland_context*)data)->running = 0;
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
    .configure = xdg_toplevel_handle_configure,
    .close = xdg_toplevel_handle_close
};

static void xdg_wm_base_handle_ping(void *data, struct xdg_wm_base *xdg_wm_base, uint32_t serial) {
    xdg_wm_base_pong(xdg_wm_base, serial);
}

static const struct xdg_wm_base_listener xdg_wm_base_listener = {
    .ping = xdg_wm_base_handle_ping
};

float zoom = 1.0f;

static void keyboard_handle_key(void *data, struct wl_keyboard *keyboard,
                               uint32_t serial, uint32_t time, uint32_t key, uint32_t state) {
    struct wayland_context *ctx = data;
    if (state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        if (key == 1) { // esc
            ctx->running = 0;
        } else if (key == 57) { // space

            // if (zoom > 1.0f) {
            //     zoom = 1.0f;
            // } else {
            //     zoom = 2.0f;
            // }
            // update_viewport(1);
            // wl_display_roundtrip(ctx->display);
            // update_viewport(0);
            // wl_display_roundtrip(ctx->display);

            // struct wl_callback *callback = wl_surface_frame(ctx->surface);
            // wl_callback_add_listener(callback, &frame_listener, NULL);
            // update_view(ctx, 1);

            // wl_display_flush(ctx->display);
            // wl_display_roundtrip(ctx->display); // Forces everything to sync
            // wl_display_flush(ctx->display);
        } else {
            printf("key: %d\n", key);
        }
    }
}

static const struct wl_keyboard_listener keyboard_listener = {
    .keymap = noop_keymap,
    .enter = (void *)noop,
    .leave = (void *)noop,
    .key = keyboard_handle_key,
    .modifiers = (void *)noop,
    .repeat_info = (void *)noop,
};

static void seat_handle_capabilities(void *data, struct wl_seat *seat, uint32_t caps) {
    struct wayland_context *ctx = data;

    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !ctx->keyboard) {
        ctx->keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(ctx->keyboard, &keyboard_listener, ctx);
    } else if (!(caps & WL_SEAT_CAPABILITY_KEYBOARD) && ctx->keyboard) {
        wl_keyboard_release(ctx->keyboard);
        ctx->keyboard = NULL;
    }
}

static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_handle_capabilities,
    .name = (void *)noop
};

static void registry_handle_global(void *data, struct wl_registry *registry,
                                   uint32_t name, const char *interface, uint32_t version) {
    struct wayland_context *ctx = data;
    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        ctx->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        ctx->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        ctx->xdg_wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(ctx->xdg_wm_base, &xdg_wm_base_listener, ctx);
    } else if (strcmp(interface, wp_viewporter_interface.name) == 0) {
        ctx->wp_viewporter = wl_registry_bind(registry, name, &wp_viewporter_interface, 1);
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        ctx->seat = wl_registry_bind(registry, name, &wl_seat_interface, 7);
        wl_seat_add_listener(ctx->seat, &seat_listener, ctx);
    }
}

static void registry_handle_global_remove(void *data, struct wl_registry *registry, uint32_t name) {}

static const struct wl_registry_listener registry_listener = {
    .global = registry_handle_global,
    .global_remove = registry_handle_global_remove
};


static int create_shm_file(size_t size) {
    int fd = memfd_create("wayland_shm", MFD_CLOEXEC);
    if (fd < 0) return -1;
    if (ftruncate(fd, size) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

void create_window() {
    ctx.display = wl_display_connect(NULL);
    if (!ctx.display) return;

    ctx.registry = wl_display_get_registry(ctx.display);
    wl_registry_add_listener(ctx.registry, &registry_listener, &ctx);
    wl_display_roundtrip(ctx.display);

    if (!ctx.compositor || !ctx.shm || !ctx.xdg_wm_base) return;

    // return ctx;
    ctx.surface = wl_compositor_create_surface(ctx.compositor);
    ctx.xdg_surface = xdg_wm_base_get_xdg_surface(ctx.xdg_wm_base, ctx.surface);
    xdg_surface_add_listener(ctx.xdg_surface, &xdg_surface_listener, &ctx);
    ctx.xdg_toplevel = xdg_surface_get_toplevel(ctx.xdg_surface);
    xdg_toplevel_add_listener(ctx.xdg_toplevel, &xdg_toplevel_listener, &ctx);
    xdg_toplevel_set_title(ctx.xdg_toplevel, "Image Viewer");

    ctx.viewport = wp_viewporter_get_viewport(ctx.wp_viewporter, ctx.surface);

    wp_viewport_set_destination(ctx.viewport, ctx.windowWidth, ctx.windowHeight);
    wp_viewport_set_source(ctx.viewport,
        wl_fixed_from_int(0),
        wl_fixed_from_int(0),
        wl_fixed_from_int(img.width),
        wl_fixed_from_int(img.height));

    // wl_surface_commit(ctx.surface);
}

struct wl_buffer *buffer;
// struct wl_buffer *buffer2;

void update_view(struct wayland_context* ctx, int test) {
    // double source_w = ctx->windowWidth;
    // double source_h = ctx->windowHeight;
    double source_w = img.width;
    double source_h = img.height;

    // if (source_w > img.width)  source_w = img.width;
    // if (source_h > img.height) source_h = img.height;

    double px = 0.0;
    double py = 0.0;
    if (test == 1) {
        // px = (img.width  - source_w) / 2.0;
        // py = (img.height - source_h) / 2.0;
        px = 200.0;
        py = 200.0;

        printf("p %f %f\n", px, py);
    }
    // wl_surface_attach(ctx->surface, buffer, 0, 0);
    // wl_surface_set_buffer_scale(ctx->surface, 1);
    wp_viewport_set_source(ctx->viewport,
                           wl_fixed_from_double(px),
                           wl_fixed_from_double(py),
                           wl_fixed_from_double(source_w),
                           wl_fixed_from_double(source_h));

    wp_viewport_set_destination(ctx->viewport, ctx->windowWidth, ctx->windowHeight);
    wl_surface_damage_buffer(ctx->surface, 0, 0, INT32_MAX, INT32_MAX);
    // xdg_surface_set_window_geometry(ctx->xdg_surface, 0, 0, ctx->windowWidth, ctx->windowHeight);
    wl_surface_commit(ctx->surface);
}

void update_vp(){
    // wl_surface_attach(ctx.surface, buffer, 0, 0);

    wp_viewport_set_destination(ctx.viewport, ctx.windowWidth, ctx.windowHeight);
    wp_viewport_set_source(ctx.viewport,
        wl_fixed_from_int(0),           // x offset in buffer
        wl_fixed_from_int(0),           // y offset in buffer
        wl_fixed_from_int(img.width),   // width of source area
        wl_fixed_from_int(img.height)); // height of source area

    wl_surface_damage_buffer(ctx.surface, 0, 0, INT32_MAX, INT32_MAX);
    // xdg_surface_set_window_geometry(ctx.xdg_surface, 0, 0, ctx.windowWidth, ctx.windowHeight);

    // Commit to apply
    wl_surface_commit(ctx.surface);
}

void update_viewport(int test) {
    if (test) {
        wl_surface_attach(ctx.surface, NULL, 0, 0);
    } else {
        // wl_surface_attach(ctx.surface, buffer, 0, 0);
    }

    int img_width  = img.width;
    int img_height = img.height;

    int source_width = (int)(img_width / zoom);
    int source_height = (int)(img_height / zoom);

    int source_x = (img_width - source_width) / 2;
    int source_y = (img_height - source_height) / 2;

    wp_viewport_set_destination(ctx.viewport, ctx.windowWidth, ctx.windowHeight);
    wp_viewport_set_source(ctx.viewport,
        // wl_fixed_from_int(source_x),
        // wl_fixed_from_int(source_y),
        wl_fixed_from_int(10),
        wl_fixed_from_int(10),
        // wl_fixed_from_int(source_width),
        // wl_fixed_from_int(source_height));
        wl_fixed_from_int(20),
        wl_fixed_from_int(20));
    // wl_surface_set_buffer_scale(ctx.surface, 1);
    // wl_surface_set_buffer_transform(ctx.surface, WL_OUTPUT_TRANSFORM_NORMAL);
    wl_surface_damage_buffer(ctx.surface, 0, 0, INT32_MAX, INT32_MAX);
    wl_surface_commit(ctx.surface);
}

void display_image(unsigned char *pixel_buffer, Image* img) {
    create_window();
    profiler("create_window");

    int stride  = img->width * 4;
    size_t size = (size_t)stride * img->height;
    int fd = create_shm_file(size);
    if (fd < 0) return;
    profiler("create_shm_file");

    void *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    memcpy(data, pixel_buffer, size);
    profiler("mmap");

    struct wl_shm_pool *pool = wl_shm_create_pool(ctx.shm, fd, (int32_t)size);
    buffer = wl_shm_pool_create_buffer(pool, 0, img->width, img->height, stride, WL_SHM_FORMAT_XRGB8888);
    // buffer2 = wl_shm_pool_create_buffer(pool, 0, img->width, img->height, stride, WL_SHM_FORMAT_XRGB8888);
    profiler("wl_shm_pool_create_buffer");

    // xdg_toplevel_set_min_size(ctx.xdg_toplevel, ctx.windowWidth, ctx.windowHeight);
    // xdg_toplevel_set_max_size(ctx.xdg_toplevel, ctx.windowWidth, ctx.windowHeight);
    // xdg_surface_set_window_geometry(ctx.xdg_surface, 0, 0, ctx.windowWidth, ctx.windowHeight);
    wl_surface_commit(ctx.surface);
    wl_display_roundtrip(ctx.display);
    profiler("wl_surface_commit/roundtrip");
    printf("> profiler: %8.2fms - sum\n", ((float)(clock() - profiler_time_sum) / CLOCKS_PER_SEC)*1000);

    while (ctx.running && wl_display_dispatch(ctx.display) != -1) {
        if (ctx.configured) {
            wl_surface_attach(ctx.surface, buffer, 0, 0);
            wl_surface_damage_buffer(ctx.surface, 0, 0, INT32_MAX, INT32_MAX);
            // wl_surface_damage(ctx.surface, 0, 0, img->width, img->height);
            wl_surface_commit(ctx.surface);
            ctx.configured = 0;
        }
    }

    munmap(data, size);
    close(fd);
    wl_shm_pool_destroy(pool);
    xdg_toplevel_destroy(ctx.xdg_toplevel);
    xdg_surface_destroy(ctx.xdg_surface);
    if (ctx.keyboard) wl_keyboard_release(ctx.keyboard);
    if (ctx.seat) wl_seat_release(ctx.seat);
    wl_surface_destroy(ctx.surface);
    xdg_wm_base_destroy(ctx.xdg_wm_base);
    wl_shm_destroy(ctx.shm);
    wl_compositor_destroy(ctx.compositor);
    wl_registry_destroy(ctx.registry);
    wl_display_disconnect(ctx.display);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <path_to_image>\n", argv[0]);
        return 1;
    }
    profiler_time      = clock();
    profiler_time_sum  = clock();
    ctx.isZoom       = 0;
    ctx.windowWidth  = 640;
    ctx.windowHeight = 480;

    if (1) {
        // ctx.img = &img;

        void* dstBuf = malloc(4000 * 6000 * 4);
        void* srcBuf = NULL;
        if (image_map_file_src_into(&img, &srcBuf) < 0) {
            return -1;
        }
        profiler("image_map_file_src_into");
        image_read_jpeg_into(&img, srcBuf, dstBuf);
        profiler("image_read_jpeg_into");
        display_image(dstBuf, &img);
        // profiler("display_stub");
        return 0;
    }

    return 0;
}
