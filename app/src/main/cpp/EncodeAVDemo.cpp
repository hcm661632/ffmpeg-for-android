//
// Created by FC5981 on 2018/8/31.
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

jobject globalFFmpegRef;
jobject globalAudioEncodeListener;

JavaVM *global_VM;



static void *
beginEncodeAudio(AVCodecContext *avCodecContext, AVFrame *frame, AVPacket *pkt, FILE *f);

void createNativeThread(void *);
void createNativeThread2(void *argc);

typedef struct encodeAudio {
    AVCodecContext *avCodecContext;
    AVFrame *frame;
    AVPacket *pkt;
    FILE *f;
} encodeAudio;


static int check_sample_fmt(const AVCodec *codec, enum AVSampleFormat sampleFormat) {
    const AVSampleFormat *sample_fmts = codec->sample_fmts; // 所有支持的SampleFormat
    while (*sample_fmts != AV_SAMPLE_FMT_NONE) {
        if (sampleFormat == *sample_fmts) {
            return 1;
        }
        sample_fmts++;
    }
    return 0;
}


/* just pick the highest supported samplerate */
static int select_sample_rate(const AVCodec *codec) {
    const int *p;
    int best_samplerate = 0;
    if (!codec->supported_samplerates)
        return 44100;

    p = codec->supported_samplerates;
    while (*p) {
        if (!best_samplerate || abs(44100 - *p) < abs(44100 - best_samplerate))
            best_samplerate = *p;
        p++;
    }
    return best_samplerate;
}


/* select layout with the highest channel count */
static int select_channel_layout(const AVCodec *codec) {
    const uint64_t *p;
    uint64_t best_ch_layout = 0;
    int best_nb_channels = 0;

    if (!codec->channel_layouts)
        return AV_CH_LAYOUT_STEREO;

    p = codec->channel_layouts;
    while (*p) {
        int nb_channels = av_get_channel_layout_nb_channels(*p);

        if (nb_channels > best_nb_channels) {
            best_ch_layout = *p;
            best_nb_channels = nb_channels;
        }
        p++;
    }
    return best_ch_layout;
}


static void encode(AVCodecContext *ctx, AVFrame *frame, AVPacket *pkt,
                   FILE *output) {
    int ret;
    /* send the frame for encoding */
    ret = avcodec_send_frame(ctx, frame);
    if (ret < 0) {
        fprintf(stderr, "Error sending the frame to the encoder\n");
        exit(1);
    }

    /* read all the available output packets (in general there may be any
     * number of them */
    while (ret >= 0) {
        ret = avcodec_receive_packet(ctx, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            ALOGD("%d, AVERROR_EOF %d", ret, AVERROR_EOF);
            return;
        } else if (ret < 0) {
            fprintf(stderr, "Error encoding audio frame\n");
            exit(1);
        }

        fwrite(pkt->data, 1, pkt->size, output);
        av_packet_unref(pkt);
    }
}

/**
 *
 * @param argc
 * @return 这个一定要有。。
 */
static void *encodeAudioThreadMthod(void *argc) {
    const AVCodec *avCodec;
    AVCodecContext *avCodecContext = NULL;
    AVFrame *frame;
    AVPacket *pkt;
    int ret;
    FILE *f;
    uint16_t *samples;
    float t, tincr;
    const char *filename = "/sdcard/hh/encodeAudioDemo.mp3";
//     Find the MP2/MP3 encoder
    avCodec = avcodec_find_encoder(AV_CODEC_ID_MP2);

    ALOGE("tid = %d",gettid());

    if (!avCodec) {
        ALOGE("%s", "Codec not found");
        exit(1);
    }
    avCodecContext = avcodec_alloc_context3(avCodec);
    if (!avCodecContext) {
        ALOGE("%s", "Could not allocate audio codec context");
        exit(1);
    }
    // 这个有什么用呢， SAMPLE_FMT_S16
    avCodecContext->sample_fmt = AV_SAMPLE_FMT_S16;
    if (!check_sample_fmt(avCodec, avCodecContext->sample_fmt)) {
        ALOGE("Encoder does not support sample format %s",
              av_get_sample_fmt_name(avCodecContext->sample_fmt));
        exit(1);
    }
//     select other audio parameters supported by the encoder

    avCodecContext->sample_rate = select_sample_rate(avCodec);//sampleRate;
    avCodecContext->bit_rate = 64000;
    avCodecContext->channel_layout = select_channel_layout(avCodec); //best_ch_layout;
    avCodecContext->channels = av_get_channel_layout_nb_channels(avCodecContext->channel_layout);

//    Open it
    if (avcodec_open2(avCodecContext, avCodec, NULL) < 0) {
        ALOGE("Could not open codec");
        exit(1);
    }

    int fd = open(filename, O_WRONLY);
    if (fd < 0) {
        ALOGE("Could not open %s", filename);
        exit(1);
    }
    //" wb "  write binary
    f = fdopen(fd, "wb");

    /* packet for holding encoded output */
    pkt = av_packet_alloc();
    if (!pkt) {
        ALOGE("Could not av_packet_alloc");
        exit(1);
    }
    frame = av_frame_alloc();
    if (!frame) {
        ALOGE("Could not av_frame_alloc");
        exit(1);
    }
//    select other audio parameters supported by the encoder
    frame->nb_samples = avCodecContext->frame_size;
    frame->channel_layout = avCodecContext->channel_layout;
    frame->format = avCodecContext->sample_fmt;

    // allocate the data buffers
    ret = av_frame_get_buffer(frame, 0);
    if (ret < 0) {
        ALOGE("Could not allocate audio data buffers %s", av_err2str(ret));
        exit(1);
    }

    //TODO
    t = 0;
    tincr = 2 * M_PI * 440.0 / avCodecContext->sample_rate;

    //获取当前native线程是否有没有被附加到jvm环境中
    ALOGE("Begin Encode Audio");


    jint nativeThreadAttatch = JNI_FALSE;
    JNIEnv *globalEnv;
    jint getEnv = global_VM->GetEnv((void **) &globalEnv, JNI_VERSION_1_6);
    ALOGE("tid = %d,getEnv = %d\n", gettid(), getEnv);   // 这里确实是 -2 ，没有被加到当前线程中
    if (getEnv == JNI_EDETACHED) {
        ALOGE("Current native thread has not attach to jvm");
        if (global_VM->AttachCurrentThread(&globalEnv, NULL) == 0) {
            nativeThreadAttatch = JNI_TRUE;
        } else {
            exit(0);
        }
    }
    jclass ffmpegClass = (globalEnv)->GetObjectClass(globalFFmpegRef);
    jclass listenerClass = globalEnv->GetObjectClass(globalAudioEncodeListener);
    if (ffmpegClass == NULL) {
        ALOGE("No such class");
    }
    jmethodID methodID = (globalEnv)->GetMethodID(ffmpegClass, "getEncodeProcess", "(I)V");
    jmethodID callbackMethod = globalEnv->GetMethodID(listenerClass, "nowProgress", "(D)V");
    jmethodID callbackMethodOver = globalEnv->GetMethodID(listenerClass, "audioEncodeOver", "(Z)V");
    if (methodID == NULL || callbackMethod == NULL || callbackMethodOver == NULL) {
        ALOGE("No such method");
        global_VM->DetachCurrentThread();
    }


    float count = 200;
    for (int i = 0; i <  count; ++i) {
        /* make sure the frame is writable -- makes a copy if the encoder
         * kept a reference internally */
        ret = av_frame_make_writable(frame);
        if (ret < 0) {
            ALOGE("av_frame_make_writable");
            exit(1);
        }
        usleep(1000 * 100); // 100ms   , total = 100ms * 200 = 20s , ANR
        uint16_t *samples = (uint16_t *) frame->data[0];
        for (int j = 0; j < avCodecContext->frame_size; ++j) {
            samples[2 * j] = sin(t) * 10000;

            for (int k = 1; k < avCodecContext->channels; ++k) {
                samples[2 * j + k] = samples[2 * j];
            }
            t += tincr;
        }
        encode(avCodecContext, frame, pkt, f);
        globalEnv->CallVoidMethod(globalAudioEncodeListener,callbackMethod,i / count * 100);
    }
    globalEnv->CallVoidMethod(globalAudioEncodeListener,callbackMethodOver,true);
    // flush the encoder
    encode(avCodecContext, NULL, pkt, f);
    ALOGM("######################  END #########################");

    if (nativeThreadAttatch) {
        // 释放全局引用
        globalEnv->DeleteGlobalRef(globalFFmpegRef);
        globalEnv->DeleteGlobalRef(globalAudioEncodeListener);
        globalEnv = NULL;
        global_VM->DetachCurrentThread();

    }
    //TODO
    fclose(f);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&avCodecContext);

    return NULL;
}


static void *encodeAudioMethod(void *argc) {
    ALOGM("Thread start");
    struct encodeAudio *audio = (struct encodeAudio *) argc;

    beginEncodeAudio(audio->avCodecContext,
                     audio->frame,
                     audio->pkt,
                     audio->f);


    fclose(audio->f);

    av_frame_free(&audio->frame);
    av_packet_free(&audio->pkt);
    avcodec_free_context(&audio->avCodecContext);

    pthread_exit(NULL);
    return NULL;
}


static void *
beginEncodeAudio(AVCodecContext *avCodecContext, AVFrame *frame, AVPacket *pkt, FILE *f) {
    int ret = 0;
    float t = 0, tincr = 2 * M_PI * 440.0 / avCodecContext->sample_rate;

    //获取当前native线程是否有没有被附加到jvm环境中
    ALOGE("Begin Encode Audio");
    jint nativeThreadAttatch = JNI_FALSE;
    JNIEnv *globalEnv;
    jint getEnv = global_VM->GetEnv((void **) &globalEnv, JNI_VERSION_1_6);
    ALOGE("tid = %d,getEnv = %d\n", gettid(), getEnv);   // 这里确实是 -2 ，没有被加到当前线程中
    if (getEnv == JNI_EDETACHED) {
        ALOGE("Current native thread has not attach to jvm");
        if (global_VM->AttachCurrentThread(&globalEnv, NULL) == 0) {
            nativeThreadAttatch = JNI_TRUE;
        } else {
            exit(0);
        }
    }
    jclass ffmpegClass = (globalEnv)->GetObjectClass(globalFFmpegRef);
    jclass listenerClass = globalEnv->GetObjectClass(globalAudioEncodeListener);
    if (ffmpegClass == NULL) {
        ALOGE("No such class");
    }
    jmethodID methodID = (globalEnv)->GetMethodID(ffmpegClass, "getEncodeProcess", "(I)V");
    jmethodID callbackMethod = globalEnv->GetMethodID(listenerClass, "nowProgress", "(D)V");
    jmethodID callbackMethodOver = globalEnv->GetMethodID(listenerClass, "audioEncodeOver", "(Z)V");
    if (methodID == NULL || callbackMethod == NULL || callbackMethodOver == NULL) {
        ALOGE("No such method");
        global_VM->DetachCurrentThread();
    }
    float count = 50.0f;
    for (int i = 0; i < (int) count; ++i) {
        /* make sure the frame is writable -- makes a copy if the encoder
         * kept a reference internally */
        ret = av_frame_make_writable(frame);
        if (ret < 0) {
            ALOGE("av_frame_make_writable");
            exit(1);
        }
        usleep(1000 * 100); // 100ms   , total = 100ms * 200 = 20s , ANR
        uint16_t *samples = (uint16_t *) frame->data[0];
        for (int j = 0; j < avCodecContext->frame_size; ++j) {
            samples[2 * j] = sin(t) * 10000;

            for (int k = 1; k < avCodecContext->channels; ++k) {
                samples[2 * j + k] = samples[2 * j];
            }
            t += tincr;
        }
        encode(avCodecContext, frame, pkt, f);
        // Callback the progress
        (globalEnv)->CallVoidMethod(globalFFmpegRef, methodID, i);
        globalEnv->CallVoidMethod(globalAudioEncodeListener, callbackMethod,
                                  i / count * 100); // 转化为百分比 double
    }
    // callback AudiioEncodeOver to Java App
    globalEnv->CallVoidMethod(globalAudioEncodeListener, callbackMethodOver, true);

    if (nativeThreadAttatch) {
        // 释放全局引用
        globalEnv->DeleteGlobalRef(globalFFmpegRef);
        globalEnv->DeleteGlobalRef(globalAudioEncodeListener);
        globalEnv = NULL;
        global_VM->DetachCurrentThread();

    }
    // flush the encoder
    encode(avCodecContext, NULL, pkt, f);
    ALOGM("######################  END #########################");
    return NULL;
}


void JNICALL
Java_com_hua_nativeFFmpeg_NativeFFmpeg_encodeAudio222(JNIEnv *env, jobject instance,
                                                      jobject outFile,
                                                      jint bitRate, jint sampleRate,
                                                      jint best_ch_layout, jint channels) {

    jclass ffmpegClass = env->GetObjectClass(instance);
    if (ffmpegClass == NULL) {
        ALOGE("No such class");
    }
    jmethodID methodID = env->GetMethodID(ffmpegClass, "getEncodeProcess", "(I)V");
    if (methodID == NULL) {
        ALOGE("No such method");
    }
    env->CallVoidMethod(instance, methodID, 123);


    jclass listener = env->FindClass(
            "com/hua/nativeFFmpeg/NativeFFmpeg$IAudioEncodeProgressListener");
    if (listener == NULL) {
        ALOGE("No such class");
        return;
    }
    jmethodID callbackMethod = env->GetMethodID(listener, "nowProgress", "(I)V");
    if (callbackMethod == NULL) {
        ALOGE("callbackMethod NULL");
    }
    //JNI DETECTED ERROR IN APPLICATION: can't call void com.hua.nativeFFmpeg.NativeFFmpeg$IAudioEncodeProgressListener.nowProgress(int) on instance of com.hua.nativeFFmpeg.NativeFFmpeg
}

void JNICALL
Java_com_hua_nativeFFmpeg_NativeFFmpeg_encodeAudioWithListener(JNIEnv *env, jobject instance,
                                                               jobject outFile,
                                                               jobject listenerobj) {
    jclass ffmpegClass = env->GetObjectClass(instance);
    if (ffmpegClass == NULL) {
        ALOGE("No such class");
    }
    jmethodID methodID = env->GetMethodID(ffmpegClass, "getEncodeProcess", "(I)V");
    if (methodID == NULL) {
        ALOGE("No such method");
    }
    env->CallVoidMethod(instance, methodID, 123);


//    jclass listener = env->FindClass("com/hua/nativeFFmpeg/NativeFFmpeg$IAudioEncodeProgressListener");
    jclass listener = env->GetObjectClass(listenerobj);
    if (listener == NULL) {
        ALOGE("No such class");
        return;
    }
    jmethodID callbackMethod = env->GetMethodID(listener, "nowProgress", "(I)V");
    if (callbackMethod == NULL) {
        ALOGE("No such Method");
        return;
    }
    for (int i = 0; i < 100; i++) {
        env->CallVoidMethod(listenerobj, callbackMethod, i);
        usleep(1000 * 50);
    }
}

void JNICALL
Java_com_hua_nativeFFmpeg_NativeFFmpeg_encodeAudio(JNIEnv *env, jobject instance, jobject outFile,
                                                   jint bitRate, jint sampleRate,
                                                   jint best_ch_layout, jint channels,
                                                   jobject listenerobj) {
    env->GetJavaVM(&global_VM);
    globalFFmpegRef = env->NewGlobalRef(instance);
    globalAudioEncodeListener = env->NewGlobalRef(listenerobj);

    ALOGM("%s", __DATE__);
    const char *filename = getEncodeFilePath(env, outFile);
    const AVCodec *avCodec;
    AVCodecContext *avCodecContext = NULL;
    AVFrame *frame;
    AVPacket *pkt;
    int ret;
    FILE *f;
    uint16_t *samples;
    float t, tincr;

//     Find the MP2/MP3 encoder
    avCodec = avcodec_find_encoder(AV_CODEC_ID_MP2);
    if (!avCodec) {
        ALOGE("%s", "Codec not found");
        exit(1);
    }
    avCodecContext = avcodec_alloc_context3(avCodec);
    if (!avCodecContext) {
        ALOGE("%s", "Could not allocate audio codec context");
        exit(1);
    }
    // 这个有什么用呢， SAMPLE_FMT_S16
    avCodecContext->sample_fmt = AV_SAMPLE_FMT_S16;
    if (!check_sample_fmt(avCodec, avCodecContext->sample_fmt)) {
        ALOGE("Encoder does not support sample format %s",
              av_get_sample_fmt_name(avCodecContext->sample_fmt));
        exit(1);
    }
//     select other audio parameters supported by the encoder

    avCodecContext->sample_rate = sampleRate; // select_sample_rate(avCodec);//sampleRate;
    avCodecContext->bit_rate = bitRate;
    avCodecContext->channel_layout = best_ch_layout;//select_channel_layout(avCodec); //best_ch_layout;
    avCodecContext->channels = channels;//av_get_channel_layout_nb_channels(avCodecContext->channel_layout);

//    Open it
    if (avcodec_open2(avCodecContext, avCodec, NULL) < 0) {
        ALOGE("Could not open codec");
        exit(1);
    }

    int fd = open(filename, O_WRONLY);
    if (fd < 0) {
        ALOGE("Could not open %s", filename);
        exit(1);
    }
    //" wb "  write binary
    f = fdopen(fd, "wb");

    /* packet for holding encoded output */
    pkt = av_packet_alloc();
    if (!pkt) {
        ALOGE("Could not av_packet_alloc");
        exit(1);
    }
    frame = av_frame_alloc();
    if (!frame) {
        ALOGE("Could not av_frame_alloc");
        exit(1);
    }
//    select other audio parameters supported by the encoder
    frame->nb_samples = avCodecContext->frame_size;
    frame->channel_layout = avCodecContext->channel_layout;
    frame->format = avCodecContext->sample_fmt;

    // allocate the data buffers
    ret = av_frame_get_buffer(frame, 0);
    if (ret < 0) {
        ALOGE("Could not allocate audio data buffers %s", av_err2str(ret));
        exit(1);
    }

    struct encodeAudio *encodeAudio1 = new encodeAudio;
    encodeAudio1->avCodecContext = avCodecContext;
    encodeAudio1->frame = frame;
    encodeAudio1->pkt = pkt;
    encodeAudio1->f = f;


    //TODO  创建线程去完成耗时的编码
    createNativeThread(encodeAudio1);
//    createNativeThread2(NULL);
#if 0
    beginEncodeAudio(avCodecContext, frame, pkt, f);

    fclose(f);

    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&avCodecContext);
#endif
}

void createNativeThread(void *encodeAudio) {
    pthread_t encodeAudioThread;
//    pthread_create(&encodeAudioThread, NULL, encodeAudioMethod, encodeAudio);
    pthread_create(&encodeAudioThread, NULL, encodeAudioThreadMthod, encodeAudio);
}


void createNativeThread2(void *argc) {
    pthread_t encodeAudioThread2;
    pthread_create(&encodeAudioThread2, NULL, encodeAudioThreadMthod, NULL);
}



void JNICALL
Java_com_hua_nativeFFmpeg_NativeFFmpeg_encodeVideo(JNIEnv *env, jobject instance, jobject outFile,
                                                   jstring codecName_) {
    const char *codecName = env->GetStringUTFChars(codecName_, 0);
    const char *encodeVideoFileName = getEncodeFilePath(env, outFile);
    const AVCodec *codec;
    AVCodecContext *c;
    AVFrame *frame;
    AVPacket *pkt;

    codec = avcodec_find_encoder(AV_CODEC_ID_MPEG1VIDEO);
    if (!codec) {
        ALOGE("Video Codec Not found");
        exit(1);
    }
    c = avcodec_alloc_context3(codec);
    if (!c) {
        ALOGE("Could not allocate VideoCodecContext");
        exit(1);
    }

    pkt = av_packet_alloc();
    if (!pkt) exit(1);


    /* put sample parameters */
    c->bit_rate = 400000;
    /* resolution must be a multiple of two */
    c->width = 352;
    c->height = 288;
    /* frames per second */
    c->time_base = (AVRational) {1, 25};
    c->framerate = (AVRational) {25, 1};

    /* emit one intra frame every ten frames
     * check frame pict_type before passing frame
     * to encoder, if frame->pict_type is AV_PICTURE_TYPE_I
     * then gop_size is ignored and the output of encoder
     * will always be I frame irrespective to gop_size
     */
    c->gop_size = 10;
    c->max_b_frames = 1;
    c->pix_fmt = AV_PIX_FMT_YUV420P;

    if (codec->id == AV_CODEC_ID_H264)
        av_opt_set(c->priv_data, "preset", "slow", 0);

    /* open it */
    int ret = avcodec_open2(c, codec, NULL);
    if (ret < 0) {
        ALOGE("Could not open codec: %s\n", av_err2str(ret));
        exit(1);
    }

    FILE *f = fopen(encodeVideoFileName, "wb");
    if (!f) {
        ALOGE("Could not open %s\n", encodeVideoFileName);
        exit(1);
    }

    frame = av_frame_alloc();
    if (!frame) {
        ALOGE("Could not allocate video frame\n");
        exit(1);
    }
    frame->format = c->pix_fmt;
    frame->width = c->width;
    frame->height = c->height;

    ret = av_frame_get_buffer(frame, 32);

    if (ret < 0) {
        ALOGE("Could not allocate the video frame data\n");
        exit(1);
    }


    /* encode 10 second of video */
    for (int i = 0; i < 25 * 10; i++) {
        fflush(stdout);

        /* make sure the frame data is writable */
        ret = av_frame_make_writable(frame);
        if (ret < 0)
            exit(1);

        /* prepare a dummy image */
        /* Y */
        for (int y = 0; y < c->height; y++) {
            for (int x = 0; x < c->width; x++) {
                frame->data[0][y * frame->linesize[0] + x] = x + y + i * 3;
            }
        }

        /* Cb and Cr */
        for (int y = 0; y < c->height / 2; y++) {
            for (int x = 0; x < c->width / 2; x++) {
                frame->data[1][y * frame->linesize[1] + x] = 128 + y + i * 2;
                frame->data[2][y * frame->linesize[2] + x] = 64 + x + i * 5;
            }
        }

        frame->pts = i;

        /* encode the image */
        encode(c, frame, pkt, f);
    }
    /* flush the encoder */
    encode(c, NULL, pkt, f);

    /* add sequence end code to have a real MPEG file */
    uint8_t endcode[] = {0, 0, 1, 0xb7};
    fwrite(endcode, 1, sizeof(endcode), f);
    fclose(f);

    avcodec_free_context(&c);
    av_frame_free(&frame);
    av_packet_free(&pkt);

    env->ReleaseStringUTFChars(codecName_, codecName);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_hua_nativeFFmpeg_NativeFFmpeg_encodeAudioWhtiPthread(JNIEnv *env, jobject instance,
                                                              jstring filePath_) {


//    const char *filename2 = env->GetStringUTFChars(filePath_, 0);

    createNativeThread2(NULL);

//    env->ReleaseStringUTFChars(filePath_, filename2);
}