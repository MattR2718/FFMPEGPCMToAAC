// FFMPEGPCMToAAC.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include <iostream>
#include <vector>

// TODO: Reference additional headers your program requires here.
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

#include "AudioEncoder.h"

#include "AudioInput.h"