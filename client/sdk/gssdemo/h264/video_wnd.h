#ifndef __VIDEO_WND_H__
#define __VIDEO_WND_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <Windows.h>

struct video_wnd;

struct video_wnd* create_video_wnd();
HWND video_wnd_get_handle(struct video_wnd* wnd);
void destroy_video_wnd(struct video_wnd* wnd);

#ifdef __cplusplus
}
#endif

#endif
