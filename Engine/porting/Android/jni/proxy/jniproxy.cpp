/* 
   Copyright 2013 KLab Inc.

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/
/*
 * jniproxy.cpp
 *
 */

// jniproxy.cpp

#include <stdio.h>
#include <string.h>
#include    <jni.h>
#include    <android/log.h>
#include    <dlfcn.h>
#include "klb_android_GameEngine_PFInterface.h"
#include "../Android/PackageDefine.h"

JavaVM  *javaVm = NULL;

/*
template<typename T>
void auto_load( T& a, void* ptr ,  const char * func_name )
{
 	if(!ptr) {
 		char buf[256];
 		sprintf(buf, "symbol not found in DLL: %s", func_name);
 	   __android_log_write(ANDROID_LOG_DEBUG, "Proxy", (const char *)buf);
 	}
    a= reinterpret_cast<T>( ptr );
}
#define get_proc_0( image, proc_func , app_func )  auto_load( proc_func , dlsym( image , #app_name ) , #app_func )
#define get_proc( proc_func , app_func )   get_proc_0( LoadApplication, proc_func , app_func )
*/
template<typename T>
void auto_load( T& a, void* image ,  const char * func_name )
{
    void* ptr = dlsym( image , func_name );
 	if(!ptr) {
 		char buf[256];
 		sprintf(buf, "symbol not found in DLL: %s", func_name);
#if DEBUG
 	   __android_log_write(ANDROID_LOG_DEBUG, "Proxy", (const char *)buf);
#endif
 	}
    a= reinterpret_cast<T>( ptr );
}


//-----------------------------------------------------------------------------
static jboolean JNICALL (*PROC_FUNC(initSequence))(JNIEnv *, jobject, jint, jint, jstring, jstring, jstring, jstring, jstring, jstring);
static void     JNICALL (*PROC_FUNC(onKLabIdResult))(JNIEnv *, jobject, jint, jstring);
static void     JNICALL (*PROC_FUNC(onShareCallback))(JNIEnv *, jobject, jstring, jboolean, jstring);
static void     JNICALL (*PROC_FUNC(OnLocationCallback))(JNIEnv *, jobject, jint, jint, jdouble, jdouble, jstring);
static void     JNICALL (*PROC_FUNC(OnNotificationCallback))(JNIEnv *, jobject, jint, jint, jstring);
static void     JNICALL (*PROC_FUNC(frameFlip))(JNIEnv *, jobject, jint);
static void     JNICALL (*PROC_FUNC(inputPoint))(JNIEnv *, jobject, jint, jint, jint, jint);
static void     JNICALL (*PROC_FUNC(inputDeviceKey))(JNIEnv *, jobject, jint, jchar);
static void     JNICALL (*PROC_FUNC(rotateScreenOrientation))(JNIEnv *, jobject, jint, jint, jint);
static void     JNICALL (*PROC_FUNC(toNativeSignal))(JNIEnv *, jobject, jint, jint);
static jint     JNICALL (*PROC_FUNC(getGLVersion))(JNIEnv *, jobject);
static void     JNICALL (*PROC_FUNC(resetViewport))(JNIEnv *, jobject);
static void     JNICALL (*PROC_FUNC(onActivityPause))( void );
static void     JNICALL (*PROC_FUNC(onActivityResume))( void );
static void		JNICALL (*PROC_FUNC(clientControlEvent))(JNIEnv *, jobject, jint, jint, jstring, jstring);
static void     JNICALL (*PROC_FUNC(WebViewControlEvent))( JNIEnv *, jobject, jobject, jint, jstring );
static void		JNICALL (*PROC_FUNC(clientResumeGame))( void );
static void		JNICALL (*PROC_FUNC(jniOnLoad))( JavaVM*, void* );
static jbyteArray JNICALL (*PROC_FUNC(internalGetLocalizedMessage))(JNIEnv *, jobject, jstring, jstring);
static void     JNICALL (*PROC_FUNC(onHeadsetActive))( void );
static void     JNICALL (*PROC_FUNC(onAdMobCallback))(JNIEnv *, jobject, jint, jint, jstring);

//-----------------------------------------------------------------------------

static void*   LoadApplication= NULL;

// setLoadAppPath() で Java 側から受け取ったディレクトリから組み立てた
// libGame.so のフルパス。APP_LOAD_NAME で開けなかった場合に使用する。
static char    LoadAppPath[256];

static int InitializeApplication()
{
#if DEBUG
	__android_log_write(ANDROID_LOG_DEBUG, "Proxy", "Initialize application DLL.");
#endif

    // lib 読み込み
    LoadApplication= dlopen( APP_LOAD_NAME , RTLD_LAZY );
    if( !LoadApplication ){
        // インストール先から探し直す
        LoadApplication= dlopen( LoadAppPath , RTLD_LAZY );
        if( !LoadApplication ){
        	return -1;
        }
    }

    // API 取り出し
    auto_load( PROC_FUNC(getGLVersion)            , LoadApplication , GET_XSTR( APP_FUNC(getGLVersion) ) );
    auto_load( PROC_FUNC(initSequence)            , LoadApplication , GET_XSTR( APP_FUNC(initSequence) ) );
    auto_load( PROC_FUNC(frameFlip)               , LoadApplication , GET_XSTR( APP_FUNC(frameFlip) ) );
    auto_load( PROC_FUNC(inputPoint)              , LoadApplication , GET_XSTR( APP_FUNC(inputPoint) ) );
    auto_load( PROC_FUNC(inputDeviceKey)          , LoadApplication , GET_XSTR( APP_FUNC(inputDeviceKey) ) );
    auto_load( PROC_FUNC(rotateScreenOrientation) , LoadApplication , GET_XSTR( APP_FUNC(rotateScreenOrientation) ) );
    auto_load( PROC_FUNC(toNativeSignal)          , LoadApplication , GET_XSTR( APP_FUNC(toNativeSignal) ) );
    auto_load( PROC_FUNC(resetViewport)           , LoadApplication , GET_XSTR( APP_FUNC(resetViewport) ) );
    auto_load( PROC_FUNC(onActivityPause)         , LoadApplication , GET_XSTR( APP_FUNC(onActivityPause) ) );
    auto_load( PROC_FUNC(onActivityResume)        , LoadApplication , GET_XSTR( APP_FUNC(onActivityResume) ) );
	auto_load( PROC_FUNC(clientControlEvent)      , LoadApplication , GET_XSTR( APP_FUNC(clientControlEvent) ) );
    auto_load( PROC_FUNC(WebViewControlEvent)     , LoadApplication , GET_XSTR( APP_FUNC(WebViewControlEvent) ) );
    auto_load( PROC_FUNC(clientResumeGame)         , LoadApplication , GET_XSTR( APP_FUNC(clientResumeGame) ) );
    auto_load( PROC_FUNC(jniOnLoad)               , LoadApplication , GET_XSTR( APP_FUNC(jniOnLoad) ) );
    auto_load( PROC_FUNC(onKLabIdResult)          , LoadApplication , GET_XSTR( APP_FUNC(onKLabIdResult) ) );
    auto_load( PROC_FUNC(onShareCallback)         , LoadApplication , GET_XSTR( APP_FUNC(onShareCallback) ) );
    auto_load( PROC_FUNC(internalGetLocalizedMessage) , LoadApplication , GET_XSTR( APP_FUNC(internalGetLocalizedMessage) ) );
    auto_load( PROC_FUNC(OnNotificationCallback)  , LoadApplication , GET_XSTR( APP_FUNC(OnNotificationCallback) ) );
    auto_load( PROC_FUNC(OnLocationCallback)      , LoadApplication , GET_XSTR( APP_FUNC(OnLocationCallback) ) );
    auto_load( PROC_FUNC(onHeadsetActive)         , LoadApplication , GET_XSTR( APP_FUNC(onHeadsetActive) ) );
    auto_load( PROC_FUNC(onAdMobCallback)         , LoadApplication , GET_XSTR( APP_FUNC(onAdMobCallback) ) );

	if (javaVm != NULL) {
		PROC_FUNC(jniOnLoad)(javaVm, NULL);
	}
    return 0;
}


extern "C" {
//-----------------------------------------------------------------------------

JNIEXPORT jboolean JNICALL JAVA_FUNC(initSequence)
  (JNIEnv *env, jobject obj, jint j_width, jint j_height, jstring j_strPath,
		  jstring j_model, jstring j_brand, jstring j_board, jstring j_version, jstring j_tz)
{
    if( !LoadApplication ){
        if(InitializeApplication()) return 0;
    }
    return PROC_FUNC(initSequence)( env, obj, j_width, j_height, j_strPath, j_model, j_brand, j_board, j_version, j_tz );
    return 0;
}

/*
 * Class:     klb_android_GameEngine_PFInterface
 * Method:    onKLabIdResult
 * Signature: (ILjava/lang/String;)V
 */
JNIEXPORT void JNICALL JAVA_FUNC(onKLabIdResult)
  (JNIEnv *env, jobject obj, jint j_result, jstring j_keyValuePairs)
{
  if(LoadApplication) PROC_FUNC(onKLabIdResult)( env, obj, j_result, j_keyValuePairs );
}

/*
 * Class:     klb_android_GameEngine_PFInterface
 * Method:    onShareCallback
 * Signature: (Ljava/lang/String;ZLjava/lang/String;)V
 */
JNIEXPORT void JNICALL JAVA_FUNC(onShareCallback)
  (JNIEnv *env, jobject obj, jstring j_callback, jboolean j_success, jstring j_result)
{
  if(LoadApplication) PROC_FUNC(onShareCallback)( env, obj, j_callback, j_success, j_result );
}

// Java から渡された保存先ディレクトリから libGame.so の場所を作っておく
JNIEXPORT void JNICALL JAVA_FUNC(setLoadAppPath)
  (JNIEnv *env, jobject obj, jstring j_path)
{
	const char * path = env->GetStringUTFChars( j_path, NULL );

	// 末尾の "files"(5文字) を "lib/libGame.so"(14文字) に差し替える
	int len = strlen(path);
	if( (size_t)len + 9 >= sizeof(LoadAppPath) ) return;
	strncpy( LoadAppPath, path, len - 5 );
	strcat( LoadAppPath, "lib/libGame.so" );

	env->ReleaseStringUTFChars( j_path, path );
}

/*
 * Class:     klb_android_GameEngine_PFInterface
 * Method:    OnLocationCallback
 * Signature: (IIDDLjava/lang/String;)V
 */
JNIEXPORT void JNICALL JAVA_FUNC(OnLocationCallback)
  (JNIEnv *env, jobject obj, jint j_callbackIndex, jint j_parameter, jdouble j_latitude, jdouble j_longitude, jstring j_message)
{
  if(LoadApplication) PROC_FUNC(OnLocationCallback)( env, obj, j_callbackIndex, j_parameter, j_latitude, j_longitude, j_message );
}

/*
 * Class:     klb_android_GameEngine_PFInterface
 * Method:    OnNotificationCallback
 * Signature: (IILjava/lang/String;)V
 */
JNIEXPORT void JNICALL JAVA_FUNC(OnNotificationCallback)
  (JNIEnv *env, jobject obj, jint j_callbackIndex, jint j_parameter, jstring j_message)
{
  if(LoadApplication) PROC_FUNC(OnNotificationCallback)( env, obj, j_callbackIndex, j_parameter, j_message );
}

/*
 * Class:     klb_android_GameEngine_PFInterface
 * Method:    frameFlip
 * Signature: (I)V
 */
JNIEXPORT void JNICALL JAVA_FUNC(frameFlip)
  (JNIEnv *env, jobject obj, jint j_deltaT)
{
  if(LoadApplication) PROC_FUNC(frameFlip)( env, obj, j_deltaT );
}

JNIEXPORT void JNICALL JAVA_FUNC(inputPoint)
  (JNIEnv *env, jobject obj, jint j_id, jint j_type, jint j_x, jint j_y)
{
  if(LoadApplication) PROC_FUNC(inputPoint)( env, obj, j_id, j_type, j_x, j_y );
}

JNIEXPORT void JNICALL JAVA_FUNC(inputDeviceKey)
  (JNIEnv *env, jobject obj, jint keyId, jchar eventType)
{
  if(LoadApplication) PROC_FUNC(inputDeviceKey)( env, obj, keyId, eventType );
}

JNIEXPORT void JNICALL JAVA_FUNC(rotateScreenOrientation)
  (JNIEnv *env, jobject obj, jint j_origin, jint j_width, jint j_height)
{
  if(LoadApplication) PROC_FUNC(rotateScreenOrientation)( env, obj, j_origin, j_width, j_height );
}

JNIEXPORT void JNICALL JAVA_FUNC(toNativeSignal)
  (JNIEnv *env, jobject obj, jint j_cmd, jint j_param)
{
  if(LoadApplication) PROC_FUNC(toNativeSignal)( env, obj, j_cmd, j_param );
}

JNIEXPORT jint JNICALL JAVA_FUNC(getGLVersion)
  (JNIEnv * env, jobject obj)
{
	jint ret = 0;
	if(!LoadApplication) {
        if(InitializeApplication()) return -1;
	}
	if(PROC_FUNC(getGLVersion)) ret = PROC_FUNC(getGLVersion)( env, obj );
	return ret;
}

JNIEXPORT void JNICALL JAVA_FUNC(resetViewport)
  (JNIEnv * env, jobject obj)
{
  if(LoadApplication) PROC_FUNC(resetViewport)( env, obj);
}

// アプリにおけるバックグラウンドに行った際の動作
JNIEXPORT void  JNICALL JAVA_FUNC(onActivityPause) (void)
{
	if(LoadApplication) PROC_FUNC(onActivityPause)();
}

// アプリに置けるフォアグラウンドに行った際の動作
JNIEXPORT void  JNICALL JAVA_FUNC(onActivityResume) (void)
{
	if(LoadApplication) PROC_FUNC(onActivityResume)();
}

JNIEXPORT void JNICALL JAVA_FUNC(clientControlEvent)
	(JNIEnv * env, jobject obj, jint j_type, jint j_widget, jstring j_data_1, jstring j_data_2)
{
	// j_widgetはちょっとどう扱っていいかわからないので、null渡しておきます //
	if(LoadApplication) PROC_FUNC(clientControlEvent)( env, obj, j_type, 0, j_data_1, j_data_2 );
}

// WebViewのコントロールイベント
JNIEXPORT void JNICALL JAVA_FUNC(WebViewControlEvent) ( JNIEnv * env, jobject obj, jobject _pWeb, jint _int, jstring _data )
{
	if(LoadApplication) PROC_FUNC(WebViewControlEvent)( env, obj, _pWeb, _int, _data );
}

JNIEXPORT void  JNICALL JAVA_FUNC(clientResumeGame) (void)
{
	if(LoadApplication) PROC_FUNC(clientResumeGame)();
}

jint JNI_OnLoad(JavaVM* vm, void* reserved)
{
	javaVm = vm;
	if(LoadApplication) {
		PROC_FUNC(jniOnLoad)(vm, reserved);
	}
	return JNI_VERSION_1_6;
}

/*
 * Class:     klb_android_GameEngine_PFInterface
 * Method:    internalGetLocalizedMessage
 * Signature: (Ljava/lang/String;Ljava/lang/String;)[B
 */
JNIEXPORT jbyteArray JNICALL JAVA_FUNC(internalGetLocalizedMessage)
  (JNIEnv *env, jobject obj, jstring j_key, jstring j_fallback)
{
  if(!LoadApplication) return NULL;
  return PROC_FUNC(internalGetLocalizedMessage)( env, obj, j_key, j_fallback );
}

// イヤホンの着脱通知
JNIEXPORT void JNICALL JAVA_FUNC(onHeadsetActive) (void)
{
	if(LoadApplication) PROC_FUNC(onHeadsetActive)();
}

// AdMob のコールバックは extension 側のクラスから呼ばれる
JNIEXPORT void JNICALL JAVA_EXT_FUNC(Firebase, onAdMobCallback)
  (JNIEnv *env, jobject obj, jint j_command, jint j_parameter, jstring j_message)
{
  if(LoadApplication) PROC_FUNC(onAdMobCallback)( env, obj, j_command, j_parameter, j_message );
}
//-----------------------------------------------------------------------------
};
