# For more information about using CMake with Android Studio, read the
# documentation: https://d.android.com/studio/projects/add-native-code.html

# Sets the minimum version of CMake required to build the native library.

cmake_minimum_required(VERSION 3.4.1)


set(CPPDIR ${CMAKE_SOURCE_DIR}/src/main/cpp)
# 设置jniLibs目录路径
set(JNI_LIBS_DIR ${CMAKE_SOURCE_DIR}/src/main/jniLibs)
set(FFMPEG_HOME ${CMAKE_SOURCE_DIR}/../ffmpeg-4.0.2)

#include_directories(${FFMPEG_HOME}/include
#                     ${JNI_LIBS_DIR}/include
#)

# Creates and names a library, sets it as either STATIC
# or SHARED, and provides the relative paths to its source code.
# You can define multiple libraries, and CMake builds them for you.
# Gradle automatically packages shared libraries with your APK.

add_library( # Sets the name of the library.
             native-lib2
             # Sets the library as a shared library.
             SHARED
             # Provides a relative path to your source file(s).
             src/main/cpp/native-lib.cpp
             src/main/cpp/readfileInfo.cpp
             src/main/cpp/EncodeAudioDemo.cpp
              )

# Searches for a specified prebuilt library and stores the path as a
# variable. Because CMake includes system libraries in the search path by
# default, you only need to specify the name of the public NDK library
# you want to add. CMake verifies that the library exists before
# completing its build.

find_library( # Sets the name of the path variable.
              log-lib
              # Specifies the name of the NDK library that
              # you want CMake to locate.
              log )



add_library(
             native-audio
             SHARED
             IMPORTED )

# 引用库文件
set_target_properties(
                       native-audio
                       PROPERTIES IMPORTED_LOCATION
                       ${JNI_LIBS_DIR}/armeabi-v7a/libnative-audio.so )



add_library(avcodec SHARED IMPORTED)
set_target_properties(avcodec PROPERTIES IMPORTED_LOCATION  ${JNI_LIBS_DIR}/${ANDROID_ABI}/libavcodec.so)


add_library(avfilter SHARED IMPORTED)
set_target_properties(avfilter PROPERTIES IMPORTED_LOCATION  ${JNI_LIBS_DIR}/${ANDROID_ABI}/libavfilter.so)


add_library(avformat SHARED IMPORTED)
set_target_properties(avformat PROPERTIES IMPORTED_LOCATION  ${JNI_LIBS_DIR}/${ANDROID_ABI}/libavformat.so)


add_library(avutil SHARED IMPORTED)
set_target_properties(avutil PROPERTIES IMPORTED_LOCATION  ${JNI_LIBS_DIR}/${ANDROID_ABI}/libavutil.so)

add_library(swresample SHARED IMPORTED)
set_target_properties(swresample PROPERTIES IMPORTED_LOCATION  ${JNI_LIBS_DIR}/${ANDROID_ABI}/libswresample.so)


add_library(swscale SHARED IMPORTED)
set_target_properties(swscale PROPERTIES IMPORTED_LOCATION  ${JNI_LIBS_DIR}/${ANDROID_ABI}/libswscale.so)

add_library(hhThirdLib SHARED IMPORTED)
set_target_properties(hhThirdLib PROPERTIES IMPORTED_LOCATION  ${JNI_LIBS_DIR}/${ANDROID_ABI}/libhhThridLib.so)

#链接第三方库的头文件
target_include_directories(native-lib2 PRIVATE
                           ${JNI_LIBS_DIR}/include
                         )

target_link_libraries( # Specifies the target library.
                       native-lib2
                       hhThirdLib
                       #native-audio

                       avcodec
                       avfilter
                       avformat
                       avutil
                       swresample
                       swscale

                       # Links the target library to the log library
                       # included in the NDK.
                       ${log-lib} )