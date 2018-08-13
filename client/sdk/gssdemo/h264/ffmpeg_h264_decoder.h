#ifndef FFMPEG_H264_DECODER_H
#define FFMPEG_H264_DECODER_H

typedef void (*on_video_decode_func)(void* decoder, void* user_key, unsigned char** data, int* linesize, int width, int height, unsigned int time_stamp);

void* create_ffmpeg_video_decoder(void* user_key, on_video_decode_func on_decode, int* result);

void  destroy_ffmpeg_video_decoder(void* decoder);

/*decode a frame video*/
void ffmpeg_video_decode_frame(void* decoder, const char* buf, int buf_len, unsigned int time_stamp);


#endif 
