#include <am.h>
#include <stdarg.h>
#include <res.h>
#include <klib.h>
#include <klib-macros.h>

void print(const char *s, ...) {
    va_list ap;
    va_start(ap, s);
    while (s) {
        for (; *s; s++) putch(*s);
        s = va_arg(ap, const char *);
    }
    va_end(ap);
}

int main(const char *args) {
    ioe_init();

    // uint32_t pixels[screen_width * screen_height]; // WARNING: large stack-allocated memory
    // uint32_t color;
    // for (int i = 0; i < w * h; i++) {
    //     pixels[i] = color;
    // }
    // ioe_write(AM_GPU_FBDRAW, &event);

    // L0_bmp

    const int img_width = 800;
    const int img_height = 600;
    const int offset = 54;
    const int color_len = 3;
    while (1) {
        int screen_width = io_read(AM_GPU_CONFIG).width;
        int screen_height = io_read(AM_GPU_CONFIG).height;
        for (int x = 0; x < screen_width; x++) {
            for (int y = 0; y < screen_height; y++) {
                int img_x = (float)x / screen_width * img_width;
                int img_y = (float)(screen_height - y - 1) / screen_height * img_height;
                int index = color_len * (img_y * img_width + img_x) + offset;
                assert(index < L0_bmp_len - color_len);
                uint32_t color = 0xff;
                for (int i = 0; i < color_len; i++) {
                    color = (color << 8) + L0_bmp[index + i];
                }
                
                io_write(AM_GPU_FBDRAW,
                    .x = x, .y = y, .w = 1, .h = 1, .sync = 1,
                    .pixels = &color // batch to optimize
                );
            }
        }

        AM_INPUT_KEYBRD_T event;
        ioe_read(AM_INPUT_KEYBRD, &event);
        if (event.keycode == AM_KEY_ESCAPE && event.keydown) {
            halt(1);
        }
    }

    // TODO: read res.h exceppt 54byes in the beginning
    // simulate draw_tile to write
    // TODO: self-adaptation

    print("\"", args, "\"", " from " __ISA__ " program!\n", NULL);

    return 0;
}



// AM_DEVREG: global register and struct define

// enum { AM_GPU_CONFIG = (9) }; 
// typedef struct { 
// 	_Bool present, has_accel; 
// 	int width, height, vmemsz; 
// } AM_GPU_CONFIG_T;

// in: read bytes from port
// out: write bytes to port