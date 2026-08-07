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
#ifndef cppinterface_h
#define cppinterface_h

#include <string.h>
#include <time.h>
#include <pthread.h>
#include <setjmp.h>

// #include "klb_android_GameStartup_PFInterface.h"
#include "CPFInterface.h"
#include "CAndroidReadFileStream.h"
#include "CSockReadStream.h"
#include "CJNI.h"

class CAndroidRequest : public IPlatformRequest
{
public:
	CAndroidRequest(const char * model,const char * brand, const char * board, const char * version, const char * tz);
	virtual ~CAndroidRequest();

	static CAndroidRequest * getInstance();

	void nativeSignal(int cmd, int param);

	virtual bool init();
	void validateEnvironment();

	//! Use Encryption for disk I/O
	virtual bool useEncryption();

	void detailedLogging(const char * basefile, const char * functionName, int lineNo, const char * format, ...);
	void logging(const char * format, ...);
	s64	nanotime();
    
    // バンドルバージョン取得
    const char* getBundleVersion();
    const char* getBundleId();

    IReadStream * openReadStream(const char * fileName, bool decrypt, u32 mode = 0);
	IReadStream * openWriteStream(const char * fileName, bool encrypt, u32 mode = 0);
	void beforeAssertFunction(const char* functionName, bool request = false);
	void addExtMsg(const char* key, const char* value, bool sendImmediately);
	void sendException(const char* message);
	void leaveBreadcrumb(const char* message);
	char* createRequestIdHeader();
	void requestExtensionEvent(const char* eventName, ExtensionEventArgs* arguments);
	ITmpFile * openTmpFile(const char * tmpPath);
    int removeTmpFile(const char * tmpPath);
	virtual bool removeFileOrFolder	 (const char * filePath);
	virtual u32 getFreeSpaceExternalKB();
	virtual u32 getPhysicalMemKB	 ();
    void excludePathFromBackup(const char * fullpath);
    void*		ifopen	(const char* name, const char* mode);
	void		ifclose	(void* file);
	int			ifseek	(void* file, long int offset, int origin);
	u32			ifread	(void* ptr, u32 size, u32 count, void* file );
	u32			ifwrite	(const void * ptr, u32 size, u32 count, void* file);
	int			ifflush	(void* file);
	long int	iftell	(void* file);
	bool		icreateEmptyFile(const char* name);
	int			irename	(const char* oldName, const char* newName);

	void * loadAudio(const char * url, bool is_se);
    bool   preLoad(void * handle);
    bool   setBufSize(void * handle, int level);
    void   playAudio(void * handle, s32 _milisec=0, float _tgtVol=1.0f, float _startVol=1.0f);
    void   stopAudio(void * handle, s32 _milisec=0, float _tgtVol=0.0f);
    void   setMasterVolume(float volume, bool SEmode);
    void   setAudioVolume(void * handle, float volume);
    void   setAudioPan(void * handle, float pan);
    void   releaseAudio(void * handle);
	
	void pauseAudio(void * handle, s32 _milisec=0, float _tgtVol=0.0f);
    void resumeAudio(void * handle, s32 _milisec=0, float _tgtVol=1.0f);
    void seekAudio(void * handle, s32 millisec);
    s32  tellAudio(void * handle);
    s32  totalTimeAudio(void * handle);
	
    s32 getState(void * handle);
    
    //! サウンドとミュージックの並行処理タイプ設定
    void setAudioMultiProcessType( s32 _processType );
    
    //! サウンドの割り込み処理をエンジン側で制御するかどうか
    void setPauseOnInterruption(bool _bPauseOnInterruption);
	char* getDeviceIntegrityInfo(const char* request);
	void decompressBGM(bool decompress);
    
    //! 経過時間を取得(sec)
    s64 getElapsedTime(void);
    
    void setFadeParam(void* _handle, float _tgtVol, u32 _milisec);

    //! フォントオブジェクト取得
	void * getFont(int size, const char * fontName = 0);
	void * getFont(int size, const char * fontName, float* pAscent);
	void * getFontSystem(int size, const char * fontName = 0);
	void * getFontSystem();

    //! フォントオブジェクト破棄
    void deleteFont(void * pFont);
    void deleteFontSystem(void * pFont);

    bool getTextInfo(const char* utf8String, void * pFont, STextInfo* pReturnInfo);

    void* getGLExtension(const char*);
    const char* getShaderExtension(int shaderType);
	bool isSafeAreaScreen();
	void setSafeAreaScreen(bool isSafeArea);
	void onKLabIdResult(int result, const char* keyValuePairs);
    bool setFrameRate(int frameRate);
    int getMaxFrameRate();
	void getSafeAreaInset(float* insets);

    // 追加分method

    const char * getFullPath(const char * assetPath, bool* isReadOnly);

    const char * getPlatform();

    IWidget * createControl(IWidget::CONTROL type, int id, const char * caption, int x, int y, int width, int height, ...);
    void destroyControl(IWidget * pControl);
    bool callApplication(APP_TYPE type, ...);
    void clearCookies();
    void exitGame();
    void copyToClipboard(const char* text);
    double getFreeMemorySize();
    double getUsedMemorySize();
    bool getSMode();
    void getDateTimeNow(char* buffer, int bufferSize);
    double getUNIXTimeNow();
    void savePng2Album(const char* path);
    void setIdleTimerActivity(bool active);
    void setUserDefaults(const char* key, bool value);
    bool getUserDefaults(const char* key);
    void setUserDefaults(const char* key, const char* value);
    void getUserDefaults(const char* key, char* buffer, int maxLength);

    void * createThread(s32 (*thread_func)(void * hThread, void * data), void * data);
    void exitThread(void * hThread, s32 status);
    bool watchThread(void * hThread, s32 * status);
    void deleteThread(void * hThread);
    void breakThread(void * hThread);

    int genUserID(char * retBuf, int maxlen);
    int genUserPW(const char * salt, char * retBuf, int maxlen);
	void registerScriptSource(const char* source, int sourceSize, const char* sourceName);
	
    bool readyDevID();
    int getDevID(char * retBuf, int maxlen);
	
    bool setSecureDataID(const char * service_name, const char * user_id);
    bool setSecureDataPW(const char * service_name, const char * passwd);
    int getSecureDataID(const char * service_name, char * retBuf, int maxlen);
    int getSecureDataPW(const char * service_name, char * retBuf, int maxlen);

	bool delSecureDataID(const char * service_name);
    bool delSecureDataPW(const char * service_name);
	
    // alert dialog
    void startAlertDialog( const char* title , const char* message );
	
	inline void setHomePath(const char * home) {
		int len = strlen(home);
		m_homePath = new char [len + 1];
		strcpy(m_homePath, home);
	}
	jclass getJavaClass(const char* className, bool global);
	bool callJavaMethod(jclass targetClass, jvalue& ret, const char * method, const char rettype, const char * form, ...);

	inline float getMasterVolume(bool SEmode) const { return (SEmode) ? m_master_SE : m_master_BGM; }


	//! ストア機能
	//! トランザクション監視開始
	void initStoreTransactionObserver();
	//! トランザクション監視終了
	void releaseStoreTransactionObserver();
	//! 購入要求: アイテムID(文字列)の配列と、その数を渡す
    void buyStoreItems(const char * item_id);
	//! プロダクトリスト取得要求: アイテムIDの配列とその数、及びcurrency表示にするかの判定とcallbackを渡す.
    void getStoreProducts(const char* json, bool currency_mode);
	//! サーバー通信後にトランザクションを閉じて購入処理を確定させる。
	void finishStoreTransaction(const char* receipt);
	bool publicKeyVerify(unsigned char* message, int messageLength,
						 unsigned char* signature, int signatureLength);
	int publicKeyEncrypt(unsigned char* input, int inputLength,
					 unsigned char* output, int outputLength);
	bool randomBytes(unsigned char* output, int length);
	int encryptAES128CBC(unsigned char* output, int outputLength,
					 const char* input, int inputLength,
					 const char* key, int keyLength);
	int decryptAES128CBC(unsigned char* output, int outputLength,
						 const char* input, int inputLength,
						 const char* key, int keyLength);
	virtual bool initNetwork();
	virtual void shutdownNetwork();
	virtual CurlObjectInternal* createNetworkOperation();
	virtual void resetNetworkOperation(CurlObjectInternal* operation);
	virtual void cleanupNetworkOperation(CurlObjectInternal* operation);
	virtual int performNetworkOperation(CurlObjectInternal* operation);
	virtual void freeNetworkFormHeaders(CurlObjectInternal* operation);
	virtual void destroyNetworkOperation(CurlObjectInternal* operation);
	virtual void appendNetworkHeader(CurlObjectInternal* operation, const char* header);
	virtual void setNetworkPostFields(CurlObjectInternal* operation);
	virtual void setNetworkPostData(CurlObjectInternal* operation, long contentLength, const void* data);
	virtual void addNetworkFormData(CurlObjectInternal* operation, const char* name,
	                                long contentLength, const void* data);
	virtual void setupNetworkConnection(CurlObjectInternal* operation, const char* url,
	                                    const char* proxy, void* callbackContext,
	                                    void* progressCallback, void* headerCallback,
	                                    void* writeCallback);
	virtual long getNetworkHttpCode(CurlObjectInternal* operation);

	virtual void*	allocMutex		();
	virtual void	freeMutex		(void* mutex);
	virtual void	mutexLock		(void* mutex);
	virtual void	mutexUnlock		(void* mutex);

	virtual void*	allocEventLock	();
	virtual void	freeEventLock	(void* lock);
	virtual void	eventSleep		(void* lock);
	virtual void	eventWakeup		(void* lock);

	void forbidSleep(bool is_forbidden);
	float getDeviceScale();
	void quitGame();
	IMovieInterface* createMoviePlayer(const char* url, int width, int height);
	void destroyMoviePlayer(IMovieInterface* player);
	const char* getLangCodeRAW();
	const char* getCountryCodeRAW();
	const char* getPreferredLangCodeRAW();
	bool getGyroPolar(float* azimuth, float* elevation);
	int getOptimalAudioHz();
	int getOptimalAudioSamples();

private:
	struct PF_THREAD {
		jmp_buf		jmp;
		pthread_t	id;
		s32 (*thread_func)(void *, void *);
		void * data;
		s32 result;
		bool running;
	};
	static void * ThreadProc(void * data);
	int sha512(const char * string, char * buf, int maxlen);

	enum {
		SND_SLOT = 256
	};
	jobject				m_platformContext;
	char 			*	m_homePath;
	const char		*	m_platform;
	float				m_master_BGM;
	float				m_master_SE;

	const char		*	m_regId;
	char                m_languageCode[25];
	char                m_countryCode[25];
	char                m_preferredLanguageCode[25];
	char*               m_deviceIntegrityInfo;
	const char*         m_klabIdCallback;
	bool                m_isSafeAreaScreen;

	static CAndroidRequest * ms_instance;
	static void getElapsedTimeSpec(struct timespec * ts);
	static s64 getElapsedNanoTime(void);
};

#endif // cppinterface_h
