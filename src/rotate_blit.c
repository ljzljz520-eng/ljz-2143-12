#include "rotate_blit.h"

#include <stdlib.h>
#include <string.h>

/*
 * 把逻辑渲染目标 src(pw×ph, TARGET) 旋转 90/270 到输出 STREAMING 纹理
 * dst(ph×pw)。软件渲染器下 TARGET 纹理不可 Lock，ReadPixels 也要求
 * 当前目标是 TARGET，故流程：
 *   1) 当前渲染目标切到 src（TARGET）；
 *   2) RenderReadPixels 读 ARGB 像素；
 *   3) CPU 旋转写入可 Lock 的 STREAMING dst。
 * 调用前渲染器当前目标必须是默认（窗口），本函数结束后恢复默认。
 */
int rotate_blit_90_270(SDL_Renderer *r, SDL_Texture *src, int pw, int ph,
                       SDL_Texture *dst, int rotation) {
    size_t npix = (size_t)pw * ph;
    Uint32 *buf = malloc(npix * sizeof(Uint32));
    if (!buf) return -1;

    if (SDL_SetRenderTarget(r, src) != 0) goto fail;
    if (SDL_RenderReadPixels(r, NULL, SDL_PIXELFORMAT_ARGB8888,
                             buf, pw * 4) != 0) goto fail;
    if (SDL_SetRenderTarget(r, NULL) != 0) goto fail;

    void *dpx = NULL;
    int pitch = 0;
    if (SDL_LockTexture(dst, NULL, &dpx, &pitch) != 0) goto fail;
    for (int y = 0; y < ph; y++) {
        for (int x = 0; x < pw; x++) {
            Uint32 c = buf[(size_t)y * pw + x];
            int tx, ty;
            /* 90: P=(ph-1-y, x) ; 270: P=(y, pw-1-x) */
            if (rotation == 90) { tx = ph - 1 - y; ty = x; }
            else               { tx = y;           ty = pw - 1 - x; }
            ((Uint32 *)((Uint8 *)dpx + (size_t)ty * pitch))[tx] = c;
        }
    }
    SDL_UnlockTexture(dst);
    free(buf);
    return 0;
fail:
    SDL_SetRenderTarget(r, NULL);
    free(buf);
    return -1;
}
