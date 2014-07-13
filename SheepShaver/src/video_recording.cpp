#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "video_recording.hpp"
#include "video.h"
#include "video_blit.h"
#include "app.hpp"

#define DEBUG 1
#include "debug.h"


video_recording_state_t::video_recording_state_t(void)
{
	video_stream = NULL;
	audio_stream = NULL;
	video_frame_raw = NULL;
	video_frame = NULL;
	audio_frame = NULL;
	output_context = NULL;
	sws_context = NULL;
}

video_recording_state_t::~video_recording_state_t(void)
{
	if (video_frame_raw) avcodec_free_frame(&video_frame_raw);
	if (video_frame) avcodec_free_frame(&video_frame);
	if (audio_frame) avcodec_free_frame(&audio_frame);
	if (output_context) avformat_free_context(output_context);
}

static AVFrame *alloc_picture(AVStream *video_stream, enum AVPixelFormat pix_fmt)
{
	AVCodecContext *c = video_stream->codec;
	AVFrame *video_frame = avcodec_alloc_frame();
	if (!video_frame) return NULL;
	avcodec_get_frame_defaults(video_frame);
	video_frame->format = pix_fmt;
	video_frame->width = c->width;
	video_frame->height = c->height;
	video_frame->pts = 0;
	if (c->get_buffer(c, video_frame) < 0) {
		avcodec_free_frame(&video_frame);
		return NULL;
	}
	return video_frame;
}

bool video_recording_state_t::initialize(uint16 video_nr, int width, int height, int depth)
{
	enum AVPixelFormat raw_fmt;
	if (depth == VIDEO_DEPTH_8BIT) raw_fmt = AV_PIX_FMT_PAL8;
	else if (depth == VIDEO_DEPTH_32BIT) raw_fmt = AV_PIX_FMT_ARGB;
	else return false;
	char filename[32];
	snprintf(filename, sizeof filename, "rec%hu.avi", video_nr);
	AVOutputFormat *fmt = av_guess_format(NULL, filename, NULL);
	if (!fmt) return false;
	if (fmt->flags & AVFMT_NOFILE) return false;

	output_context = avformat_alloc_context();
	if (!output_context) return false;
	output_context->oformat = fmt;
	snprintf(output_context->filename, sizeof(output_context->filename), "%s", filename);
	if (fmt->video_codec == AV_CODEC_ID_NONE) return false;

	if (!add_audio_stream(AV_CODEC_ID_PCM_S16LE)) return false;
	if (!add_video_stream(fmt->video_codec, width, height)) return false;
	if (!open_audio()) return false;
	if (!open_video()) return false;
	if (!(video_frame_raw = alloc_picture(video_stream, raw_fmt))) return false;
	if (!(video_frame = alloc_picture(video_stream, AV_PIX_FMT_YUV420P))) return false;
	if (!init_sws_context()) return false;

	if (avio_open(&output_context->pb, filename, AVIO_FLAG_WRITE) < 0) return false;
	avformat_write_header(output_context, NULL);
	return true;
}

void video_recording_state_t::finalize(void)
{
	av_write_trailer(output_context);
	avcodec_close(audio_stream->codec);
	avcodec_close(video_stream->codec);
	avio_close(output_context->pb);
}

bool video_recording_state_t::add_audio_stream(enum AVCodecID codec_id)
{
	AVCodecContext *c;
	AVCodec *codec;
	if (!(codec = avcodec_find_encoder(codec_id))) return false;
	audio_stream = avformat_new_stream(output_context, codec);
	if (!audio_stream) return false;
	audio_stream->pts.den = 1;
	c = audio_stream->codec;
	c->sample_fmt = AV_SAMPLE_FMT_S16;
	c->sample_rate = 44100;
	c->channels = 2;
	c->channel_layout = AV_CH_LAYOUT_STEREO;
	if (output_context->oformat->flags & AVFMT_GLOBALHEADER)
		c->flags |= CODEC_FLAG_GLOBAL_HEADER;
	return true;
}

bool video_recording_state_t::open_audio(void)
{
	AVCodecContext *c = audio_stream->codec;
	if (!(c->codec->capabilities & CODEC_CAP_VARIABLE_FRAME_SIZE)) return false;
	if (avcodec_open2(c, NULL, NULL) < 0) return false;
	audio_frame = avcodec_alloc_frame();
	if (!audio_frame) return false;
	audio_frame->sample_rate = c->sample_rate;
	audio_frame->format = c->sample_fmt;
	audio_frame->channel_layout = c->channel_layout;
	audio_frame->nb_samples = 2048;
	audio_frame->pts = 0;
	return true;
}

void video_recording_state_t::write_audio_frame(uint8 *buffer)
{
	AVCodecContext *c = audio_stream->codec;
	av_samples_fill_arrays(audio_frame->data, audio_frame->linesize, buffer, 2, 2048, AV_SAMPLE_FMT_S16, 0);
	uint16 *base = (uint16 *)audio_frame->data[0];
	for (int i = 0; i < 4096; ++i) {
		*base = bswap_16(*base);
		++base;
	}
	AVPacket pkt;
	int got_packet = 0;
	av_init_packet(&pkt);
	pkt.data = NULL;
	pkt.size = 0;
	if (avcodec_encode_audio2(c, &pkt, audio_frame, &got_packet) < 0) {
		fprintf(stderr, "Error encoding an audio frame\n");
	}
	if (got_packet) {
		pkt.pts = pkt.dts = audio_frame->pts++;
		pkt.stream_index = audio_stream->index;
		if (av_interleaved_write_frame(output_context, &pkt) < 0) {
			fprintf(stderr, "Error writing an audio frame\n");
		}
		if (av_interleaved_write_frame(output_context, NULL) < 0) {
			fprintf(stderr, "Error flushing output buffer\n");
		}
	}
}


bool video_recording_state_t::add_video_stream(enum AVCodecID codec_id, int width, int height)
{
	AVCodecContext *c;
	AVCodec *codec = avcodec_find_encoder(codec_id);
	if (!codec) return false;
	video_stream = avformat_new_stream(output_context, codec);
	if (!video_stream) return false;
	video_stream->pts.den = 1;
	c = video_stream->codec;
	c->bit_rate = 100000000;
	c->width = width;
	c->height = height;
	c->time_base.den = 8000;
	c->time_base.num = 133;
	c->gop_size = 0;
	c->pix_fmt = AV_PIX_FMT_YUV420P;
	/* Some formats want stream headers to be separate. */
	if (output_context->oformat->flags & AVFMT_GLOBALHEADER)
		c->flags |= CODEC_FLAG_GLOBAL_HEADER;
	return true;
}

bool video_recording_state_t::open_video(void)
{
	if (avcodec_open2(video_stream->codec, NULL, NULL) < 0) return false;
	return true;
}

bool video_recording_state_t::init_sws_context(void)
{
	AVCodecContext *c = video_stream->codec;
	sws_context = sws_getCachedContext(sws_context, c->width, c->height, (enum AVPixelFormat)video_frame_raw->format,
									   c->width, c->height, AV_PIX_FMT_YUV420P,
									   SWS_BICUBIC, NULL, NULL, NULL);
	if (!sws_context) return false;
	return true;
}

void video_recording_state_t::write_video_frame(uint8 *framebuffer, rgb_color *palette)
{
	AVCodecContext *c = video_stream->codec;
	avpicture_fill((AVPicture *)video_frame_raw, framebuffer, (enum AVPixelFormat)video_frame_raw->format, c->width, c->height);
	if (video_frame_raw->format == AV_PIX_FMT_PAL8) {
		uint32 *base = (uint32 *)video_frame_raw->data[1];
		for (int i = 0; i < 256; ++i) {
			*base++ = (palette[i].red << 16) | (palette[i].green << 8) | palette[i].blue;
		}
	}
	sws_scale(sws_context, video_frame_raw->data, video_frame_raw->linesize,
			  0, c->height, video_frame->data, video_frame->linesize);
	AVPacket pkt;
	int got_packet = 0;
	av_init_packet(&pkt);
	pkt.data = NULL;
	pkt.size = 0;
	if (avcodec_encode_video2(c, &pkt, video_frame, &got_packet) < 0) {
		fprintf(stderr, "Error encoding a video frame\n");
	}
	if (got_packet) {
		pkt.pts = pkt.dts = video_frame->pts++;
		pkt.stream_index = video_stream->index;
		if (av_interleaved_write_frame(output_context, &pkt) < 0) {
			fprintf(stderr, "Error writing a video frame\n");
		}
	}
}


void sheepshaver_state::init_video_recording(void)
{
	av_register_all();
}

void sheepshaver_state::start_video_recording(int width, int height, int depth)
{
	finalize_video_recording();
	video_recording_state = new video_recording_state_t();
	if (!video_recording_state->initialize(++video_nr, width, height, depth)) {
		fprintf(stderr, "error initializing video recording state\n");
		delete video_recording_state;
	}
}

void sheepshaver_state::finalize_video_recording(void)
{
	if (video_recording_state) {
		video_recording_state->finalize();
		delete video_recording_state;
		video_recording_state = NULL;
	}
}

void sheepshaver_state::record_video(void)
{
	if (!video_recording_state) return;
	video_recording_state->write_video_frame(Mac2HostAddr(video_state.screen_base), video_state.mac_pal);
}

void sheepshaver_state::record_audio(uint8 *buffer)
{
	if (!video_recording_state) return;
	video_recording_state->write_audio_frame(buffer);
}

void sheepshaver_state::record_audio(void)
{
	static uint8 silence[8192] = {0};
	record_audio(silence);
}
