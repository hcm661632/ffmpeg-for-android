//
// Created by FC5981 on 2018/8/29.
//

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/file.h>
#include "include/readfileInfo.h"
#include "include/androidlog.h"
#include <stdlib.h>

struct buffer_data {
    uint8_t *ptr;
    size_t size; ///< size left in the buffer
};


size_t av_strlcpy(char *dst, const char *src, size_t size)
{
    size_t len = 0;
    while (++len < size && *src)
        *dst++ = *src++;
    if (len <= size)
        *dst = 0;
    return len + strlen(src) - 1;
}

char *getShowInfo(AVFormatContext *fmt, const char *filename);

static int read_packet(void *opaque, uint8_t *buf, int buf_size) {
    struct buffer_data *bd = (struct buffer_data *) opaque;
    buf_size = FFMIN(buf_size, bd->size);

    if (!buf_size)
        return AVERROR_EOF;


    /* copy internal buffer data to buf */
    memcpy(buf, bd->ptr, buf_size);
    bd->ptr += buf_size;
    bd->size -= buf_size;

    return buf_size;
}

jstring
Java_com_hua_nativeFFmpeg_NativeFFmpeg_readMediaInfo(JNIEnv *env, jobject instance,
                                                     jstring fileName_) {

    const char *input_filename = env->GetStringUTFChars(fileName_, 0);
// 比如说解析 ID3

    AVFormatContext *fmt_ctx = NULL;
    AVIOContext *avio_ctx = NULL;
    uint8_t *buffer = NULL, *avio_ctx_buffer = NULL;
    char showInfo[1024];
    memset(showInfo, 0, sizeof(showInfo) / sizeof(showInfo[0]));
    char tempFormat[50];
    memset(showInfo, 0, sizeof(tempFormat) / sizeof(tempFormat[0]));
    size_t buffer_size, avio_ctx_buffer_size = 4096;

    struct buffer_data bd = {0};
    int ret = 0;

    ret = av_file_map(input_filename, &buffer, &buffer_size, 0, NULL);
    if (ret < 0) {
        goto end;
    }
    /* fill opaque structure used by the AVIOContext read callback */
    bd.ptr = buffer;        // ptr 地址是 mmap 由linux分配的
    bd.size = buffer_size; // 通过  fstat获得，

    if (!(fmt_ctx = avformat_alloc_context())) {
        ret = AVERROR(ENOMEM);
        goto end;
    }



    avio_ctx_buffer = (uint8_t *) av_malloc(avio_ctx_buffer_size);
    if (!avio_ctx_buffer) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    avio_ctx = avio_alloc_context(avio_ctx_buffer, avio_ctx_buffer_size, 0, &bd, &read_packet, NULL,
                                  NULL);
    if (!avio_ctx) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

//    fmt_ctx->pb = avio_ctx;
    ret = avformat_open_input(&fmt_ctx,input_filename,NULL,NULL);

    if( fmt_ctx->metadata != NULL ) ALOGD("%d",__LINE__);

    if (ret < 0) {
        ALOGE("Could not open input file");
        goto end;
    }

    ret = avformat_find_stream_info(fmt_ctx, NULL);
    if (ret < 0) {
        ALOGE("Could not find stream info");
        goto end;
    }

    // 输出 ，文件名
    strcpy(showInfo, fmt_ctx->filename);
    strcat(showInfo, "\r\n");
    // 输出 ， 比特率
    strcat(showInfo, "Bit_rate: ");
    sprintf(tempFormat, "%ld", fmt_ctx->bit_rate);
    strcat(showInfo, tempFormat);strcat(showInfo, "\r\n");

    // 再补充其它要显示的内容
//     getShowInfo(fmt_ctx,input_filename);
    strcat(showInfo, (const char *) getShowInfo(fmt_ctx, input_filename));
    ALOGD("All Info: = %s",showInfo );
    env->ReleaseStringUTFChars(fileName_, input_filename);

    // 如果乱码，则  虚拟机会崩溃,
//    return env->NewStringUTF(showInfo);
    return env->NewStringUTF("请看Trace信息，logcat -s ThirdLib_JNI");


    end:
    avformat_close_input(&fmt_ctx);
    /* note: the internal buffer could have changed, and be != avio_ctx_buffer */
    if (avio_ctx) {
        av_freep(&avio_ctx->buffer);
        av_freep(&avio_ctx);
    }
    if (ret < 0) {
        av_file_unmap(buffer, buffer_size);
        char *errRet = av_err2str(ret);
        strcat(errRet, "Error Occur...");
        ALOGE("%s\n", errRet);
        return env->NewStringUTF(errRet);
    }

}

char *getShowInfo(AVFormatContext *fmt_ctx, const char *indent) {
    AVDictionaryEntry *tag = NULL;
    char getShowInfo[512];
    char tempFormat[64];
    while ((tag = av_dict_get(fmt_ctx->metadata, "", tag, AV_DICT_IGNORE_SUFFIX))) {
//        ALOGI("%s=%s\n", tag->key, tag->value);
        strcat(getShowInfo,tag->key);
        strcat(getShowInfo,tag->value);
        strcat(getShowInfo,"\r\n");
    }

    strcat(getShowInfo,"Duration: ");
    if (fmt_ctx->duration != AV_NOPTS_VALUE) {
        int hours, mins, secs, us;
        int64_t duration = fmt_ctx->duration + (fmt_ctx->duration <= INT64_MAX - 5000 ? 5000 : 0);
        secs  = duration / AV_TIME_BASE;
        us    = duration % AV_TIME_BASE;
        mins  = secs / 60;
        secs %= 60;
        hours = mins / 60;
        mins %= 60;
        sprintf(tempFormat, "%02d:%02d:%02d.%02d", hours, mins, secs, (100 * us) / AV_TIME_BASE);
        strcat(getShowInfo,tempFormat);
//        ALOGI("%02d:%02d:%02d.%02d", hours, mins, secs, (100 * us) / AV_TIME_BASE);
    } else {
        strcat(getShowInfo,"N/A");
//        ALOGI("N/A");
    }

    return getShowInfo;
}

// Get metadata

JNIEXPORT jstring JNICALL
Java_com_hua_nativeFFmpeg_NativeFFmpeg_readMediaMetadata(JNIEnv *env, jobject instance,
                                                         jstring fileName_) {
    const char *fileName = env->GetStringUTFChars(fileName_, 0);
    AVFormatContext *fmt_ctx = NULL;
    AVDictionaryEntry *tag = NULL;
    int ret;
    if ((ret = avformat_open_input(&fmt_ctx, fileName, NULL, NULL)))
        goto error;

   /* while ((tag = av_dict_get(fmt_ctx->metadata, "", tag, AV_DICT_IGNORE_SUFFIX))) {
        ALOGI("%s=%s\n", tag->key, tag->value);
    }*/

    while ((tag = av_dict_get(fmt_ctx->metadata, "", tag, AV_DICT_IGNORE_SUFFIX)))
        if (strcmp("language", tag->key)) {
            const char *p = tag->value;
            ALOGI("%-10s: %-16s", tag->key,tag->value);
            while (*p) {
                char tmp[256];
                size_t len = strcspn(p, "\x8\xa\xb\xc\xd");
                av_strlcpy(tmp, p, FFMIN(sizeof(tmp), len+1));

                p += len;
                if (*p == 0xd)  ALOGI("DDDDDDDDD");//av_log(ctx, AV_LOG_INFO, " ");
                if (*p == 0xa)  ALOGI("AAAAAAAAAA");//av_log(ctx, AV_LOG_INFO, "\n%s  %-16s: ", indent, "");
                if (*p) p++;
            }
            //av_log(ctx, AV_LOG_INFO, "\n");
        }

    avformat_close_input(&fmt_ctx);
    env->ReleaseStringUTFChars(fileName_, fileName);
    return env->NewStringUTF("Metadata");
    // when error
    error:
    return env->NewStringUTF(av_err2str(ret));
}







