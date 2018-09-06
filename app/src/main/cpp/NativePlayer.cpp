//
// Created by FC5981 on 2018/9/3.
//
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>

#include "include/EncodeAVDemo.h"
#include "include/androidlog.h"
/* Add for ANativeWindow*/
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <libavformat/avformat.h>

#define LOG_TAG "NativePlayer_JNI"
volatile jboolean nativePlayerStop = false;


int decode_packet(AVPacket **pPacket);

void JNICALL
Java_com_hua_nativeFFmpeg_NativeFFmpeg_nativePlay(JNIEnv *env, jobject instance, jstring input,
                                                  jobject surface) {
    const char *file_name = env->GetStringUTFChars(input, 0);
    ALOGD("play");

    AVFormatContext *pFormatCtx = avformat_alloc_context();
    // Open video file
    if (avformat_open_input(&pFormatCtx, file_name, NULL, NULL) != 0) {
        ALOGE("Couldn't open file:%s\n", file_name);
        return; // Couldn't open file
    }
    // Retrieve stream information
    if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
        ALOGE("Couldn't find stream information.");
        return;
    }

    int bestVideoStream = av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (bestVideoStream == -1) {
        ALOGE("Didn't find a video stream.");
        return; // Didn't find a video stream
    }
    AVCodecParameters *avCodecParameters = pFormatCtx->streams[bestVideoStream]->codecpar;
    AVCodec *pCodec = avcodec_find_decoder(avCodecParameters->codec_id);
    if (pCodec == NULL) {
        ALOGE("Codec not found.");
        return; // Codec not found
    }
    AVCodecContext *pCodecCtx = avcodec_alloc_context3(pCodec);
    if (!pCodecCtx) {
        ALOGE("Codec not alloc CodecContext.");
        return; // Codec not found
    }
    int ret = avcodec_parameters_to_context(pCodecCtx, avCodecParameters);  // 这一步也是必须的
    if (ret) {
        ALOGE("Can't copy decoder context");
        return;
    }
    AVCodecParserContext *pAVCodecParserContext = av_parser_init(pCodec->id);
    if (!pAVCodecParserContext) {
        ALOGE("av_parser_init Failed");
        return;
    }

    if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0) {
        ALOGE("Could not open codec.");
        return; // Could not open codec
    }

    // 获取native window
    ANativeWindow *nativeWindow = ANativeWindow_fromSurface(env, surface);

    // 获取视频宽高
    int videoWidth = pCodecCtx->width;
    int videoHeight = pCodecCtx->height;

    // 设置native window的buffer大小,可自动拉伸
    ANativeWindow_setBuffersGeometry(nativeWindow, videoWidth, videoHeight,
                                     WINDOW_FORMAT_RGBA_8888);
    ANativeWindow_Buffer windowBuffer;

    // Allocate video frame
    AVFrame *pFrame = av_frame_alloc();

    // 用于渲染
    AVFrame *pFrameRGBA = av_frame_alloc();
    if (pFrameRGBA == NULL || pFrame == NULL) {
        ALOGE("Could not allocate video frame.");
        return;
    }

    // Determine required buffer size and allocate buffer
    // buffer中数据就是用于渲染的,且格式为RGBA
    int numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGBA, pCodecCtx->width, pCodecCtx->height,
                                            1);
    uint8_t *buffer = (uint8_t *) av_malloc(numBytes * sizeof(uint8_t));
    av_image_fill_arrays(pFrameRGBA->data, pFrameRGBA->linesize, buffer, AV_PIX_FMT_RGBA,
                         pCodecCtx->width, pCodecCtx->height, 1);

    // 由于解码出来的帧格式不是RGBA的,在渲染之前需要进行格式转换
    struct SwsContext *sws_ctx = sws_getContext(pCodecCtx->width,
                                                pCodecCtx->height,
                                                pCodecCtx->pix_fmt,
                                                pCodecCtx->width,
                                                pCodecCtx->height,
                                                AV_PIX_FMT_RGBA,
                                                SWS_BILINEAR,
                                                NULL,
                                                NULL,
                                                NULL);

    AVPacket *pkt = av_packet_alloc();

    // 新API 解码显示* @deprecated Use avcodec_send_packet() and avcodec_receive_frame().
    //avcodec_decode_video2 attribute_deprecated

    /* read frames from the file */
    while (av_read_frame(pFormatCtx, pkt) >= 0) {
        if (pkt->stream_index == bestVideoStream){
            int ret = avcodec_send_packet(pCodecCtx, pkt);
            if (ret < 0) {
                ALOGE("Error while sending a packet to the decoder: %s", av_err2str(ret));
                return;
            }
            while (ret >= 0){
                ret = avcodec_receive_frame(pCodecCtx,pFrame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                } else if (ret < 0) {
                    ALOGE("Error while receiving a frame from the decoder: %s", av_err2str(ret));
                    return ;
                }
                if(ret >= 0 && !nativePlayerStop) {
                  // Draw in native window
                    ANativeWindow_lock(nativeWindow,&windowBuffer,0);

                    // 格式转换,  这一步是必须的
                    sws_scale(sws_ctx, (uint8_t const *const *) pFrame->data,
                              pFrame->linesize, 0, pCodecCtx->height,
                              pFrameRGBA->data, pFrameRGBA->linesize);

                    uint8_t *dst = (uint8_t *) windowBuffer.bits;//WindowBuff 要显示的地址
                    int dstStride = windowBuffer.stride * 4;   // 一行所占的像素大小 ，因为是 AGRB ，所以是32位，4个字节
                    uint8_t *src = (pFrameRGBA->data[0]);      // 原始数据
                    int srcStride = pFrameRGBA->linesize[0];   // 图片一行的大小
                    // 由于window的stride和帧的stride不同,因此需要逐行复制
                    for (int h = 0; h < videoHeight ; h++) {
                        uint8_t *srcAddr = dst + dstStride * h;
                        memcpy(dst + dstStride * h ,src + h* srcStride,srcStride);
                    }
                    ANativeWindow_unlockAndPost(nativeWindow);
                }
            }
        }

        if(nativePlayerStop) {
            // make sure we don't leak native windows
            if (nativeWindow != NULL) {
                ALOGE("Free native Window");
                ANativeWindow_release(nativeWindow);

                nativeWindow = NULL;
            }
            av_packet_unref(pkt);
            break;
        }

        av_packet_unref(pkt);
        if (ret < 0)
            break;
    }


    if (nativePlayerStop || av_read_frame(pFormatCtx, pkt) < 0) {
        ALOGE("Free resource ,, buffer pframe pCodecCtx pFormatCtx");
        av_free(buffer);
        av_free(pFrameRGBA);
        // Free the YUV frame
        av_free(pFrame);
        // Close the codecs
        avcodec_close(pCodecCtx);
        // Close the video file
        avformat_close_input(&pFormatCtx);
    }
}

int decode_packet(AVPacket **pPacket) {

    return 0;
}


void JNICALL
Java_com_hua_nativeFFmpeg_NativeFFmpeg_nativePlay222(JNIEnv *env, jobject instance, jstring input,
                                                  jobject surface) {
    const char *file_name = env->GetStringUTFChars(input, 0);
    ALOGD("play");

    AVFormatContext *pFormatCtx = avformat_alloc_context();
    // Open video file
    if (avformat_open_input(&pFormatCtx, file_name, NULL, NULL) != 0) {
        ALOGE("Couldn't open file:%s\n", file_name);
        return; // Couldn't open file
    }
    // Retrieve stream information
    if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
        ALOGE("Couldn't find stream information.");
        return;
    }

    int bestVideoStream = av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (bestVideoStream == -1) {
        ALOGE("Didn't find a video stream.");
        return; // Didn't find a video stream
    }
    AVCodecParameters *avCodecParameters = pFormatCtx->streams[bestVideoStream]->codecpar;
    AVCodec *pCodec = avcodec_find_decoder(avCodecParameters->codec_id);
    if (pCodec == NULL) {
        ALOGE("Codec not found.");
        return; // Codec not found
    }
    AVCodecContext *pCodecCtx = avcodec_alloc_context3(pCodec);
    if (!pCodecCtx) {
        ALOGE("Codec not alloc CodecContext.");
        return; // Codec not found
    }
    int ret = avcodec_parameters_to_context(pCodecCtx, avCodecParameters);  // 这一步也是必须的
    if (ret) {
        ALOGE("Can't copy decoder context");
        return;
    }
    if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0) {
        ALOGE("Could not open codec.");
        return; // Could not open codec
    }

    // 获取native window
    ANativeWindow *nativeWindow = ANativeWindow_fromSurface(env, surface);

    // 获取视频宽高
    int videoWidth = pCodecCtx->width;
    int videoHeight = pCodecCtx->height;

    // 设置native window的buffer大小,可自动拉伸
    ANativeWindow_setBuffersGeometry(nativeWindow, videoWidth, videoHeight,
                                     WINDOW_FORMAT_RGBA_8888);
    ANativeWindow_Buffer windowBuffer;

    // Allocate video frame
    AVFrame *pFrame = av_frame_alloc();

    // 用于渲染
    AVFrame *pFrameRGBA = av_frame_alloc();
    if (pFrameRGBA == NULL || pFrame == NULL) {
        ALOGE("Could not allocate video frame.");
        return;
    }

    // Determine required buffer size and allocate buffer
    // buffer中数据就是用于渲染的,且格式为RGBA
    int numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGBA, pCodecCtx->width, pCodecCtx->height,
                                            1);
    uint8_t *buffer = (uint8_t *) av_malloc(numBytes * sizeof(uint8_t));
    av_image_fill_arrays(pFrameRGBA->data, pFrameRGBA->linesize, buffer, AV_PIX_FMT_RGBA,
                         pCodecCtx->width, pCodecCtx->height, 1);

    // 由于解码出来的帧格式不是RGBA的,在渲染之前需要进行格式转换
    struct SwsContext *sws_ctx = sws_getContext(pCodecCtx->width,
                                                pCodecCtx->height,
                                                pCodecCtx->pix_fmt,
                                                pCodecCtx->width,
                                                pCodecCtx->height,
                                                AV_PIX_FMT_RGBA,
                                                SWS_BILINEAR,
                                                NULL,
                                                NULL,
                                                NULL);

    int frameFinished;
    AVPacket packet;
    while (av_read_frame(pFormatCtx, &packet) >= 0) {
        // Is this a packet from the video stream?
        if (packet.stream_index == bestVideoStream) {
            // Decode video frame
            avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &packet);
            // 并不是decode一次就可解码出一帧
            if (frameFinished && !nativePlayerStop) {
                // lock native window buffer
                ANativeWindow_lock(nativeWindow, &windowBuffer, 0);

                // 格式转换
                sws_scale(sws_ctx, (uint8_t const *const *) pFrame->data,
                          pFrame->linesize, 0, pCodecCtx->height,
                          pFrameRGBA->data, pFrameRGBA->linesize);

                // 获取stride
                uint8_t *dst = (uint8_t *) windowBuffer.bits;
                int dstStride = windowBuffer.stride * 4;
                uint8_t *src = (pFrameRGBA->data[0]);
                int srcStride = pFrameRGBA->linesize[0];
                // 由于window的stride和帧的stride不同,因此需要逐行复制
                for (int h = 0; h < videoHeight; h++) {
                    memcpy(dst + h * dstStride, src + h * srcStride, srcStride);
                }
                ANativeWindow_unlockAndPost(nativeWindow);
            }
            if(nativePlayerStop) {
                // make sure we don't leak native windows
                if (nativeWindow != NULL) {
                    ALOGE("Free native Window");
                    ANativeWindow_release(nativeWindow);

                    nativeWindow = NULL;
                }
                av_packet_unref(&packet);
                break;
            }

        }
        av_packet_unref(&packet);
    }
    if (nativePlayerStop || av_read_frame(pFormatCtx, &packet) < 0) {
        ALOGE("Free resource ,, buffer pframe pCodecCtx pFormatCtx");
        av_free(buffer);
        av_free(pFrameRGBA);
        // Free the YUV frame
        av_free(pFrame);
        // Close the codecs
        avcodec_close(pCodecCtx);
        // Close the video file
        avformat_close_input(&pFormatCtx);
    }
}


void JNICALL
Java_com_hua_nativeFFmpeg_NativeFFmpeg_nativePlayStop(JNIEnv *env, jobject instance,
                                                      jboolean stop) {
    ALOGE("Need native Player stop %s", stop == 1 ? "true" : "false");
    nativePlayerStop = stop;
}

