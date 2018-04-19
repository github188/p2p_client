#ifndef __DD_YUV_H__
#define __DD_YUV_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <Windows.h>

struct direct_draw_yuv;

struct direct_draw_yuv* create_dd_yuv(HWND wnd, int video_width, int video_height);
void destroy_dd_yuv(struct direct_draw_yuv* dd_yuv);
void dd_yuv_draw(struct direct_draw_yuv* dd_yuv, unsigned char** data, int* linesize);

#ifdef __cplusplus
}
#endif

#endif
