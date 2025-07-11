#pragma once


#include <iostream>
#include <vector>
#include <string>

extern "C" {
	#include <libavformat/avformat.h>
	#include <libavcodec/avcodec.h>
	#include <libswresample/swresample.h>
	#include <libavutil/opt.h>
}


// Function to decode audio from a file and return PCM16 mono data at 16kHz
std::vector<int16_t> decode_audio_to_pcm16(const std::string& filename) {
    AVFormatContext* fmt_ctx = nullptr;

    // Open input media file
    if (avformat_open_input(&fmt_ctx, filename.c_str(), nullptr, nullptr) < 0)
        throw std::runtime_error("Failed to open input file");

	// Retrieve stream information from media file
    if (avformat_find_stream_info(fmt_ctx, nullptr) < 0)
        throw std::runtime_error("Failed to find stream info");

	// Find the first audio stream from the format context
    int audio_stream_index = -1;
    for (unsigned i = 0; i < fmt_ctx->nb_streams; i++) {
        if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audio_stream_index = i;
            break;
        }
    }
    if (audio_stream_index < 0)
        throw std::runtime_error("No audio stream found");


	// Get codec parameters from the audio stream
    AVCodecParameters* codecpar = fmt_ctx->streams[audio_stream_index]->codecpar;

	// Find the decoder for the audio stream
    const AVCodec* decoder = avcodec_find_decoder(codecpar->codec_id);
    if (!decoder)
        throw std::runtime_error("Decoder not found");


	// Allocate codec context for the decoder
    AVCodecContext* codec_ctx = avcodec_alloc_context3(decoder);
    if (!codec_ctx)
        throw std::runtime_error("Failed to allocate codec context");

	// Copy codec parameters to codec context
    if (avcodec_parameters_to_context(codec_ctx, codecpar) < 0)
        throw std::runtime_error("Failed to copy codec parameters");

	// Open the codec
    if (avcodec_open2(codec_ctx, decoder, nullptr) < 0)
        throw std::runtime_error("Failed to open codec");

    // Setup SwrContext for resampling
    SwrContext* swr_ctx = swr_alloc();
    if (!swr_ctx)
        throw std::runtime_error("Failed to allocate SwrContext");

    // Set input options
    av_opt_set_chlayout(swr_ctx, "in_chlayout", &codec_ctx->ch_layout, 0);
    av_opt_set_sample_fmt(swr_ctx, "in_sample_fmt", codec_ctx->sample_fmt, 0);
    av_opt_set_int(swr_ctx, "in_sample_rate", codec_ctx->sample_rate, 0);

	// Set output options
    // 16kHz, 16-bit signed PCM, MONO
    AVChannelLayout out_ch_layout;
    av_channel_layout_default(&out_ch_layout, 1); // MONO

    av_opt_set_chlayout(swr_ctx, "out_chlayout", &out_ch_layout, 0);
    av_opt_set_sample_fmt(swr_ctx, "out_sample_fmt", AV_SAMPLE_FMT_S16, 0);
    av_opt_set_int(swr_ctx, "out_sample_rate", 16000, 0);


    // Initialise resampling context
    if (swr_init(swr_ctx) < 0)
        throw std::runtime_error("Failed to initialize SwrContext");

	// Allocate packet and frame for decoding
    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();


    std::vector<int16_t> pcm_data;


	// Read audio frame from the input file
    while (av_read_frame(fmt_ctx, pkt) >= 0) {
		// Check if the packet belongs to the audio stream found
        if (pkt->stream_index == audio_stream_index) {
			// Send packet to the decoder
            if (avcodec_send_packet(codec_ctx, pkt) == 0) {
				// Receive decoded frame from the decoder
                while (avcodec_receive_frame(codec_ctx, frame) == 0) {
					// Resample the audio frame to 16kHz mono 16-bit PCM
                    int dst_nb_samples = av_rescale_rnd(
                        swr_get_delay(swr_ctx, codec_ctx->sample_rate) + frame->nb_samples,
                        16000, codec_ctx->sample_rate, AV_ROUND_UP);

					// Allocate output buffer for resampled audio
                    uint8_t* out_buf = nullptr;
                    int out_linesize;
                    av_samples_alloc(&out_buf, &out_linesize, 1, dst_nb_samples, AV_SAMPLE_FMT_S16, 0);

					// Convert the audio frame to the desired format
                    int converted_samples = swr_convert(
                        swr_ctx, &out_buf, dst_nb_samples,
                        (const uint8_t**)frame->extended_data, frame->nb_samples);

                    if (converted_samples < 0)
                        throw std::runtime_error("Error during resampling");

					// Copy the converted samples to the output vector
                    int data_size = converted_samples * av_get_bytes_per_sample(AV_SAMPLE_FMT_S16);
                    int16_t* samples = reinterpret_cast<int16_t*>(out_buf);
                    pcm_data.insert(pcm_data.end(), samples, samples + converted_samples);

					// Free the output buffer
                    av_freep(&out_buf);
                }
            }
        }

		// Free the packet after processing
        av_packet_unref(pkt);
    }

	// Free resources
    av_channel_layout_uninit(&out_ch_layout);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    swr_free(&swr_ctx);
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&fmt_ctx);

    return pcm_data;
}