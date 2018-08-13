#include "h264_reader.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "gss_common.h"
#include "tm.h"

#ifdef _WIN32
#include <Windows.h>
#endif

int read_h264_file(const char* filepath, int fps, unsigned char deamon, on_h264_frame on_frame)
{
	FILE *f;
	char* buffer;
	size_t buf_len = 4; //4 is NALU length, "00 00 00 01"
	size_t readed;
	size_t begin = 0;
	const size_t max_buf_len = 512*1024;
	unsigned int time_stamp = 0;
	int64_t span_time;
	int64_t  begin_time = now_ms_time();
	buffer = (char*)malloc(max_buf_len);

	do
	{
		buf_len = 4;
		begin = 0;
		f = fopen(filepath, "rb");
		if(!f)
		{
			printf("read_h264_file failed to fopen %s\r\n", filepath); 
			free(buffer);
			return -1;
		}
		
		//read first NALU
		fread(buffer, sizeof(char), buf_len, f);

		while( !feof(f) )
		{
			size_t i;
			begin = 0;
			readed = fread(buffer+buf_len, sizeof(char), max_buf_len-buf_len, f);
			for(i=buf_len; i<readed+buf_len; i++)
			{
				//find next NALU
				if(buffer[i] == 0 && buffer[i+1] == 0 && buffer[i+2] == 0 && buffer[i+3] == 1)
				{
					unsigned char nut = *(buffer+begin+4) & 0x1f;
					unsigned char next_nut = buffer[i+4] & 0x1f;
					int ret = 0;
					//nut  7: SPS, 8: PPS, 5: I Frame, 1: P Frame, 9: AUD, 6: SEI
					if(nut == 7 && next_nut!=1) //merge sps pps iframe
						continue;

					if(on_frame)
						ret = on_frame(buffer+begin, i-begin, time_stamp);

					if(ret != 0)
					{
						printf("read_h264_file failed on_frame return %d\r\n", ret); 
						free(buffer);
						fclose(f);
						return -1;
					}
					
					time_stamp += (1000/fps);
					span_time = now_ms_time()-begin_time;
					//printf("frame %d %lld\r\n", time_stamp, span_time);
					if(time_stamp > span_time)
						usleep(1000*(time_stamp-(unsigned int)span_time));
					else
						time_stamp = span_time; //may be on_frame too long time
					begin = i;
				}
			}
			buf_len = i-begin;
			if(buf_len)
				memmove(buffer, buffer+begin, buf_len);

		}
		
		fclose(f);

	}while(deamon);

	free(buffer);
	return 0;
}