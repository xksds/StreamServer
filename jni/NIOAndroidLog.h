#ifndef __NIO_ANDROID_LOG_H__
#define __NIO_ANDROID_LOG_H__
#include <android/log.h>
#define LOG_TAG "NIOLiveServer"

#define ALOGD(...)  __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define ALOGI(...)  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define ALOGE(...)  __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#endif //__NIO_ANDROID_LOG_H__
