#ifndef __NIO_LIVE_SERVER_H__
#define __NIO_LIVE_SERVER_H__

#include <pthread.h>
#include <string.h>
#include <jni.h>

#include "NIOAndroidLog.h"
#include "liveMedia.hh"
#include "BasicUsageEnvironment.hh"
#include "liveMedia.hh"
#include "ffmpeg_media.h"
#include "ffmpeg_rtsp_server.h"


#define JNI_CLASS_NAME "com/nio/mediaserver/MediaServer"
#define PackageName(x) Java_com_nio_mediaserver_MediaServer_##x

#define DESCRIPTION "Session streamed by \"NIOLiveServer\""
#define PORT 8554
#define FALSE 0
#define TRUE 1

#define ENABLE_FFMPEG 1

#define MEDIA_SERVER_ERROR 1
#define MEDIA_SERVER_ESTABLISHED 2

static JavaVM *javaVM;

struct MatroskaDemuxCreationState {
  MatroskaFileServerDemux* demux;
  char watchVariable;
};      
struct OggDemuxCreationState {                                                                                                                                                  
    OggFileServerDemux* demux;                                                                                                                                                    
    char watchVariable;                                                                                                                                                           
};  

typedef struct ServerStruct {
    jobject weakRef;
    jclass javaClass;
    jmethodID callbackID;

    UsageEnvironment* env;
    TaskScheduler* scheduler;
    ServerMediaSession* sms;
    RTSPServer* rtspServer;
    Medium* demux;

    const char *inputFilePath;
    const char *streamName;

    char exitLoop;
    pthread_mutex_t interface_lock;
}ServerStruct;

//static void announceStream(RTSPServer* rtspServer, ServerMediaSession* sms, char const* streamName, char const* inputFileName);
static int setupNativeCallback(ServerStruct *server, JNIEnv *env, jobject thiz);
static int postEventUptoJava(ServerStruct *server, int what, int arg1, int arg2, void* obj);
static void announceStream(ServerStruct *server);
static int checkHandler(long long handler);
static ServerMediaSession *CreateServerMediaSessionByName(UsageEnvironment& env, const char *streamName, const char* name);
static void onMatroskaDemuxCreation(MatroskaFileServerDemux* newDemux, void* /*clientData*/);
static void onOggDemuxCreation(OggFileServerDemux* newDemux, void* /*clientData*/);
static bool JExceptionCheck(JNIEnv *env);

#ifdef __cplusplus
extern "C" {
#endif
long long PackageName(init)(JNIEnv *env, jclass thiz, jobject weak_ref);
void PackageName(uninit)(JNIEnv *env, jobject thiz, long long handler);
jint PackageName(setInputFilePath)(JNIEnv *env, jobject thiz, long long handler, jstring jpath);
jint PackageName(setOutputStreamName)(JNIEnv *env, jobject thiz, long long handler, jstring jname);
jint PackageName(start)(JNIEnv *env, jobject thiz, long long handler);
jint PackageName(stop)(JNIEnv *env, jobject thiz, long long handler);
#ifdef __cplusplus
}
#endif

#endif //__NIO_LIVE_SERVER_H__
