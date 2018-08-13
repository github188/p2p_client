#include "st_queue.h"
#include "libavformat/avformat.h"
#include "libavutil/avutil.h"
#include "ffmpeg_h264_decoder.h"


typedef struct ffmpeg_h264_decoder{
	st_queue* frame_queue;

	unsigned char is_run;

#ifdef _WIN32
	HANDLE thread;
#else
	pthread_t thread;
#endif
	AVCodec* codec;
	AVCodecContext* codec_ctx;

	void* user_key;
	on_video_decode_func on_decode;

	AVFrame* dec_frame;

}ffmpeg_h264_decoder;

#ifdef _WIN32
static DWORD WINAPI ffmpeg_decode_h264_thread(void* arg)
#else
static void* ffmpeg_decode_h264_thread(void* arg)
#endif
{
	ffmpeg_h264_decoder* decoder = (ffmpeg_h264_decoder*)arg;
	
	while(1){
		queue_item* item = NULL;
		AVPacket pkt;
		int ret;
		unsigned int* ts;

		//get encode video frame from frame list
		item = st_queue_pop(decoder->frame_queue);
		if(!decoder->is_run)
			break;
		if(!item)
			continue;

		ts = (unsigned int*)(item+1);

		av_init_packet(&pkt);  
		pkt.data = (uint8_t*)(ts+1) ;
		pkt.size = item->length; 

		ret = avcodec_send_packet(decoder->codec_ctx, &pkt);
		if(ret == 0){
			ret = avcodec_receive_frame(decoder->codec_ctx, decoder->dec_frame );
			if(ret == 0){
				(*decoder->on_decode)(decoder, 
					decoder->user_key,
					decoder->dec_frame->data, 
					decoder->dec_frame->linesize,
					decoder->dec_frame->width,
					decoder->dec_frame->height,
					*ts);
				av_frame_unref(decoder->dec_frame);
			}
		}
		free(item);
	}

	return 0;
}

void* create_ffmpeg_video_decoder(void* user_key, on_video_decode_func on_decode, int* result){
	ffmpeg_h264_decoder* decoder;
	AVCodec* codec;
	AVCodecContext* codec_ctx;
	int ret;

	codec = avcodec_find_decoder(AV_CODEC_ID_H264);
	if(codec == 0){
		if(result)
			*result = -1;
		return NULL;
	}

	codec_ctx = avcodec_alloc_context3(codec);
	codec_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
	ret = avcodec_open2(codec_ctx, codec, 0);
	if(ret < 0){
		if(result)
			*result = -2;
		av_free(codec_ctx);
		return NULL;
	}

	decoder = malloc(sizeof(ffmpeg_h264_decoder));
	decoder->on_decode = on_decode;
	decoder->user_key = user_key;

	decoder->dec_frame = av_frame_alloc();

	decoder->frame_queue = create_st_queue();

	decoder->is_run = 1;

	decoder->codec = codec;
	decoder->codec_ctx = codec_ctx;

#ifdef _WIN32
	decoder->thread = CreateThread(NULL, 0, ffmpeg_decode_h264_thread, decoder, 0, NULL);
#else
	pthread_create(&decoder->thread, NULL, ffmpeg_decode_h264_thread, decoder);
#endif	

	if(result)
		*result = 0;
	return decoder;
}

void  destroy_ffmpeg_video_decoder(void* decoder){
	ffmpeg_h264_decoder* dec = (ffmpeg_h264_decoder*)decoder;
	if(!decoder)
		return;

	dec->is_run = 0;
	/*wake up decode thread, wait for decode thread exit*/
	st_queue_wakeup(dec->frame_queue);

#ifdef _WIN32
	WaitForSingleObject(dec->thread, INFINITE);
#else
	pthread_join(dec->thread, NULL);
#endif
	
	destroy_st_queue(dec->frame_queue);

	avcodec_close(dec->codec_ctx);
	av_free(dec->codec_ctx);
	
	av_frame_free(&dec->dec_frame);

	free(dec);
}

/*decode a frame video*/
void ffmpeg_video_decode_frame(void* decoder, const char* buf, int buf_len, unsigned int time_stamp){
	ffmpeg_h264_decoder* dec = (ffmpeg_h264_decoder*)decoder;
	queue_item* item;
	unsigned int* ts;
	if(!decoder)
		return;
	
	/*copy data , push to item list*/
	item = malloc(sizeof(queue_item)+sizeof(unsigned int)+buf_len);
	item->length = buf_len;
	item->next = NULL;

	ts = (unsigned int*)(item+1);
	*ts = time_stamp;

	memcpy(ts+1, buf, buf_len);

	/*push video frame to queue and wake up decode thread*/
	st_queue_push_item(dec->frame_queue, item);
}