#ifndef VIDEO_RECORDING_HPP
#define VIDEO_RECORDING_HPP

extern "C" {
#include "libavutil/channel_layout.h"
#include "libavutil/mathematics.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
}

#include "sysdeps.h"


struct video_recording_state_t
{
	AVFormatContext *output_context;
	AVStream *video_stream;
	AVFrame *video_frame_raw;
	AVFrame *video_frame;
	struct SwsContext *sws_context;

	AVStream *audio_stream;
	AVFrame *audio_frame;

	video_recording_state_t(void);
	~video_recording_state_t(void);

	bool initialize(uint16, int, int, int);
	void finalize(void);

	bool add_audio_stream(enum AVCodecID);
	bool open_audio(void);
	void write_audio_frame(uint8 *);

	bool add_video_stream(enum AVCodecID, int, int);
	bool open_video(void);
	bool init_sws_context(void);
	void write_video_frame(uint8 *, rgb_color *);
};

#endif
