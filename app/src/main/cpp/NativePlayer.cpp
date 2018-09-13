//
// Created by FC5981 on 2018/9/3.
//
#include <sys/types.h>
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

void dumpAudioInfo(const AVCodecContext *pAudioCodecContext);





void JNICALL
Java_com_hua_nativeFFmpeg_NativeFFmpeg_nativePlayAudio(JNIEnv *env, jobject instance,
                                                       jstring fileName_, jobject surface) {
    ALOGE("play Audio");
    const char *fileName = env->GetStringUTFChars(fileName_, 0);
    AVFormatContext *pFormatCtx = avformat_alloc_context();
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
    AVCodecParameters *pAudioCodecParameters = pFormatCtx->streams[bestAudioStream]->codecpar;
    AVCodec *pAudioCodec = avcodec_find_decoder(pAudioCodecParameters->codec_id);
    AVCodecParserContext *pAudioCodecParseContext = av_parser_init(pAudioCodecParameters->codec_id);

    AVCodecContext *pAudioCodecContext = avcodec_alloc_context3(pAudioCodec);
    avcodec_parameters_to_context(pAudioCodecContext, pAudioCodecParameters);
    if (avcodec_open2(pAudioCodecContext, pAudioCodec, NULL) < 0) {
        ALOGE("Could not open codec.......");
        return; // Could not open codec
    }
    //Debug
    dumpAudioInfo(pAudioCodecContext);

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
                                               pAudioCodecContext->sample_rate,
                                               pAudioCodecContext->channels);
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
    env->CallVoidMethod(audioTrack,audioTrackPlayMethodID);

    // Allocate audio frame
    AVFrame *frame = av_frame_alloc();
    AVPacket *pkt = av_packet_alloc();

    SwrContext *swrCtx = swr_alloc();//开辟空间
    //设置选项  start
    //输入的采样格式
    enum AVSampleFormat in_sample_fmt = pAudioCodecContext->sample_fmt;
    //输出的采样格式16bit PCM
    enum AVSampleFormat out_sample_fmt = AV_SAMPLE_FMT_S16;
    //输入采样率
    int in_sample_rate = pAudioCodecContext->sample_rate;
    //输出采样率  44100
    int out_sample_rate = in_sample_rate;

    int64_t in_ch_layout = pAudioCodecContext->channel_layout;
    //获取输出的声道布局(默认立体声)
    int64_t out_ch_layout = AV_CH_LAYOUT_STEREO;

    swr_alloc_set_opts(swrCtx,
                       out_ch_layout, out_sample_fmt, out_sample_rate,
                       in_ch_layout, in_sample_fmt, in_sample_rate,
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
    ALOGE("bestAudioStream = %d", bestAudioStream);              // 0

    while (av_read_frame(pFormatCtx,pkt) >= 0) {
        if (bestAudioStream == pkt->stream_index) {
           ret = avcodec_decode_audio4(pAudioCodecContext,frame,&got_frame,pkt);
            if(ret < 0) {
                ALOGE("decode_audio over");
            }
            if(got_frame > 0) {
                swr_convert(swrCtx,&out_buffer,2*44100,(const uint8_t **)frame->data,frame->nb_samples);
                int out_buffer_size = av_samples_get_buffer_size(NULL,
                                                                 out_channel_nb,
                                                                 frame->nb_samples,
                                                                 out_sample_fmt, 1);

                jbyteArray data_array = env->NewByteArray(out_buffer_size);
                jbyte *sample_byte = env->GetByteArrayElements(data_array, 0);
                memcpy(sample_byte,out_buffer,out_buffer_size);
                env->ReleaseByteArrayElements(data_array,sample_byte,0);
                env->CallIntMethod(audioTrack,audioTrackWriteMethodID,data_array,0,out_buffer_size);
                env->DeleteLocalRef(data_array);
                usleep(1000*16);
//                ALOGE("AudioTrack Write out_buffer_size %d" ,out_buffer_size);
            }

        }
        av_packet_unref(pkt);
    }


    env->ReleaseStringUTFChars(fileName_, fileName);
    av_frame_free(&frame);
    av_free(out_buffer);
    swr_free(&swrCtx);
    avcodec_close(pAudioCodecContext);
    avformat_close_input(&pFormatCtx);
}








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
        if (pkt->stream_index == bestVideoStream) {
            int ret = avcodec_send_packet(pCodecCtx, pkt);
            if (ret < 0) {
                ALOGE("Error while sending a packet to the decoder: %s", av_err2str(ret));
                return;
            }
            while (ret >= 0) {
                ret = avcodec_receive_frame(pCodecCtx, pFrame);
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
                              pFrame->linesize, 0, pCodecCtx->height,
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
                }
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

void dumpAudioInfo(const AVCodecContext *pAudioCodecContext) {
    ALOGD("sample_rate = %d ", pAudioCodecContext->sample_rate);  // 44100
    ALOGD("bit_rate = %d", pAudioCodecContext->bit_rate);        // 32k
    ALOGD("channels = %d ", pAudioCodecContext->channels);        // 2 stereo
    ALOGD("codecName = %s ", pAudioCodecContext->codec->name);   // mp3float



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
            if (nativePlayerStop) {
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

