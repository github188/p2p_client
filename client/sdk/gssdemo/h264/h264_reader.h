#ifndef __H264_READER_H__
#define __H264_READER_H__

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*on_h264_frame)(const char* buf, int len, unsigned int time_stamp);

int read_h264_file(const char* file_path, int fps, unsigned char deamon, on_h264_frame on_frame);

#ifdef __cplusplus
}
#endif

#endif
