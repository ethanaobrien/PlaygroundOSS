#ifndef PLAYGROUND_RUNTIME_RUNTIME_PLATFORM_H
#define PLAYGROUND_RUNTIME_RUNTIME_PLATFORM_H

#include "CPFInterface.h"

#include <atomic>
#include <functional>
#include <memory>
#include <string>

namespace playground::runtime {

class RuntimeStateStore;
class DesktopScriptRegistry;
class RuntimeWidgetManager;

class RuntimePlatform final : public IPlatformRequest {
public:
  using GLProcResolver = void *(*)(const char *name);
  using StorageCommit = std::function<bool()>;

  RuntimePlatform(std::string installRoot, std::string contentRoot,
                  GLProcResolver glResolver = nullptr);
  RuntimePlatform(std::string installRoot, std::string contentRoot,
                  std::string stateRoot, GLProcResolver glResolver,
                  StorageCommit commitState = {},
                  StorageCommit commitContent = {});
  ~RuntimePlatform() override;

  bool init() override;
  bool useEncryption() override;
  void validateEnvironment() override;
  void detailedLogging(const char *, const char *, int, const char *,
                       ...) override;
  void logging(const char *, ...) override;
  void *ifopen(const char *, const char *) override;
  void ifclose(void *) override;
  int ifseek(void *, long, int) override;
  u32 ifread(void *, u32, u32, void *) override;
  u32 ifwrite(const void *, u32, u32, void *) override;
  int ifflush(void *) override;
  long iftell(void *) override;
  bool icreateEmptyFile(const char *) override;
  int irename(const char *, const char *) override;
  const char *getBundleVersion() override;
  const char *getBundleId() override;
  s64 nanotime() override;
  IReadStream *openReadStream(const char *, bool, u32) override;
  IReadStream *openWriteStream(const char *, bool, u32) override;
  void beforeAssertFunction(const char *, bool) override;
  void addExtMsg(const char *, const char *, bool) override;
  void sendException(const char *) override;
  void leaveBreadcrumb(const char *) override;
  char *createRequestIdHeader() override;
  void copyToClipboard(const char *) override;
  double getUsedMemorySize() override;
  double getFreeMemorySize() override;
  bool getSMode() override;
  void getDateTimeNow(char *, int) override;
  double getUNIXTimeNow() override;
  void requestExtensionEvent(const char *, ExtensionEventArgs *) override;
  void savePng2Album(const char *) override;
  void setIdleTimerActivity(bool) override;
  ITmpFile *openTmpFile(const char *) override;
  int removeTmpFile(const char *) override;
  bool removeFileOrFolder(const char *) override;
  void excludePathFromBackup(const char *) override;
  u32 getFreeSpaceExternalKB() override;
  u32 getPhysicalMemKB() override;
  char *getDeviceIntegrityInfo(const char *) override;
  void decompressBGM(bool) override;
  s64 getElapsedTime() override;
  void *getFontSystem() override;
  void deleteFontSystem(void *) override;
  void *getFont(int, const char *) override;
  void deleteFont(void *) override;
  const char *getFullPath(const char *, bool *) override;
  const char *getPlatform() override;
  void *getGLExtension(const char *) override;
  const char *getShaderExtension(int) override;
  bool setFrameRate(int) override;
  int getMaxFrameRate() override;
  IWidget *createControl(IWidget::CONTROL, int, const char *, int, int, int,
                         int, ...) override;
  void destroyControl(IWidget *) override;
  bool callApplication(APP_TYPE, ...) override;
  void clearCookies() override;
  bool readyDevID() override;
  int getDevID(char *, int) override;
  void exitGame() override;
  bool setSecureDataID(const char *, const char *) override;
  bool setSecureDataPW(const char *, const char *) override;
  int getSecureDataID(const char *, char *, int) override;
  int getSecureDataPW(const char *, char *, int) override;
  bool delSecureDataID(const char *) override;
  bool delSecureDataPW(const char *) override;
  void setUserDefaults(const char *, bool) override;
  bool getUserDefaults(const char *) override;
  void setUserDefaults(const char *, const char *) override;
  void getUserDefaults(const char *, char *, int) override;
  void *createThread(s32 (*)(void *, void *), void *) override;
  void exitThread(void *, s32) override;
  bool watchThread(void *, s32 *) override;
  void deleteThread(void *) override;
  void breakThread(void *) override;
  int genUserID(char *, int) override;
  int genUserPW(const char *, char *, int) override;
  void registerScriptSource(const char *, int, const char *) override;
  void initStoreTransactionObserver() override;
  void releaseStoreTransactionObserver() override;
  void buyStoreItems(const char *) override;
  void getStoreProducts(const char *, bool) override;
  void finishStoreTransaction(const char *) override;
  bool publicKeyVerify(unsigned char *, int, unsigned char *, int) override;
  int publicKeyEncrypt(unsigned char *, int, unsigned char *, int) override;
  bool randomBytes(unsigned char *, int) override;
  int encryptAES128CBC(unsigned char *, int, const char *, int, const char *,
                       int) override;
  int decryptAES128CBC(unsigned char *, int, const char *, int, const char *,
                       int) override;
  bool initNetwork() override;
  void shutdownNetwork() override;
  CurlObjectInternal *createNetworkOperation() override;
  void resetNetworkOperation(CurlObjectInternal *) override;
  void cleanupNetworkOperation(CurlObjectInternal *) override;
  int performNetworkOperation(CurlObjectInternal *) override;
  void freeNetworkFormHeaders(CurlObjectInternal *) override;
  void destroyNetworkOperation(CurlObjectInternal *) override;
  void appendNetworkHeader(CurlObjectInternal *, const char *) override;
  void setNetworkPostFields(CurlObjectInternal *) override;
  void setNetworkPostData(CurlObjectInternal *, long, const void *) override;
  void addNetworkFormData(CurlObjectInternal *, const char *, long,
                          const void *) override;
  void setupNetworkConnection(CurlObjectInternal *, const char *, const char *,
                              void *, void *, void *, void *) override;
  long getNetworkHttpCode(CurlObjectInternal *) override;
  IMovieInterface *createMoviePlayer(const char *, int, int) override;
  void destroyMoviePlayer(IMovieInterface *) override;
  void startAlertDialog(const char *, const char *) override;
  void forbidSleep(bool) override;
  float getDeviceScale() override;
  void quitGame() override;
  void *allocMutex() override;
  void freeMutex(void *) override;
  void mutexLock(void *) override;
  void mutexUnlock(void *) override;
  void *allocEventLock() override;
  void freeEventLock(void *) override;
  void eventSleep(void *) override;
  void eventWakeup(void *) override;
  const char *getLangCodeRAW() override;
  const char *getCountryCodeRAW() override;
  const char *getPreferredLangCodeRAW() override;
  bool getGyroPolar(float *, float *) override;
  void *getFont(int, const char *, float *) override;
  void *getFontSystem(int, const char *) override;
  bool getTextInfo(const char *, void *, STextInfo *) override;

  bool quitRequested() const { return m_quitRequested.load(); }
  void handleTextInput(const char *);
  bool handleEditingKey(int, bool);
  void pumpPlatformEvents();
  bool openExternalUrl(const char *url);

private:
  std::string resolvePath(const char *, bool *) const;
  std::string m_installRoot;
  std::string m_contentRoot;
  std::string m_stateRoot;
  StorageCommit m_commitState;
  StorageCommit m_commitContent;
  GLProcResolver m_glResolver;
  std::unique_ptr<RuntimeStateStore> m_state;
  std::unique_ptr<DesktopScriptRegistry> m_scriptRegistry;
  std::unique_ptr<RuntimeWidgetManager> m_widgetManager;
  std::string m_deviceId;
  std::atomic<bool> m_quitRequested{false};
  int m_frameRate{60};
};

} // namespace playground::runtime

#endif
