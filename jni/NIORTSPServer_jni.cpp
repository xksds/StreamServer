#include "NIORTSPServer_jni.h" 

JNIEXPORT jlong PackageName(init)(JNIEnv *env, jclass thiz, jobject weak_ref)
{
    ALOGD("Init ... Build Date %s %s", __DATE__, __TIME__);

    ServerStruct  *server = new ServerStruct();
    if (NULL == server)
    {
        ALOGE("Cannot alloc ServerStruct !!!");
        return -1;
    }

    memset(server, 0, sizeof(ServerStruct));
    server->weakRef = env->NewGlobalRef(weak_ref);

    env->GetJavaVM(&javaVM);  
    setupNativeCallback(server, env, thiz);

    pthread_mutex_init(&server->interface_lock, NULL);

    server->scheduler = BasicTaskScheduler::createNew();
    if(NULL == server->scheduler)
        goto ERR;
    server->env = BasicUsageEnvironment::createNew(*(server->scheduler));
    if(NULL == server->env)
        goto ERR;

#if ENABLE_FFMPEG
    server->rtspServer = FfmpegRTSPServer::createNew(*(server->env), PORT, NULL);
    if (NULL == server->rtspServer) {
        ALOGE("Failed to create FFMPEG RTSP server:%s", server->env->getResultMsg());
        server->rtspServer = FfmpegRTSPServer::createNew(*(server->env), PORT + 1, NULL);
        if (NULL == server->rtspServer) {
            ALOGE("Failed to create FFMPEG RTSP server:%s", server->env->getResultMsg());
            goto ERR;
        }
    } else {
        ALOGI("%p: create FFMPEG RTSP server successful in 8554!", server);
    }
#else
    server->rtspServer = RTSPServer::createNew(*(server->env), PORT, NULL);
    if (NULL == server->rtspServer) {
        ALOGE("Failed to create RTSP server:%s", server->env->getResultMsg());
        server->rtspServer = RTSPServer::createNew(*(server->env), PORT + 1, NULL);
        if (NULL == server->rtspServer) {
            ALOGE("Failed to create RTSP server:%s", server->env->getResultMsg());
            goto ERR;
        }
    } else {
        ALOGI("%p: create RTSP server successful in 8554!", server);
    }
#endif // ENABLE_FFMPEG

    return (int64_t) server;

ERR:
    if(NULL != server) {
        pthread_mutex_destroy(&server->interface_lock);
        delete server;
        server = NULL;
    }

    return -1;
}

void PackageName(uninit)(JNIEnv *env, jobject thiz, long long handler)
{
    if (true != checkHandler(handler))
        return;

    ServerStruct  *server = (ServerStruct*)handler;

    ALOGD("%p: Uninit server ...", server);

    pthread_mutex_lock(&server->interface_lock);
    server->exitLoop = ~0;

    if(NULL != server->rtspServer)
    {
        server->rtspServer->closeAllClientSessionsForServerMediaSession(server->sms);
        server->rtspServer->removeServerMediaSession(server->sms);

        Medium::close(server->rtspServer);
        server->rtspServer = NULL;
        server->sms = NULL;
    }

    if(NULL != server->env)
    {
        server->env->reclaim();
        server->env = NULL;
    }

    if(NULL != server->scheduler)
    {
        //delete server->scheduler;
        server->scheduler = NULL;
    }

    pthread_mutex_unlock(&server->interface_lock);
    pthread_mutex_destroy(&server->interface_lock);
    delete server;
    server = NULL;

    return;
}

jint PackageName(setInputFilePath)(JNIEnv *env, jobject thiz, long long handler, jstring jpath)
{
    int nRC = -1, len = 0;
    jboolean isCopy;
    const char *path = NULL;

    if(TRUE != checkHandler(handler))
        return nRC;

    ServerStruct  *server = (ServerStruct*)handler;

    ALOGD("%p: setInputFilePath path %p ...", server, jpath);

    pthread_mutex_lock(&server->interface_lock);

    if(NULL == jpath) {
        ALOGE("Input file path is NULL !!!");
        goto END;
    }

    server->inputFilePath = env->GetStringUTFChars(jpath, &isCopy);
    if(NULL == server->inputFilePath)
    {
        ALOGE("Cannot get input file path !!!");
        goto END;
    }

END:
    if(NULL != jpath && NULL != path)
        env->ReleaseStringUTFChars(jpath, path);

    pthread_mutex_unlock(&server->interface_lock);

    return 0;   
}

jint PackageName(setOutputStreamName)(JNIEnv *env, jobject thiz, long long handler, jstring jname)
{
    int nRC = -1, len = 0;
    jboolean isCopy;
    const char *name = NULL;

    if(TRUE != checkHandler(handler))
        return nRC;

    ServerStruct  *server = (ServerStruct*)handler;

    ALOGD("%p: setOutputStreamName name %p ...", server, jname);

    pthread_mutex_lock(&server->interface_lock);

    if(NULL == jname) {
        ALOGE("Output stream name NULL !!!");
        goto END;
    }

    server->streamName = env->GetStringUTFChars(jname, &isCopy);
    if(NULL == server->streamName)
    {
        ALOGE("Cannot get output stream name !!!");
        goto END;
    }

    nRC = 0;
END:
    if(NULL != jname && NULL != name)
        env->ReleaseStringUTFChars(jname, name);

    pthread_mutex_unlock(&server->interface_lock);

    return nRC;
}

jint PackageName(start)(JNIEnv *env, jobject thiz, long long handler)
{
    int nRC = -1;
    ServerMediaSession* sms = NULL;

    if(TRUE != checkHandler(handler))
        return nRC;

    ServerStruct  *server = (ServerStruct*)handler;

    ALOGD("%p: start server ...", server);

    pthread_mutex_lock(&server->interface_lock);

    if(NULL == server->rtspServer || NULL == server->inputFilePath || NULL == server->streamName) {
        ALOGE("Cannot get correct informations, rtsp server %p, input file path %p, stream name %p",
                server->rtspServer, server->inputFilePath, server->streamName);
        goto END;
    }

    ALOGD("%p Stream name : %s ", server, server->streamName);
    ALOGD("%p Input file paht : %s", server, server->inputFilePath);

    server->sms = CreateServerMediaSessionByName(*server->env, server->streamName, server->inputFilePath);
    if(NULL == server->sms) {
        ALOGE("Create MediaSession failed !!!");
        ALOGE("Stream name : %s ", server->streamName);
        ALOGE("Input file paht : %s", server->inputFilePath);
        goto END;
    }

    server->rtspServer->addServerMediaSession(server->sms);

    announceStream(server);

    if (server->rtspServer->setUpTunnelingOverHTTP(80)
            || server->rtspServer->setUpTunnelingOverHTTP(8000)
            || server->rtspServer->setUpTunnelingOverHTTP(8080)) {
        ALOGI("we use port-->%d", server->rtspServer->httpServerPortNum());
    } else {
        ALOGI("RTSP-over-HTTP tunneling is not available.");
    }
    pthread_mutex_unlock(&server->interface_lock);

    server->env->taskScheduler().doEventLoop(&server->exitLoop);

    return nRC;

END:
    pthread_mutex_unlock(&server->interface_lock);
    return nRC;
}

jint PackageName(stop)(JNIEnv *env, jobject thiz, long long handler)
{
    int nRC = -1;

    if(TRUE != checkHandler(handler))
        return nRC;

    ServerStruct  *server = (ServerStruct*)handler;

    ALOGD("%p: stop server ...", server);

    pthread_mutex_lock(&server->interface_lock);

    server->exitLoop = ~0;

END:
    pthread_mutex_unlock(&server->interface_lock);
    return 0;
}

/////////////// Private ///////////////////
static int setupNativeCallback(ServerStruct *server, JNIEnv *env, jobject thiz)
{
    if(NULL == server)
    {
        ALOGE("setupNativeCallback Error with NULL == server !!!");
        return -1;
    }

    jclass clazz = env->FindClass(JNI_CLASS_NAME);
    if(NULL == clazz)
    {
        ALOGE("Cannot find class %s", JNI_CLASS_NAME);
        return -1;
    }
    server->javaClass = (jclass)env->NewGlobalRef((jobject)clazz);
    if(true == JExceptionCheck(env) || NULL == server->javaClass)
    {
        ALOGE("NewGlobalRef failed, javaClass %p", server->javaClass);
        return -1;
    }

    server->callbackID = env->GetStaticMethodID(server->javaClass, "nativeCallback", "(Ljava/lang/Object;IIILjava/lang/Object;)I");
    if(true == JExceptionCheck(env) || NULL == server->callbackID)
    {
        ALOGE("Cannot find nativeCallback ID");
        return -1;
    }

    return 0;
}

static int postEventUptoJava(ServerStruct *server, int what, int arg1, int arg2, void* obj)
{
    if(NULL == server || NULL == server->weakRef || NULL == server->javaClass || NULL == server->callbackID)
    {
        if(NULL != server)
            ALOGE("postEventUptoJava failed server %p weakRef %p class %p callback %p !!!", server, server->weakRef, server->javaClass, server->callbackID);
        else
            ALOGE("postEventUptoJava failed server %p !!!", server);
        return -1;
    }

    JNIEnv *env = NULL;
    if (JNI_OK != javaVM->AttachCurrentThread(&env, NULL))
    {
        ALOGE("postEventUptoJava AttachCurrentThread failed !!!");
        return -1;
    }
    
    jobject jobj = NULL;

    switch(what)
    {
        case MEDIA_SERVER_ESTABLISHED:
            jobj = env->NewStringUTF((const char*)obj);
            break;
        default:
            ALOGE("postEventUptoJava with unknown type: %d", what);
    }

    int result = (int)env->CallStaticIntMethod(server->javaClass, server->callbackID, server->weakRef, what, arg1, arg2, jobj);
    if(0 > result)
    {
        ALOGE("postEventUptoJava failed with result %d", result);
        return result;
    }
    
    return 0;
}

static int checkHandler(int64_t handler)
{
    if (0 >= handler)
    {
        ALOGE("Wrong input handler !!!");
        return FALSE;
    }

    return TRUE;
}

static ServerMediaSession *CreateServerMediaSessionByName(UsageEnvironment& env, const char *streamName, const char* name)
{
    char const* extension = strrchr(name, '.');
    if (NULL == extension) return NULL;

    ServerMediaSession* sms = NULL;
    Boolean const reuseSource = False;

    sms = ServerMediaSession::createNew(env, streamName, streamName, DESCRIPTION);
    if(NULL == sms) return NULL;

    if (strcmp(extension, ".aac") == 0) {
        // Assumed to be an AAC Audio (ADTS format) file:
        sms->addSubsession(ADTSAudioFileServerMediaSubsession::createNew(env, name, reuseSource));
    } else if (strcmp(extension, ".amr") == 0) {
        // Assumed to be an AMR Audio file:
        sms->addSubsession(AMRAudioFileServerMediaSubsession::createNew(env, name, reuseSource));
    } else if (strcmp(extension, ".ac3") == 0) {
        // Assumed to be an AC-3 Audio file:
        sms->addSubsession(AC3AudioFileServerMediaSubsession::createNew(env, name, reuseSource));
    } else if (strcmp(extension, ".m4e") == 0) {
        // Assumed to be a MPEG-4 Video Elementary Stream file:
        sms->addSubsession(MPEG4VideoFileServerMediaSubsession::createNew(env, name, reuseSource));
    } else if (strcmp(extension, ".264") == 0) {
        // Assumed to be a H.264 Video Elementary Stream file:
        OutPacketBuffer::maxSize = 100000; // allow for some possibly large H.264 frames
        sms->addSubsession(H264VideoFileServerMediaSubsession::createNew(env, name, reuseSource));
    } else if (strcmp(extension, ".265") == 0) {
        // Assumed to be a H.265 Video Elementary Stream file:
        OutPacketBuffer::maxSize = 100000; // allow for some possibly large H.265 frames
        sms->addSubsession(H265VideoFileServerMediaSubsession::createNew(env, name, reuseSource));
    } else if (strcmp(extension, ".mp3") == 0) {
        // Assumed to be a MPEG-1 or 2 Audio file:
        // To stream using 'ADUs' rather than raw MP3 frames, uncomment the following:
        //#define STREAM_USING_ADUS 1
        // To also reorder ADUs before streaming, uncomment the following:
        //#define INTERLEAVE_ADUS 1
        // (For more information about ADUs and interleaving,
        //  see <http://www.live555.com/rtp-mp3/>)
        Boolean useADUs = False;
        Interleaving* interleaving = NULL;
#ifdef STREAM_USING_ADUS
        useADUs = True;
#ifdef INTERLEAVE_ADUS
        unsigned char interleaveCycle[] = {0,2,1,3}; // or choose your own...
        unsigned const interleaveCycleSize
            = (sizeof interleaveCycle)/(sizeof (unsigned char));
        interleaving = new Interleaving(interleaveCycleSize, interleaveCycle);
#endif
#endif
        sms->addSubsession(MP3AudioFileServerMediaSubsession::createNew(env, name, reuseSource, useADUs, interleaving));
    } else if (strcmp(extension, ".mpg") == 0) {
        // Assumed to be a MPEG-1 or 2 Program Stream (audio+video) file:
        MPEG1or2FileServerDemux* demux
            = MPEG1or2FileServerDemux::createNew(env, name, reuseSource);
        sms->addSubsession(demux->newVideoServerMediaSubsession());
        sms->addSubsession(demux->newAudioServerMediaSubsession());
    } else if (strcmp(extension, ".vob") == 0) {
        // Assumed to be a VOB (MPEG-2 Program Stream, with AC-3 audio) file:
        MPEG1or2FileServerDemux* demux
            = MPEG1or2FileServerDemux::createNew(env, name, reuseSource);
        sms->addSubsession(demux->newVideoServerMediaSubsession());
        sms->addSubsession(demux->newAC3AudioServerMediaSubsession());
    } else if (strcmp(extension, ".ts") == 0) {
        // Assumed to be a MPEG Transport Stream file:
        // Use an index file name that's the same as the TS file name, except with ".tsx":
        unsigned indexFileNameLen = strlen(name) + 2; // allow for trailing "x\0"
        char* indexFileName = new char[indexFileNameLen];
        sprintf(indexFileName, "%sx", name);
        sms->addSubsession(MPEG2TransportFileServerMediaSubsession::createNew(env, name, indexFileName, reuseSource));
        delete[] indexFileName;
    } else if (strcmp(extension, ".wav") == 0) {
        // Assumed to be a WAV Audio file:
        // To convert 16-bit PCM data to 8-bit u-law, prior to streaming,
        // change the following to True:
        Boolean convertToULaw = False;
        sms->addSubsession(WAVAudioFileServerMediaSubsession::createNew(env, name, reuseSource, convertToULaw));
    } else if (strcmp(extension, ".dv") == 0) {
        // Assumed to be a DV Video file
        // First, make sure that the RTPSinks' buffers will be large enough to handle the huge size of DV frames (as big as 288000).
        OutPacketBuffer::maxSize = 300000;

        sms->addSubsession(DVVideoFileServerMediaSubsession::createNew(env, name, reuseSource));
    } else if (strcmp(extension, ".mkv") == 0 || strcmp(extension, ".webm") == 0) {
        // Assumed to be a Matroska file (note that WebM ('.webm') files are also Matroska files)
        OutPacketBuffer::maxSize = 100000; // allow for some possibly large VP8 or VP9 frames

        // Create a Matroska file server demultiplexor for the specified file.
        // (We enter the event loop to wait for this to complete.)
        MatroskaDemuxCreationState creationState;
        creationState.watchVariable = 0;
        MatroskaFileServerDemux::createNew(env, name, onMatroskaDemuxCreation, &creationState);
        env.taskScheduler().doEventLoop(&creationState.watchVariable);

        ServerMediaSubsession* smss;
        while ((smss = creationState.demux->newServerMediaSubsession()) != NULL) {
            sms->addSubsession(smss);
        }
    } else if (strcmp(extension, ".ogg") == 0 || strcmp(extension, ".ogv") == 0 || strcmp(extension, ".opus") == 0) {
        // Assumed to be an Ogg file

        // Create a Ogg file server demultiplexor for the specified file.
        // (We enter the event loop to wait for this to complete.)
        OggDemuxCreationState creationState;
        creationState.watchVariable = 0;
        OggFileServerDemux::createNew(env, name, onOggDemuxCreation, &creationState);
        env.taskScheduler().doEventLoop(&creationState.watchVariable);

        ServerMediaSubsession* smss;
        while ((smss = creationState.demux->newServerMediaSubsession()) != NULL) {
            sms->addSubsession(smss);
        }
    } else if (strcmp(extension, ".avi") == 0 || strcmp(extension, ".mp4") == 0) {
        FfmpegServerDemux* demux = FfmpegServerDemux::CreateNew(env, name, reuseSource);
        if (demux != NULL) {
            sms->addSubsession(demux->NewAudioServerMediaSubsession());
            sms->addSubsession(demux->NewVideoServerMediaSubsession());
        }
    }

    return sms;
}

//static void announceStream(RTSPServer* rtspServer, ServerMediaSession* sms, char const* streamName, char const* inputFileName)
static void announceStream(ServerStruct *server)
{
    char* url = server->rtspServer->rtspURL(server->sms);
    UsageEnvironment& env = server->rtspServer->envir();

    ALOGI("Play this stream using the URL : %s ", url);

    postEventUptoJava(server, MEDIA_SERVER_ESTABLISHED, 0, 0, (void*)url);

    delete[] url;
}

static void onMatroskaDemuxCreation(MatroskaFileServerDemux* newDemux, void* /*clientData*/)
{
    //matroskaDemux = newDemux;
    //newDemuxWatchVariable = 1;
}

static void onOggDemuxCreation(OggFileServerDemux* newDemux, void* /*clientData*/)
{
    //oggDemux = newDemux;
    //newDemuxWatchVariable = 1;
}

static bool JExceptionCheck(JNIEnv *env)
{
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        return true;
    }

    return false;
}
