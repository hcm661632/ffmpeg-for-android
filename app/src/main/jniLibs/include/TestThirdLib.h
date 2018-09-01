//
// Created by FC5981 on 2018/8/27.
//

#ifndef FFMPEGLIB_TESTTHRIDLIB_H
#define FFMPEGLIB_TESTTHRIDLIB_H

#include <android/log.h>

#define  LOG_TAG "ThirdLib_JNI"
#define ALOGV(...) __android_log_print(ANDROID_LOG_INFO,LOG_TAG,__VA_ARGS__)
#define ALOGD(...) __android_log_print(ANDROID_LOG_DEBUG,LOG_TAG,__VA_ARGS__)
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR,LOG_TAG,__VA_ARGS__)
class TestThirdLib{
public:
    void converToUpper(char* str);
    void converToLower(char* str);
    void setName(char* name);
    void setAge(unsigned int age);
    char* getName();
    unsigned  int getAge();
    TestThirdLib(){

    }

    TestThirdLib(char* name, unsigned int age){
        this->age = age;
        this->name = name;
    }

private:
    char* name;
    unsigned int age;
};


#endif //FFMPEGLIB_TESTTHRIDLIB_H