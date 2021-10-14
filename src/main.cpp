/*.
 * copyright (c) 2001 Fabrice Bellard
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <iostream>
#include <vector>
#include <string>
#include <Magick++.h>
#include <experimental/filesystem>
#include <thread>

namespace fs = std::experimental::filesystem;

extern "C" {
    #include <libavcodec/avcodec.h>
    #include <libavutil/mathematics.h>
    #include <libavformat/avformat.h>
    #include <libavutil/timestamp.h>
    #include <libswscale/swscale.h>
}

#define VIDS_TO_PROCESS 10

void saveFramesToPNG(const std::string& inputVidPath, std::string outputPath);

void executeThread(std::vector<std::string>& videoFiles, std::vector<std::string>& outputFolders, int threadNum, int numThreads) {
    for (int i = 0; i < VIDS_TO_PROCESS; i++) {
        if (i % numThreads == threadNum) {
            saveFramesToPNG(videoFiles[i], outputFolders[i]);
        }
    }
}

int main(int argc, char *argv[])
{
    Magick::InitializeMagick(NULL);

    fs::create_directory("./video-pngs");
    std::vector<std::string> videoFiles;
    std::vector<std::string> outputFolders; 
    auto video_directory = fs::directory_iterator("/newvolume/video-starter-task/2017-04-10");

    for(auto const& dir_entry : video_directory) {
        std::string videoPath = dir_entry.path();
        std::string outputPath = "./video-pngs/" + dir_entry.path().stem().string();

        fs::create_directory(outputPath); 
        videoFiles.push_back(videoPath);
        outputFolders.push_back(outputPath);
    }

    auto start = std::chrono::system_clock::now();

    int numThreads = 1; 
    std::vector<std::thread> workerThreads;
    for(int i = 0; i < numThreads; i++) {
        workerThreads.push_back(std::thread(executeThread, std::ref(videoFiles), std::ref(outputFolders), i, numThreads));
    }

    for(int i = 0; i < numThreads; i++) {
        workerThreads[i].join();
    }

    auto end = std::chrono::system_clock::now();
    std::chrono::duration<double> elapsed_seconds = end - start;

    std::cout << "elapsed time: " << elapsed_seconds.count() << "s\n";

    return 0; 
}

void saveFramesToPNG(const std::string& inputVidPath, std::string outputPath) {

    // Code adapted from: https://lists.ffmpeg.org/pipermail/libav-user/2011-April/000031.html
    enum PixelFormat pixelFmt = PIX_FMT_RGBA;
    AVFormatContext *formatCtx = NULL;
    AVCodecContext *codecCtx = NULL;
    AVCodec *codec = NULL;
    AVFrame *srcFrame = NULL;
    AVFrame *destFrame = NULL;
    uint8_t *buffer = NULL;
    AVPacket packet;
    SwsContext* swsCtx = NULL;

    int frameFinished;
    int numBytes;

    const char* vidPath = inputVidPath.c_str();
    int videoStream = -1;

    /* TODO: Try only registering video's codec
        *  But currently only takes .325 millis */
    av_register_all();

    if (avformat_open_input (&formatCtx, vidPath, NULL, NULL) != 0) {
        std::cout << "avformat_open_input fails" << '\n';
        return; 
    }

    if (avformat_find_stream_info (formatCtx, NULL) < 0) {
        std::cout << "avformat_find_stream_info fails" << '\n';
        return;
    }

    //NOTE: Grabs first video stream it can find, nothing else
    for (unsigned int i = 0; i < formatCtx->nb_streams; ++i)
        if (formatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStream = i;
            break;
        }

    if (videoStream == -1) {
        std::cout << "couldn't find video stream" << '\n';
        return; 
    }

    // Get a pointer to the codec context for the video stream
    codecCtx = formatCtx->streams[videoStream]->codec;
    int width = codecCtx->width; 
    int height = codecCtx->height; 

    // std::cout << "height: " << height << ", width: " << width << '\n';

    codec = avcodec_find_decoder(codecCtx->codec_id);
    if (codec == NULL) { 
        std::cout << "avcodec_find_decoder fails" << '\n';
        return; 
    }

    // Open codec
    if (avcodec_open2(codecCtx, codec, NULL) < 0) {
        std::cout << "avcodec_open2 fails" << '\n';
        return; 
    }

    // Allocate frames
    srcFrame = avcodec_alloc_frame();
    destFrame = avcodec_alloc_frame();

    if (destFrame == NULL || srcFrame == NULL) {
        std::cout << "avcodec_alloc_frame fails" << '\n';
        return; 
    }

    numBytes = avpicture_get_size(pixelFmt, width, height);

    buffer = (uint8_t *) av_malloc(numBytes * sizeof(uint8_t));

    avpicture_fill((AVPicture *) destFrame, buffer, pixelFmt, width, height);

    swsCtx = sws_getContext(codecCtx->width, codecCtx->height,
            codecCtx->pix_fmt, width, height, pixelFmt, SWS_BICUBIC, NULL,
            NULL, NULL);

    int frameNum = 0; 
    while (av_read_frame(formatCtx, &packet) >= 0) {

        // Is this a packet from the video stream?
        if (packet.stream_index == videoStream) {

            // Decode video frame
            avcodec_decode_video2(codecCtx, srcFrame, &frameFinished,
                    &packet);

            // Did we get a video frame?
            if (frameFinished) {
                sws_scale(swsCtx, srcFrame->data, srcFrame->linesize, 0,
                        codecCtx->height, destFrame->data,
                        destFrame->linesize);
                //NOTE: destFrame->data now points to raw RGBA pixel data
                //so you could do something else with it (E.g. transcode)
                Magick::Image(width, height, "RGBA", Magick::CharPixel,
                        destFrame->data[0]).write(outputPath + "/" + std::to_string(frameNum) + ".png");
                frameNum++; 
            }
        }

        // Free the packet that was allocated by av_read_frame
        av_free_packet(&packet);
    }

    sws_freeContext(swsCtx);
    av_free(buffer);
    av_free(destFrame);
    av_free(srcFrame);

    // Close the codec
    avcodec_close(codecCtx);

    // Close the video file
    avformat_close_input(&formatCtx);
}