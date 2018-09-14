//
// Created by FC5981 on 2018/9/3.
//
#include <sys/types.h>
#include <unistd.h>
//#include <pthread.h>
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

#include <thread>


#define LOG_TAG "NativePlayer_JNI"
volatile jboolean nativePlayerStop = false;

AVFormatContext *pFormatCtx;
AVCodecContext *pVideoCodecCtx;
AVCodecContext *pAudioCodecCtx;
AVCodecParserContext *pAVVideoCodecParserContext;
AVCodecParserContext *pAudioCodecParse;
struct SwsContext *sws_ctx;
SwrContext *swrCtx;
AVCodecParameters *avVideoCodecParameters;
AVCodecParameters *avAudioCodecParameters;
AVCodec *pVideoCodec;
AVCodec *pAudioCodec;
AVPacket *packet;
AVFrame *pFrame;
AVFrame *pFrameRGBA;

ANativeWindow *nativeWindow;
ANativeWindow_Buffer windowBuffer;


int frameFinished = 0;
int decode_packet(AVPacket **pPacket);

void dumpAudioInfo(const AVCodecContext *pAudioCodecContext);

void decodeVideo_oldAPI();

static int decodeVideo_newAPI();

static int decodeAudio_newAPI(JNIEnv *env, jobject audioTrack, jmethodID audioTrackWriteMethodID,
                              uint8_t *out_buffer, int out_channel_nb);

void* playVideoThreadMethod(void*);



void JNICALL
Java_com_hua_nativeFFmpeg_NativeFFmpeg_nativePlay_1VideoAndAudio(JNIEnv *env, jobject instance,
                                                  jstring input,
                                                  jobject surface) {

    const char *file_name = env->GetStringUTFChars(input, 0);
    ALOGD("play AV");
    pFormatCtx = avformat_alloc_context();
    // Open AV file
    if (avformat_open_input(&pFormatCtx, file_name, NULL, NULL) != 0) {
        ALOGE("Couldn't open file:%s\n", file_name);
        return;
    }

    // Retrieve stream information
    if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
        ALOGE("Couldn't find stream information.");
        return;
    }

    int bestVideoStream = av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    int bestAudioStream = av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    if (bestVideoStream == -1) {
        ALOGE("Didn't find a video stream.");
        return; // Didn't find a video stream
    }
    if (bestAudioStream == -1) {
        ALOGE("Didn't find a audio stream.");
        return; // Didn't find a audio stream
    }
    avVideoCodecParameters = pFormatCtx->streams[bestVideoStream]->codecpar;
    avAudioCodecParameters = pFormatCtx->streams[bestAudioStream]->codecpar;
    pVideoCodec = avcodec_find_decoder(avVideoCodecParameters->codec_id);
    pAudioCodec = avcodec_find_decoder(avAudioCodecParameters->codec_id);

    pAudioCodecParse = av_parser_init(pAudioCodec->id);
    if (!pAudioCodecParse) {
        ALOGE("pAudioCodecParse  not found\n");
        return;
    }

    if (pVideoCodec == NULL || pAudioCodec == NULL) {
        ALOGE("Codec not found.");
        return; // Codec not found
    }
    pVideoCodecCtx = avcodec_alloc_context3(pVideoCodec);
    pAudioCodecCtx = avcodec_alloc_context3(pAudioCodec);
    if (!pVideoCodecCtx || !pAudioCodecCtx) {
        ALOGE("Codec not alloc CodecContext.");
        return; // Codec not found
    }
    int ret = avcodec_parameters_to_context(pVideoCodecCtx, avVideoCodecParameters);  // 这一步也是必须的
    if (ret) {
        ALOGE("Can't copy  Video decoder context");
        return;
    }
    ret = avcodec_parameters_to_context(pAudioCodecCtx, avAudioCodecParameters);  // 这一步也是必须的
    if (ret) {
        ALOGE("Can't copy Audio decoder context");
        return;
    }

    if (avcodec_open2(pVideoCodecCtx, pVideoCodec, NULL) < 0 ||
        avcodec_open2(pAudioCodecCtx, pAudioCodec, NULL) < 0) {
        ALOGE("Could not open videoCodec or audioCodec");
        return; // Could not open codec
    }

    // Init AudioTrack...
    jclass nativeFFmpegCls = env->GetObjectClass(instance);
    if (nativeFFmpegCls == NULL) {
        ALOGE("nativeFFmpegCls == null");
        return;
    }
    jmethodID createAudioTrackMethodID = env->GetMethodID(nativeFFmpegCls, "createAudioTrack",
                                                          "(II)Landroid/media/AudioTrack;");
    if (createAudioTrackMethodID == NULL) {
        ALOGE("createAudioTrackMethodID == null");
        return;
    }

    jobject audioTrack = env->CallObjectMethod(instance, createAudioTrackMethodID,
                                               pAudioCodecCtx->sample_rate,
                                               pAudioCodecCtx->channels);
    if (audioTrack == NULL) {
        ALOGE("audioTrack == null");
        return;
    }
    // 找到AudioTrack的  write 方法
    jclass audioTrackCls = env->GetObjectClass(audioTrack);
    jmethodID audioTrackWriteMethodID = env->GetMethodID(audioTrackCls, "write", "([BII)I");
    jmethodID audioTrackPlayMethodID = env->GetMethodID(audioTrackCls, "play", "()V");
    if (audioTrackWriteMethodID == NULL || audioTrackPlayMethodID == NULL) {
        ALOGE("audioTrackWriteMethodID audioTrackPlayMethodID == null");
        return;
    }
    env->CallVoidMethod(audioTrack, audioTrackPlayMethodID);
    // End AudioTrack

    // Video
    // 获取native window
    nativeWindow = ANativeWindow_fromSurface(env, surface);
    // 获取视频宽高
    int videoWidth = pVideoCodecCtx->width;
    int videoHeight = pVideoCodecCtx->height;

    // 设置native window的buffer大小,可自动拉伸
    ANativeWindow_setBuffersGeometry(nativeWindow, videoWidth, videoHeight,
                                     WINDOW_FORMAT_RGBA_8888);
    // Allocate video frame
    pFrame = av_frame_alloc();
    // 用于渲染
    pFrameRGBA = av_frame_alloc();
    if (pFrameRGBA == NULL || pFrame == NULL) {
        ALOGE("Could not allocate video frame.");
        return;
    }

    // Determine required buffer size and allocate buffer
    // buffer中数据就是用于渲染的,且格式为RGBA
    int numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGBA, pVideoCodecCtx->width,
                                            pVideoCodecCtx->height,
                                            1);
    uint8_t *buffer = (uint8_t *) av_malloc(numBytes * sizeof(uint8_t));
    av_image_fill_arrays(pFrameRGBA->data, pFrameRGBA->linesize, buffer, AV_PIX_FMT_RGBA,
                         pVideoCodecCtx->width, pVideoCodecCtx->height, 1);

    // 由于解码出来的帧格式不是RGBA的,在渲染之前需要进行格式转换
    sws_ctx = sws_getContext(pVideoCodecCtx->width,
                             pVideoCodecCtx->height,
                             pVideoCodecCtx->pix_fmt,
                             pVideoCodecCtx->width,
                             pVideoCodecCtx->height,
                             AV_PIX_FMT_RGBA,
                             SWS_BILINEAR,
                             NULL,
                             NULL,
                             NULL);

    packet = av_packet_alloc();
    // Audio 重采样
    swrCtx = swr_alloc();//开辟空间

//    ALOGE("############### channels %d",pAudioCodecCtx->channels);                 // 2
//    int64_t out_ch_layout = AV_CH_LAYOUT_STEREO;
//    ALOGE("############### out_ch_layout %d",out_ch_layout);                      // 3
//    ALOGE("############### channel_layout %d",pAudioCodecCtx->channel_layout);   // 3
//    ALOGE("############### AV_SAMPLE_FMT_S16 %d",pAudioCodecCtx->sample_fmt);    //8
    AVSampleFormat tempOutSampleFormat = AV_SAMPLE_FMT_S16;
    swr_alloc_set_opts(swrCtx, pAudioCodecCtx->channel_layout, tempOutSampleFormat,
                       pAudioCodecCtx->sample_rate, pAudioCodecCtx->channel_layout,
                       pAudioCodecCtx->sample_fmt, pAudioCodecCtx->sample_rate, 0, NULL);
    //输出的声道个数
    int out_channel_nb = av_get_channel_layout_nb_channels(pAudioCodecCtx->channel_layout);
    uint8_t *out_buffer = (uint8_t *) av_malloc(2 * 44100);//保存的就是 16bit PCM  44.1kHZ的数据
    int got_frame = 0;
    ret = swr_init(swrCtx);
    if (ret < 0) {
        ALOGE("swresample init failed...");
        return;
    }


    // 新API 解码显示* @deprecated Use avcodec_send_packet() and avcodec_receive_frame().
    //avcodec_decode_video2 attribute_deprecated
//    pthread_t mVideoThread;
//    pthread_create(&mVideoThread, nullptr,playVideoThreadMethod,NULL);
//    pthread_join(mVideoThread,0);


    while (av_read_frame(pFormatCtx, packet) >= 0) {  // 调试发现，音频和视频是随机到来的
        // VideoPlay
        if (packet->stream_index == bestVideoStream) {
            ret = decodeVideo_newAPI();
        }
        // Audio Play
        if (bestAudioStream == packet->stream_index) {
            decodeAudio_newAPI(env, audioTrack, audioTrackWriteMethodID, out_buffer,
                               out_channel_nb);
        }
        av_packet_unref(packet);
        if (ret < 0 || nativePlayerStop)
            break;
    }

#if 1
    if (nativePlayerStop || av_read_frame(pFormatCtx, packet) < 0) {
        ALOGE("Free resource ,, buffer pframe pCodecCtx pFormatCtx");
        av_free(buffer);
        av_free(pFrameRGBA);
        // Free the YUV frame
        av_free(pFrame);
        // Close the codecs
        avcodec_close(pVideoCodecCtx);
        avcodec_close(pAudioCodecCtx);
        // Close the video file
        avformat_close_input(&pFormatCtx);
    }
#endif
}

void* playVideoThreadMethod(void*) {
    int ret;
    while (av_read_frame(pFormatCtx, packet) >= 0 || !nativePlayerStop) {  // 调试发现，音频和视频是随机到来的
        // VideoPlay
        if (packet->stream_index == AVMEDIA_TYPE_VIDEO) {
            ret = decodeVideo_newAPI();
        }

        av_packet_unref(packet);
        if (ret < 0)
            break;
    }

    return 0;
}


void JNICALL
Java_com_hua_nativeFFmpeg_NativeFFmpeg_nativePlay_1AudioOnly_1NewAPI(JNIEnv *env, jobject instance,
                                                                   jstring fileName_) {
    ALOGE("play Audio");
    const char *fileName = env->GetStringUTFChars(fileName_, 0);
    pFormatCtx = avformat_alloc_context();
    // Open video or Audio file
    if (avformat_open_input(&pFormatCtx, fileName, NULL, NULL) != 0) {
        ALOGE("Couldn't open file:%s\n", fileName);
        return; // Couldn't open file
    }
    // Retrieve stream information
    if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
        ALOGE("Couldn't find stream information.");
        return;
    }
    int bestAudioStream = av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    avAudioCodecParameters = pFormatCtx->streams[bestAudioStream]->codecpar;
    pAudioCodec = avcodec_find_decoder(avAudioCodecParameters->codec_id);

    pAudioCodecCtx = avcodec_alloc_context3(pAudioCodec);
    avcodec_parameters_to_context(pAudioCodecCtx, avAudioCodecParameters);
    if (avcodec_open2(pAudioCodecCtx, pAudioCodec, NULL) < 0) {
        ALOGE("Could not open codec.......");
        return; // Could not open codec
    }
    //Debug
    dumpAudioInfo(pAudioCodecCtx);
    // Find AudioTrack and Play the Sound
    jclass nativeFFmpegCls = env->GetObjectClass(instance);
    if (nativeFFmpegCls == NULL) {
        ALOGE("nativeFFmpegCls == null");
        return;
    }
    jmethodID createAudioTrackMethodID = env->GetMethodID(nativeFFmpegCls, "createAudioTrack",
                                                          "(II)Landroid/media/AudioTrack;");
    if (createAudioTrackMethodID == NULL) {
        ALOGE("createAudioTrackMethodID == null");
        return;
    }

    jobject audioTrack = env->CallObjectMethod(instance, createAudioTrackMethodID,
                                               pAudioCodecCtx->sample_rate,
                                               pAudioCodecCtx->channels);
    if (audioTrack == NULL) {
        ALOGE("audioTrack == null");
        return;
    }
    // 找到AudioTrack的  write 方法
    jclass audioTrackCls = env->GetObjectClass(audioTrack);
    jmethodID audioTrackWriteMethodID = env->GetMethodID(audioTrackCls, "write", "([BII)I");
    jmethodID audioTrackPlayMethodID = env->GetMethodID(audioTrackCls, "play", "()V");
    if (audioTrackWriteMethodID == NULL || audioTrackPlayMethodID == NULL) {
        ALOGE("audioTrackWriteMethodID audioTrackPlayMethodID == null");
        return;
    }
    env->CallVoidMethod(audioTrack, audioTrackPlayMethodID);

    // Allocate audio frame
    pFrame = av_frame_alloc();
    packet = av_packet_alloc();

    swrCtx = swr_alloc();//开辟空间
    //设置选项  start
    //输入的采样格式
    enum AVSampleFormat in_sample_fmt = pAudioCodecCtx->sample_fmt;
    //输出的采样格式16bit PCM
    enum AVSampleFormat out_sample_fmt = AV_SAMPLE_FMT_S16;
    //输入采样率
    int in_sample_rate = pAudioCodecCtx->sample_rate;
    //输出采样率  44100
    int out_sample_rate = in_sample_rate;

    int64_t in_ch_layout = pAudioCodecCtx->channel_layout;
    //获取输出的声道布局(默认立体声)
    int64_t out_ch_layout = AV_CH_LAYOUT_STEREO;

    swr_alloc_set_opts(swrCtx,
                       pAudioCodecCtx->channel_layout, AV_SAMPLE_FMT_S16,
                       pAudioCodecCtx->sample_rate,
                       pAudioCodecCtx->channel_layout, pAudioCodecCtx->sample_fmt,
                       pAudioCodecCtx->sample_rate,
                       0, NULL);
    //输出的声道个数
    int out_channel_nb = av_get_channel_layout_nb_channels(out_ch_layout);
    uint8_t *out_buffer = (uint8_t *) av_malloc(2 * 44100);//保存的就是 16bit PCM  44.1kHZ的数据
    int got_frame = 0;
    int ret = swr_init(swrCtx);
    if (ret < 0) {
        ALOGE("swresample init failed...");
        return;
    }
    // 新API 解码显示* @deprecated Use avcodec_send_packet() and avcodec_receive_frame().
    //avcodec_decode_video2 attribute_deprecated

    /* read frames from the file */

//    int read_frame = av_read_frame(pFormatCtx, pkt);
//    ALOGE("read_frame = %d",read_frame);              //0
//    ALOGE("stream_index = %d",  pkt->stream_index);   //0
//    ALOGE("size = %d",  pkt->size);                   // 417
//    ALOGE("duration = %d",  pkt->duration);              // 368640
    ALOGE("bestAudioStream = %d", bestAudioStream);              // 0  or   1

    while (av_read_frame(pFormatCtx, packet) >= 0) {
        if (bestAudioStream == packet->stream_index) {
            ret = decodeAudio_newAPI(env, audioTrack, audioTrackWriteMethodID, out_buffer,
                                     out_channel_nb);
        }
        av_packet_unref(packet);
        if (ret < 0 || nativePlayerStop)
            break;
    }

    env->ReleaseStringUTFChars(fileName_, fileName);
    av_frame_free(&pFrame);
    av_free(out_buffer);
    swr_free(&swrCtx);
    avcodec_close(pAudioCodecCtx);
    avformat_close_input(&pFormatCtx);
}


void JNICALL
Java_com_hua_nativeFFmpeg_NativeFFmpeg_nativePlayAudioVideo(JNIEnv *env, jobject instance,
                                                            jstring input,
                                                            jobject surface) {

    const char *file_name = env->GetStringUTFChars(input, 0);
    ALOGD("play AV");
    AVFormatContext *pFormatCtx = avformat_alloc_context();
    // Open AV file
    if (avformat_open_input(&pFormatCtx, file_name, NULL, NULL) != 0) {
        ALOGE("Couldn't open file:%s\n", file_name);
        return;
    }
    // Retrieve stream information
    if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
        ALOGE("Couldn't find stream information.");
        return;
    }


    int bestVideoStream = av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    int bestAudioStream = av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    if (bestVideoStream == -1) {
        ALOGE("Didn't find a video stream.");
        return; // Didn't find a video stream
    }
    if (bestAudioStream == -1) {
        ALOGE("Didn't find a audio stream.");
        return; // Didn't find a audio stream
    }
    AVCodecParameters *avVideoCodecParameters = pFormatCtx->streams[bestVideoStream]->codecpar;
    AVCodecParameters *avAudioCodecParameters = pFormatCtx->streams[bestAudioStream]->codecpar;
    AVCodec *pVideoCodec = avcodec_find_decoder(avVideoCodecParameters->codec_id);
    AVCodec *pAudioCodec = avcodec_find_decoder(avAudioCodecParameters->codec_id);
    if (pVideoCodec == NULL || pAudioCodec == NULL) {
        ALOGE("Codec not found.");
        return; // Codec not found
    }
    AVCodecContext *pVideoCodecCtx = avcodec_alloc_context3(pVideoCodec);
    AVCodecContext *pAudioCodecCtx = avcodec_alloc_context3(pAudioCodec);
    if (!pVideoCodecCtx || !pAudioCodecCtx) {
        ALOGE("Codec not alloc CodecContext.");
        return; // Codec not found
    }
    int ret = avcodec_parameters_to_context(pVideoCodecCtx, avVideoCodecParameters);  // 这一步也是必须的
    ret = avcodec_parameters_to_context(pAudioCodecCtx, avAudioCodecParameters);  // 这一步也是必须的
    if (ret) {
        ALOGE("Can't copy decoder context");
        return;
    }


    if (avcodec_open2(pVideoCodecCtx, pVideoCodec, NULL) < 0 ||
        avcodec_open2(pAudioCodecCtx, pAudioCodec, NULL) < 0) {
        ALOGE("Could not open videoCodec or audioCodec");
        return; // Could not open codec
    }


    // Init AudioTrack...
    jclass nativeFFmpegCls = env->GetObjectClass(instance);
    if (nativeFFmpegCls == NULL) {
        ALOGE("nativeFFmpegCls == null");
        return;
    }
    jmethodID createAudioTrackMethodID = env->GetMethodID(nativeFFmpegCls, "createAudioTrack",
                                                          "(II)Landroid/media/AudioTrack;");
    if (createAudioTrackMethodID == NULL) {
        ALOGE("createAudioTrackMethodID == null");
        return;
    }

    jobject audioTrack = env->CallObjectMethod(instance, createAudioTrackMethodID,
                                               pAudioCodecCtx->sample_rate,
                                               pAudioCodecCtx->channels);
    if (audioTrack == NULL) {
        ALOGE("audioTrack == null");
        return;
    }
    // 找到AudioTrack的  write 方法
    jclass audioTrackCls = env->GetObjectClass(audioTrack);
    jmethodID audioTrackWriteMethodID = env->GetMethodID(audioTrackCls, "write", "([BII)I");
    jmethodID audioTrackPlayMethodID = env->GetMethodID(audioTrackCls, "play", "()V");
    if (audioTrackWriteMethodID == NULL || audioTrackPlayMethodID == NULL) {
        ALOGE("audioTrackWriteMethodID audioTrackPlayMethodID == null");
        return;
    }
    env->CallVoidMethod(audioTrack, audioTrackPlayMethodID);
    // End AudioTrack

    // Video
    // 获取native window
    ANativeWindow *nativeWindow = ANativeWindow_fromSurface(env, surface);
    // 获取视频宽高
    int videoWidth = pVideoCodecCtx->width;
    int videoHeight = pVideoCodecCtx->height;

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
    int numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGBA, pVideoCodecCtx->width,
                                            pVideoCodecCtx->height,
                                            1);
    uint8_t *buffer = (uint8_t *) av_malloc(numBytes * sizeof(uint8_t));
    av_image_fill_arrays(pFrameRGBA->data, pFrameRGBA->linesize, buffer, AV_PIX_FMT_RGBA,
                         pVideoCodecCtx->width, pVideoCodecCtx->height, 1);

    // 由于解码出来的帧格式不是RGBA的,在渲染之前需要进行格式转换
    struct SwsContext *sws_ctx = sws_getContext(pVideoCodecCtx->width,
                                                pVideoCodecCtx->height,
                                                pVideoCodecCtx->pix_fmt,
                                                pVideoCodecCtx->width,
                                                pVideoCodecCtx->height,
                                                AV_PIX_FMT_RGBA,
                                                SWS_BILINEAR,
                                                NULL,
                                                NULL,
                                                NULL);

    AVPacket *pkt = av_packet_alloc();
    // Audio 重采样
    struct SwrContext *swrCtx = swr_alloc();//开辟空间

//    ALOGE("############### channels %d",pAudioCodecCtx->channels);
//    int64_t out_ch_layout = AV_CH_LAYOUT_STEREO;
//    ALOGE("############### out_ch_layout %d",out_ch_layout);
//    ALOGE("############### channel_layout %d",pAudioCodecCtx->channel_layout);
    ALOGE("############### AV_SAMPLE_FMT_S16 %d", pAudioCodecCtx->sample_fmt);
    swr_alloc_set_opts(swrCtx, pAudioCodecCtx->channel_layout, AV_SAMPLE_FMT_S16,
                       pAudioCodecCtx->sample_rate, pAudioCodecCtx->channel_layout,
                       pAudioCodecCtx->sample_fmt, pAudioCodecCtx->sample_rate, 0, NULL);
    //输出的声道个数
    int out_channel_nb = av_get_channel_layout_nb_channels(pAudioCodecCtx->channel_layout);
    uint8_t *out_buffer = (uint8_t *) av_malloc(2 * 44100);//保存的就是 16bit PCM  44.1kHZ的数据
    int got_frame = 0;
    ret = swr_init(swrCtx);
    if (ret < 0) {
        ALOGE("swresample init failed...");
        return;
    }


    // 新API 解码显示* @deprecated Use avcodec_send_packet() and avcodec_receive_frame().
    //avcodec_decode_video2 attribute_deprecated

    /* read frames from the file */
    while (av_read_frame(pFormatCtx, pkt) >= 0) {  // 调试发现，音频和视频是随机到来的

//        if(pkt->stream_index == 0 || pkt->stream_index ==  1)
//            ALOGE("Stream.......... %d",pkt->stream_index);
        if (pkt->stream_index == bestVideoStream) {
            int ret = avcodec_send_packet(pVideoCodecCtx, pkt);
            if (ret < 0) {
                ALOGE("Error while sending a packet to the decoder: %s", av_err2str(ret));
                return;
            }
            while (ret >= 0) {
                ret = avcodec_receive_frame(pVideoCodecCtx, pFrame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                } else if (ret < 0) {
                    ALOGE("Error while receiving a frame from the decoder: %s", av_err2str(ret));
                    return;
                }
                if (ret >= 0 && !nativePlayerStop) {
                    // Draw in native window
                    ANativeWindow_lock(nativeWindow, &windowBuffer, 0);

                    // 格式转换,  这一步是必须的
                    sws_scale(sws_ctx, (uint8_t const *const *) pFrame->data,
                              pFrame->linesize, 0, pVideoCodecCtx->height,
                              pFrameRGBA->data, pFrameRGBA->linesize);

                    uint8_t *dst = (uint8_t *) windowBuffer.bits;//WindowBuff 要显示的地址
                    int dstStride = windowBuffer.stride * 4;   // 一行所占的像素大小 ，因为是 AGRB ，所以是32位，4个字节
                    uint8_t *src = (pFrameRGBA->data[0]);      // 原始数据
                    int srcStride = pFrameRGBA->linesize[0];   // 图片一行的大小
                    // 由于window的stride和帧的stride不同,因此需要逐行复制
                    for (int h = 0; h < videoHeight; h++) {
                        uint8_t *srcAddr = dst + dstStride * h;
                        memcpy(dst + dstStride * h, src + h * srcStride, srcStride);
                    }
                    ANativeWindow_unlockAndPost(nativeWindow);
//                    usleep(1000*16);
                }
            }
        }

        if (bestAudioStream == pkt->stream_index) {
            ret = avcodec_decode_audio4(pAudioCodecCtx, pFrame, &got_frame, pkt);
            if (ret < 0) {
                ALOGE("decode_audio over");
            }
            if (got_frame > 0) {
                swr_convert(swrCtx, &out_buffer, 2 * 44100, (const uint8_t **) pFrame->data,
                            pFrame->nb_samples);
                int out_buffer_size = av_samples_get_buffer_size(NULL,
                                                                 out_channel_nb,
                                                                 pFrame->nb_samples,
                                                                 AV_SAMPLE_FMT_S16
                        /*pAudioCodecCtx->sample_fmt*/, 1);

                jbyteArray data_array = env->NewByteArray(out_buffer_size);
                jbyte *sample_byte = env->GetByteArrayElements(data_array, 0);
                memcpy(sample_byte, out_buffer, out_buffer_size);
                env->ReleaseByteArrayElements(data_array, sample_byte, 0);
                env->CallIntMethod(audioTrack, audioTrackWriteMethodID, data_array, 0,
                                   out_buffer_size);
                env->DeleteLocalRef(data_array);
//                usleep(1000*16);
            }

        }

        if (nativePlayerStop) {
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
        ALOGE("Free resource ,, buffer pframe pVideoCodecCtx pFormatCtx");
        av_free(buffer);
        av_free(pFrameRGBA);
        // Free the YUV frame
        av_free(pFrame);
        // Close the codecs
        avcodec_close(pVideoCodecCtx);
        avcodec_close(pAudioCodecCtx);
        // Close the video file
        avformat_close_input(&pFormatCtx);
    }

}


void JNICALL
Java_com_hua_nativeFFmpeg_NativeFFmpeg_nativePlay_1VideoOnly_1NewAPI(JNIEnv *env, jobject instance,
                                                              jstring input,
                                                              jobject surface) {
    const char *file_name = env->GetStringUTFChars(input, 0);
    ALOGD("play");

    pFormatCtx = avformat_alloc_context();
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
    avVideoCodecParameters = pFormatCtx->streams[bestVideoStream]->codecpar;
    pVideoCodec = avcodec_find_decoder(avVideoCodecParameters->codec_id);
    if (pVideoCodec == NULL) {
        ALOGE("Codec not found.");
        return; // Codec not found
    }
    pVideoCodecCtx = avcodec_alloc_context3(pVideoCodec);
    if (!pVideoCodecCtx) {
        ALOGE("Codec not alloc CodecContext.");
        return; // Codec not found
    }
    int ret = avcodec_parameters_to_context(pVideoCodecCtx, avVideoCodecParameters);  // 这一步也是必须的
    if (ret) {
        ALOGE("Can't copy decoder context");
        return;
    }
    pAVVideoCodecParserContext = av_parser_init(pVideoCodec->id);
    if (!pAVVideoCodecParserContext) {
        ALOGE("av_parser_init Failed");
        return;
    }
    if (avcodec_open2(pVideoCodecCtx, pVideoCodec, NULL) < 0) {
        ALOGE("Could not open codec.");
        return; // Could not open codec
    }
    // 获取native window
    nativeWindow = ANativeWindow_fromSurface(env, surface);

    // 获取视频宽高
    int videoWidth = pVideoCodecCtx->width;
    int videoHeight = pVideoCodecCtx->height;

    // 设置native window的buffer大小,可自动拉伸
    ANativeWindow_setBuffersGeometry(nativeWindow, videoWidth, videoHeight,
                                     WINDOW_FORMAT_RGBA_8888);
    // Allocate video frame
    pFrame = av_frame_alloc();

    // 用于渲染
    pFrameRGBA = av_frame_alloc();
    if (pFrameRGBA == NULL || pFrame == NULL) {
        ALOGE("Could not allocate video frame.");
        return;
    }
    // Determine required buffer size and allocate buffer
    // buffer中数据就是用于渲染的,且格式为RGBA
    int numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGBA, pVideoCodecCtx->width,
                                            pVideoCodecCtx->height,
                                            1);
    uint8_t *buffer = (uint8_t *) av_malloc(numBytes * sizeof(uint8_t));
    av_image_fill_arrays(pFrameRGBA->data, pFrameRGBA->linesize, buffer, AV_PIX_FMT_RGBA,
                         pVideoCodecCtx->width, pVideoCodecCtx->height, 1);

    // 由于解码出来的帧格式不是RGBA的,在渲染之前需要进行格式转换
    sws_ctx = sws_getContext(pVideoCodecCtx->width,
                             pVideoCodecCtx->height,
                             pVideoCodecCtx->pix_fmt,
                             pVideoCodecCtx->width,
                             pVideoCodecCtx->height,
                             AV_PIX_FMT_RGBA,
                             SWS_BILINEAR,
                             NULL,
                             NULL,
                             NULL);
    packet = av_packet_alloc();

    // 新API 解码显示* @deprecated Use avcodec_send_packet() and avcodec_receive_frame().
    //avcodec_decode_video2 attribute_deprecated

    /* read frames from the file */
    while (av_read_frame(pFormatCtx, packet) >= 0) {
        if (packet->stream_index == bestVideoStream && !nativePlayerStop) {
            ret = decodeVideo_newAPI();
        }
        av_packet_unref(packet);
        if (ret < 0 || nativePlayerStop) break;
    }


    if (nativePlayerStop || av_read_frame(pFormatCtx, packet) < 0) {
        ALOGE("Free resource ,, buffer pframe pVideoCodecCtx pFormatCtx");
        av_free(buffer);
        av_free(pFrameRGBA);
        // Free the YUV frame
        av_free(pFrame);
        // Close the codecs
        avcodec_close(pVideoCodecCtx);
        // Close the video file
        avformat_close_input(&pFormatCtx);
    }
}

void dumpAudioInfo(const AVCodecContext *pAudioCodecContext) {
    ALOGD("sample_rate = %d ", pAudioCodecContext->sample_rate);  // 44100
    ALOGD("bit_rate = %d", pAudioCodecContext->bit_rate);        // 32k
    ALOGD("channels = %d ", pAudioCodecContext->channels);        // 2 stereo
    ALOGD("codecName = %s ", pAudioCodecContext->codec->name);   // mp3float
    ALOGD("sample_fmt = %d ", pAudioCodecContext->sample_fmt);   // mp3float

}


void JNICALL
Java_com_hua_nativeFFmpeg_NativeFFmpeg_native_Play_1VideoOnly_1OldAPI(JNIEnv *env, jobject instance,
                                                              jstring input,
                                                              jobject surface) {
    const char *file_name = env->GetStringUTFChars(input, 0);
    ALOGD("play");

    pFormatCtx = avformat_alloc_context();
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
    avVideoCodecParameters = pFormatCtx->streams[bestVideoStream]->codecpar;
    pVideoCodec = avcodec_find_decoder(avVideoCodecParameters->codec_id);
    if (pVideoCodec == NULL) {
        ALOGE("Codec not found.");
        return; // Codec not found
    }
    pVideoCodecCtx = avcodec_alloc_context3(pVideoCodec);
    if (!pVideoCodecCtx) {
        ALOGE("Codec not alloc CodecContext.");
        return; // Codec not found
    }
    int ret = avcodec_parameters_to_context(pVideoCodecCtx, avVideoCodecParameters);  // 这一步也是必须的
    if (ret) {
        ALOGE("Can't copy decoder context");
        return;
    }
    if (avcodec_open2(pVideoCodecCtx, pVideoCodec, NULL) < 0) {
        ALOGE("Could not open codec.");
        return; // Could not open codec
    }

    // 获取native window
    nativeWindow = ANativeWindow_fromSurface(env, surface);

    // 获取视频宽高
    int videoWidth = pVideoCodecCtx->width;
    int videoHeight = pVideoCodecCtx->height;

    // 设置native window的buffer大小,可自动拉伸
    ANativeWindow_setBuffersGeometry(nativeWindow, videoWidth, videoHeight,
                                     WINDOW_FORMAT_RGBA_8888);

    // Allocate video frame
    pFrame = av_frame_alloc();

    // 用于渲染
    pFrameRGBA = av_frame_alloc();
    if (pFrameRGBA == NULL || pFrame == NULL) {
        ALOGE("Could not allocate video frame.");
        return;
    }

    // Determine required buffer size and allocate buffer
    // buffer中数据就是用于渲染的,且格式为RGBA
    int numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGBA, pVideoCodecCtx->width,
                                            pVideoCodecCtx->height,
                                            1);
    uint8_t *buffer = (uint8_t *) av_malloc(numBytes * sizeof(uint8_t));
    av_image_fill_arrays(pFrameRGBA->data, pFrameRGBA->linesize, buffer, AV_PIX_FMT_RGBA,
                         pVideoCodecCtx->width, pVideoCodecCtx->height, 1);

    // 由于解码出来的帧格式不是RGBA的,在渲染之前需要进行格式转换
    sws_ctx = sws_getContext(pVideoCodecCtx->width,
                             pVideoCodecCtx->height,
                             pVideoCodecCtx->pix_fmt,
                             pVideoCodecCtx->width,
                             pVideoCodecCtx->height,
                             AV_PIX_FMT_RGBA,
                             SWS_BILINEAR,
                             NULL,
                             NULL,
                             NULL);

    packet = av_packet_alloc();
    while (av_read_frame(pFormatCtx, packet) >= 0) {
        // Is this a packet from the video stream?
        if (packet->stream_index == bestVideoStream && !nativePlayerStop) {
            // Decode video frame
            decodeVideo_oldAPI();
        }
        av_packet_unref(packet);
    }
    if (nativePlayerStop || av_read_frame(pFormatCtx, packet) < 0) {
        ALOGE("Free resource ,, buffer pframe pVideoCodecCtx pFormatCtx");
        av_free(buffer);
        av_free(pFrameRGBA);
        // Free the YUV frame
        av_free(pFrame);
        // Close the codecs
        avcodec_close(pVideoCodecCtx);
        // Close the video file
        avformat_close_input(&pFormatCtx);
    }
}

static int decodeAudio_newAPI(JNIEnv *env, jobject audioTrack, jmethodID audioTrackWriteMethodID,
                              uint8_t *out_buffer, int out_channel_nb) {

    int ret = avcodec_send_packet(pAudioCodecCtx, packet);
    if (ret < 0) {
        ALOGE("Error while sending a packet to the decoder: %s\n",
              av_err2str(ret));
    }

    while (ret >= 0) {
        ret = avcodec_receive_frame(pAudioCodecCtx, pFrame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        } else if (ret < 0) {
            ALOGE("Error while receiving a frame from the decoder: %s\n",
                  av_err2str(ret));
        }
        if (ret >= 0) {
            swr_convert(swrCtx, &out_buffer, 2 * 44100, (const uint8_t **) pFrame->data,
                        pFrame->nb_samples);
            int out_buffer_size = av_samples_get_buffer_size(NULL,
                                                             out_channel_nb,
                                                             pFrame->nb_samples,
                                                             AV_SAMPLE_FMT_S16
                    /*pAudioCodecCtx->sample_fmt*/, 1);
            jbyteArray data_array = env->NewByteArray(out_buffer_size);
            jbyte *sample_byte = env->GetByteArrayElements(data_array, 0);
            memcpy(sample_byte, out_buffer, out_buffer_size);
            env->ReleaseByteArrayElements(data_array, sample_byte, 0);
            env->CallIntMethod(audioTrack, audioTrackWriteMethodID, data_array, 0, out_buffer_size);
            env->DeleteLocalRef(data_array);

        }
    }
    return 0;
}


static int decodeVideo_newAPI() {


    int ret = avcodec_send_packet(pVideoCodecCtx, packet);
    if (ret < 0) {
        ALOGE("Error while sending a packet to the decoder: %s", av_err2str(ret));
        return ret;
    }
    while (ret >= 0) {
        ret = avcodec_receive_frame(pVideoCodecCtx, pFrame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        } else if (ret < 0) {
            ALOGE("Error while receiving a frame from the decoder: %s", av_err2str(ret));
            return ret;
        }
        if (ret >= 0 && !nativePlayerStop) {
            // Draw in native window
            ANativeWindow_lock(nativeWindow, &windowBuffer, 0);

            // 格式转换,  这一步是必须的
            sws_scale(sws_ctx, (uint8_t const *const *) pFrame->data,
                      pFrame->linesize, 0, pVideoCodecCtx->height,
                      pFrameRGBA->data, pFrameRGBA->linesize);

            uint8_t *dst = (uint8_t *) windowBuffer.bits;//WindowBuff 要显示的地址
            int dstStride = windowBuffer.stride * 4;   // 一行所占的像素大小 ，因为是 AGRB ，所以是32位，4个字节
            uint8_t *src = (pFrameRGBA->data[0]);      // 原始数据
            int srcStride = pFrameRGBA->linesize[0];   // 图片一行的大小
            // 由于window的stride和帧的stride不同,因此需要逐行复制
            for (int h = 0; h < pVideoCodecCtx->height; h++) {
                memcpy(dst + dstStride * h, src + h * srcStride, srcStride);
            }
            ANativeWindow_unlockAndPost(nativeWindow);
//            usleep(1000*16);
        }
    }
    return 0;
}

void decodeVideo_oldAPI() {
    avcodec_decode_video2(pVideoCodecCtx, pFrame, &frameFinished, packet);
    // 并不是decode一次就可解码出一帧
    if (frameFinished) {
        // lock native window buffer
        ANativeWindow_lock(nativeWindow, &windowBuffer, 0);
        // 格式转换
        sws_scale(sws_ctx, (uint8_t const *const *) pFrame->data,
                  pFrame->linesize, 0, pVideoCodecCtx->height,
                  pFrameRGBA->data, pFrameRGBA->linesize);

        // 获取stride
        uint8_t *dst = (uint8_t *) windowBuffer.bits;
        int dstStride = windowBuffer.stride * 4;
        uint8_t *src = (pFrameRGBA->data[0]);
        int srcStride = pFrameRGBA->linesize[0];
        // 由于window的stride和帧的stride不同,因此需要逐行复制
        for (int h = 0; h < pVideoCodecCtx->height; h++) {
            memcpy(dst + h * dstStride, src + h * srcStride, srcStride);
        }
        ANativeWindow_unlockAndPost(nativeWindow);
    }
}


void JNICALL
Java_com_hua_nativeFFmpeg_NativeFFmpeg_nativePlayStop(JNIEnv *env, jobject instance,
                                                      jboolean stop) {
    ALOGE("Need native Player stop %s", stop == 1 ? "true" : "false");
    nativePlayerStop = stop;
}

