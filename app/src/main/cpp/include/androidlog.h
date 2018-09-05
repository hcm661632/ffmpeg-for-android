//
// Created by FC5981 on 2018/8/29.
//

#ifndef TESTFFMPEGLIBS_ANDROIDLOG_H
#define TESTFFMPEGLIBS_ANDROIDLOG_H

#include <android/log.h>

#ifndef LOG_TAG
#define  LOG_TAG "ThirdLib_JNI"
#endif


#define ALOGI(...) __android_log_print(ANDROID_LOG_INFO,LOG_TAG,__VA_ARGS__)
#define ALOGV(...) __android_log_print(ANDROID_LOG_VERBOSE,LOG_TAG,__VA_ARGS__)
#define ALOGD(...) __android_log_print(ANDROID_LOG_DEBUG,LOG_TAG,__VA_ARGS__)
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR,LOG_TAG,__VA_ARGS__)
#define ALOGM(fmt, ...)  __android_log_print(ANDROID_LOG_DEBUG,LOG_TAG,\
                                             "[%s:%d]" fmt, __FUNCTION__, __LINE__,##__VA_ARGS__);


#endif //TESTFFMPEGLIBS_ANDROIDLOG_H
