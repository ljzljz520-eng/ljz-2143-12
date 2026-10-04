#ifndef TEXSTRATEGY_H
#define TEXSTRATEGY_H

typedef enum {
    TEX_ATLAS   = 0,  /* 预生成多尺寸纹理 */
    TEX_RUNTIME = 1   /* 运行时缩放 */
} TexStrategy;

#endif
