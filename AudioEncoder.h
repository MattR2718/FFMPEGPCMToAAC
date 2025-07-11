#pragma once

#include <iostream>
#include <thread>
#include <mutex>
#include <queue>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libavutil/error.h>
}


class AudioEncoder {
public:
    AudioEncoder(const std::string& outPath, bool _alwaysWriteToDisk = false, bool _printProgress = false)
    : alwaysWriteToDisk(_alwaysWriteToDisk), printProgress(_printProgress){
		initEncoder(outPath);
		// Create and start the encoding thread
		encoderThread = std::thread(&AudioEncoder::encodeLoop, this);
    }

	~AudioEncoder() {
        stop();
	}

    void pushSamples(const int16_t* data, uint32_t numSamples) {
        if (!data || numSamples == 0) { return; }

		// Push samples to the queue
		std::lock_guard<std::mutex> lock(queueMutex);
		for (int i = 0; i < numSamples; i++) { sampleQueue.push(data[i]); }
		queueCond.notify_one();
    }

    void stop() {
		// Signal the encoder thread to stop processing
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            stopping = true;
        }

        // Wake up the encoder thread if it's waiting
        queueCond.notify_all();

		// Wait for the encoder thread to finish
        if (encoderThread.joinable()) {
            encoderThread.join();
        }
    }



private:
    std::thread encoderThread;
    std::mutex queueMutex;
    std::condition_variable queueCond;
    std::queue<int16_t> sampleQueue;

    std::atomic<bool> stopping = false;
    bool initialized = false;

    // FFmpeg
    AVFormatContext* fmtCtx = nullptr;
    AVCodecContext* codecCtx = nullptr;
    AVStream* stream = nullptr;
    SwrContext* swrCtx = nullptr;
    AVFrame* frame = nullptr;
    AVPacket* pkt = nullptr;
    int64_t pts = 0;

    bool alwaysWriteToDisk = true;
    bool printProgress = true;
    uint32_t numSamplesEncoded = 0;

	// Main encoding loop which reads samples from the queue
    void encodeLoop() {
        const int frameSize = codecCtx->frame_size;

		// Loop forever until stopping is set
        while (true) {
            std::vector<int16_t> samples;

			// Wait for samples to be available or stopping signal
            {
                std::unique_lock<std::mutex> lock(queueMutex);
                queueCond.wait(lock, [this] { return !sampleQueue.empty() || stopping; });

                // Drain as many samples as available (even partial frame)
                while (!sampleQueue.empty() && samples.size() < frameSize) {
                    samples.push_back(sampleQueue.front());
                    sampleQueue.pop();
                }

                // Exit only if stopping and no more samples left
                if (stopping && sampleQueue.empty() && samples.empty()) {
                    break;
                }
            }

            if (samples.empty()) continue;

            const int16_t* data = samples.data();
            size_t total = samples.size();
            size_t offset = 0;

            //std::cout << "Processing " << total << " samples.\n";


			// If we have enough samples for a full frame, process them
            while (offset + frameSize <= total) {
                av_frame_make_writable(frame);

				// Fill the frame with samples
                const uint8_t* inData[1] = { reinterpret_cast<const uint8_t*>(data + offset) };
                int outSamples = swr_convert(
                    swrCtx, frame->data, frameSize, inData, frameSize
                );

                frame->pts = pts;
                pts += frame->nb_samples;

                encodeFrame(frame);

                offset += frameSize;
				numSamplesEncoded += frameSize;
            }



            if (printProgress) {
                int percentComplete = ((float)numSamplesEncoded / (float)(numSamplesEncoded + sampleQueue.size()) * 100) / 1;
                if (percentComplete % 5 == 0) {
                    std::cout << "Encoded " << numSamplesEncoded << " samples out of " << numSamplesEncoded + sampleQueue.size() << ": " << percentComplete << "% Complete\r";
                }
            }

        }

        if (printProgress) { std::cout << '\n'; }

        // Flush encoder
        encodeFrame(nullptr);
		// Write the trailer to finalize the file
        av_write_trailer(fmtCtx);
		// Free resources
        cleanup();
    
    }



    /*void flushEncoder() {
        encodeFrame(nullptr);
        av_write_trailer(fmtCtx);
    }*/

    void cleanup() {
        if (pkt) av_packet_free(&pkt);
        if (frame) av_frame_free(&frame);
        if (swrCtx) swr_free(&swrCtx);
        if (codecCtx) avcodec_free_context(&codecCtx);
        if (fmtCtx) {
            if (!(fmtCtx->oformat->flags & AVFMT_NOFILE))
                avio_closep(&fmtCtx->pb);
            avformat_free_context(fmtCtx);
        }
    }

	// Initialize the encoder with the output file path
    void initEncoder(const std::string& outPath) {
		// Allocate the format context for the output file
        avformat_alloc_output_context2(&fmtCtx, nullptr, nullptr, outPath.c_str());
        if (!fmtCtx) throw std::runtime_error("Failed to allocate format context");

		// Set the output format to AAC
        const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
        if (!codec) throw std::runtime_error("AAC encoder not found");

		// Initialise codec context for the encoder
        codecCtx = avcodec_alloc_context3(codec);
        codecCtx->sample_fmt = AV_SAMPLE_FMT_FLTP;
        codecCtx->sample_rate = 16000;
        codecCtx->bit_rate = 64000;
        codecCtx->ch_layout = AV_CHANNEL_LAYOUT_MONO;
        codecCtx->time_base = { 1, 16000 };

		// Create a new stream in the format context
        stream = avformat_new_stream(fmtCtx, nullptr);
        stream->time_base = codecCtx->time_base;

		// Open the codec
        if (avcodec_open2(codecCtx, codec, nullptr) < 0)
            throw std::runtime_error("Failed to open codec");

		// Set codec parameters for the stream
        avcodec_parameters_from_context(stream->codecpar, codecCtx);

		// Open the output file to write
        if (!(fmtCtx->oformat->flags & AVFMT_NOFILE)) {
            if (avio_open(&fmtCtx->pb, outPath.c_str(), AVIO_FLAG_WRITE) < 0)
                throw std::runtime_error("Failed to open output file");
        }

		// Write the header to the output file
        if (avformat_write_header(fmtCtx, nullptr) < 0)
            throw std::runtime_error("Failed to write header");

		// Allocate SwrContext for resampling
        swrCtx = swr_alloc();
        if (!swrCtx) {
            throw std::runtime_error("Failed to allocate SwrContext");
        }

        // Set output options
        av_opt_set_chlayout(swrCtx, "out_chlayout", &codecCtx->ch_layout, 0);
        av_opt_set_int(swrCtx, "out_sample_rate", codecCtx->sample_rate, 0);
        av_opt_set_sample_fmt(swrCtx, "out_sample_fmt", codecCtx->sample_fmt, 0);

        // Set input options
        // MONO, 16kHz, S16
        AVChannelLayout in_ch_layout = AV_CHANNEL_LAYOUT_MONO;
        av_opt_set_chlayout(swrCtx, "in_chlayout", &in_ch_layout, 0);
        av_opt_set_int(swrCtx, "in_sample_rate", 16000, 0);
        av_opt_set_sample_fmt(swrCtx, "in_sample_fmt", AV_SAMPLE_FMT_S16, 0);

		// Initialize the resampling context
        if (swr_init(swrCtx) < 0) {
            throw std::runtime_error("Failed to initialize resampler");
        }

		// Allocate frame and packet for encoding
        frame = av_frame_alloc();
        frame->nb_samples = codecCtx->frame_size;
        frame->format = codecCtx->sample_fmt;
        frame->sample_rate = codecCtx->sample_rate;
        frame->ch_layout = codecCtx->ch_layout;
        av_frame_get_buffer(frame, 0);

        pkt = av_packet_alloc();
        initialized = true;
    
    }



    void encodeFrame(AVFrame* frame) {
		// Send the frame to the encoder
        if (avcodec_send_frame(codecCtx, frame) < 0)
            return;

		// Receive packets from the encoder and write them to the output file
        while (true) {
            int ret = avcodec_receive_packet(codecCtx, pkt);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;

            pkt->stream_index = stream->index;
            av_packet_rescale_ts(pkt, codecCtx->time_base, stream->time_base);
            av_interleaved_write_frame(fmtCtx, pkt);
            av_packet_unref(pkt);

			// If alwaysWriteToDisk is true, flush the file to disk whenever possible
			// Data is written immediately to disk so in case of crash, data isn't lost
            if (alwaysWriteToDisk) {
                // Flush file to disk
                if (fmtCtx->pb) {
                    avio_flush(fmtCtx->pb);  // Force a write to disk
                }
            }
        }
    }

};