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
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <sstream>
#include <string>
#include <vector>
#include <zlib.h>

#include "BaseType.h"
#include "CAndroidRequest.h"
#include "CAndroidPathConv.h"
#include "CJNI.h"
#include "CPFInterface.h"
#include "KLBPlatformMetrics.h"
#include "msgpack/pack.h"
#include "msgpack/sbuffer.h"

KLBPlatformMetrics::KLBPlatformMetrics() :
last_rss(-1),
log_buf_len(1024),
log_suffix(0),
logging_enabled(true),
signals_patched(false),
device_ident(NULL),
device_ident_len(0),
log_fp(NULL)
{
	report_mutex  = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
	logging_mutex = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
	thlog_mutex   = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
	page_size = sysconf(_SC_PAGESIZE);
	log_fullpath = CKLBPathConv::getInstance().fullpath("external/__pfmtrx");
	curl_obj = curl_easy_init();
	log_buf = (char*)calloc(log_buf_len, 1);
	getOrGenerateDeviceIdent(log_fullpath);

	char msg[1024];
	snprintf(msg, sizeof(msg), "PfMtrx; session_id=TBD, engine_hash=%s", "6c4c5a51e");
	appendLog(msg, 0, MSGTYPE_INIT);
}

KLBPlatformMetrics::~KLBPlatformMetrics() {
	if (reportingThread) {
		CPFInterface::getInstance().platform().deleteThread(reportingThread);
		reportingThread = NULL;
	}
	if (postingThread) {
		CPFInterface::getInstance().platform().deleteThread(postingThread);
		postingThread = NULL;
	}
	curl_easy_cleanup(curl_obj);
	pthread_mutex_destroy(&report_mutex);
	pthread_mutex_destroy(&logging_mutex);
	pthread_mutex_destroy(&thlog_mutex);
	free(log_buf);
}

KLBPlatformMetrics* KLBPlatformMetrics::getInstance() {
	static KLBPlatformMetrics metrics;
	return &metrics;
}

void KLBPlatformMetrics::patchSignals() {
	if (!signals_patched) {
		memset(&pfmtrx_signal_handler, 0, sizeof(pfmtrx_signal_handler));
		pfmtrx_signal_handler.sa_sigaction = signalHandler;
		pfmtrx_signal_handler.sa_flags = SA_RESETHAND;
		sigaction(SIGILL, &pfmtrx_signal_handler, &default_signal_handlers[SIGILL]);
		sigaction(SIGABRT, &pfmtrx_signal_handler, &default_signal_handlers[SIGABRT]);
		sigaction(SIGSEGV, &pfmtrx_signal_handler, &default_signal_handlers[SIGSEGV]);
		signals_patched = true;
	}
}

void KLBPlatformMetrics::signalHandler(int signal, siginfo_t *info, void *reserved) {
	KLBPlatformMetrics* metrics = getInstance();
	metrics->reportImmediately();
	metrics->rotateLog(false);
	metrics->uploadLogs(true);
	metrics->default_signal_handlers[signal].sa_handler(signal);
}

void KLBPlatformMetrics::unpatchSignals() {
	if (signals_patched) {
		sigaction(SIGILL, &default_signal_handlers[SIGILL], NULL);
		sigaction(SIGABRT, &default_signal_handlers[SIGABRT], NULL);
		sigaction(SIGSEGV, &default_signal_handlers[SIGSEGV], NULL);
		signals_patched = false;
	}
}

void KLBPlatformMetrics::start() {
	patchSignals();
	reportingThread = CPFInterface::getInstance().platform().createThread(ReportingThreadParam, this);
	postingThread = CPFInterface::getInstance().platform().createThread(PostingThreadParam, this);
}

void KLBPlatformMetrics::stop() {
	unpatchSignals();
}

s32 KLBPlatformMetrics::ReportingThreadParam(void * hThread, void * data)
{
	KLBPlatformMetrics* metrics = static_cast<KLBPlatformMetrics*>(data);
	while (true) {
		metrics->reportImmediately();
		sleep(REPORT_INTERVAL_SEC);
	}
}

s32 KLBPlatformMetrics::PostingThreadParam(void * hThread, void * data)
{
	KLBPlatformMetrics* metrics = static_cast<KLBPlatformMetrics*>(data);
	int pass_count = 0;
	while (true) {
		if (!metrics->uploadLogs(false)) {
			if (++pass_count == 8) {
				pass_count = 0;
				metrics->rotateLog(false);
			}
		}
		sleep(REPORT_INTERVAL_SEC);
	}
}

void KLBPlatformMetrics::rotateLog(bool is_lock_gained) {
	if (!is_lock_gained) {
		pthread_mutex_lock(&logging_mutex);
	}

	const size_t path_len = strlen(log_fullpath) + 4;
	char rotated_path[path_len];
	if (log_fp) {
		fclose(log_fp);
		log_fp = NULL;
	}

	struct stat info;
	stat(log_fullpath, &info);
	int suffix;
	for (suffix = 0; suffix < 100; ++suffix) {
		snprintf(rotated_path, path_len, "%s.%d", log_fullpath, suffix);
		if (stat(rotated_path, &info) && !rename(log_fullpath, rotated_path)) {
			if (!is_lock_gained) {
				pthread_mutex_unlock(&logging_mutex);
			}
			return;
		}
	}
	logging_enabled = false;

	if (!is_lock_gained) {
		pthread_mutex_unlock(&logging_mutex);
	}
}

int KLBPlatformMetrics::uploadLogs(bool is_terminating) {
	std::vector<std::string> upload_files;
	struct curl_httppost* form = NULL;
	struct curl_httppost* form_end = NULL;
	struct stat info;
	int suffix = 0;
	while (suffix < 100) {
		std::stringstream path_stream;
		path_stream << log_fullpath << "." << suffix;
		const std::string rotated_path = path_stream.str();
		path_stream << ".sending";
		const std::string sending_path = path_stream.str();

		const int rotated_status = stat(rotated_path.c_str(), &info);
		const int sending_status = stat(sending_path.c_str(), &info);
		if (!rotated_status || !sending_status) {
			if (!rotated_status) {
				rename(rotated_path.c_str(), sending_path.c_str());
			}
			upload_files.push_back(path_stream.str());
			if (!is_terminating && upload_files.size() == 10) {
				break;
			}
		}
		++suffix;
	}

	if (!upload_files.empty()) {
		for (size_t index = 0; index < upload_files.size(); ++index) {
			const std::string& file = upload_files[index];
			stat(file.c_str(), &info);
			const int file_size = info.st_size;

			const int fd = open(file.c_str(), O_RDONLY);
			if (fd < 0) {
				return -1;
			}

			void* file_data = mmap(NULL, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
			char* compressed_data = static_cast<char*>(malloc(file_size));
			z_stream stream;
			stream.zalloc = Z_NULL;
			stream.zfree = Z_NULL;
			stream.opaque = Z_NULL;
			if (deflateInit(&stream, Z_BEST_SPEED) != Z_OK) {
				deflateEnd(&stream);
				munmap(file_data, file_size);
				close(fd);
				return -1;
			}

			stream.avail_in = file_size;
			stream.next_in = static_cast<Bytef*>(file_data);
			stream.avail_out = file_size;
			stream.next_out = reinterpret_cast<Bytef*>(compressed_data);
			const int compression_status = deflate(&stream, Z_FINISH);
			if (compression_status == Z_STREAM_ERROR ||
				stream.avail_in != 0 ||
				stream.avail_out == 0)
			{
				deflateEnd(&stream);
				munmap(file_data, file_size);
				close(fd);
				return -1;
			}

			const int compressed_size = file_size - stream.avail_out;
			deflateEnd(&stream);
			munmap(file_data, file_size);
			close(fd);
			curl_formadd(
				&form,
				&form_end,
				CURLFORM_COPYNAME,
				"files",
				CURLFORM_COPYCONTENTS,
				compressed_data,
				CURLFORM_CONTENTSLENGTH,
				static_cast<long>(compressed_size),
				CURLFORM_END);
			free(compressed_data);
		}

		char* escaped_ident = curl_easy_escape(curl_obj, device_ident, 0);
		char upload_url[1024];
		snprintf(
			upload_url,
			sizeof(upload_url),
			"%s?devid=%s&log_format=%d",
			"http://54.250.124.0/droidlog/",
			escaped_ident,
			4);
		curl_free(escaped_ident);

		curl_easy_setopt(curl_obj, CURLOPT_URL, upload_url);
		curl_easy_setopt(curl_obj, CURLOPT_NOSIGNAL, 1);
		curl_easy_setopt(curl_obj, CURLOPT_HTTPPOST, form);
		if (curl_easy_perform(curl_obj) == CURLE_OK) {
			for (size_t index = 0; index < upload_files.size(); ++index) {
				unlink(upload_files[index].c_str());
			}
		}
		curl_formfree(form);
	}

	const size_t upload_count = upload_files.size();
	upload_files.clear();
	return upload_count;
}

int KLBPlatformMetrics::rotateAndUploadLog(bool is_terminating) {
	rotateLog(false);
	return uploadLogs(is_terminating);
}

void KLBPlatformMetrics::appendLog(const char* msg, int msg_len, u8 msg_type) {
	if (!logging_enabled) return;

	pthread_mutex_lock(&logging_mutex);
	if ((msg_type >= MSGTYPE_INIT) && (msg_type <= MSGTYPE_SCREENSHOT)) {
		msg_len = strlen(msg);
	}

	msgpack_sbuffer* buffer = msgpack_sbuffer_new();
	msgpack_packer* packer = msgpack_packer_new(buffer, msgpack_sbuffer_write);
	msgpack_pack_long(packer, CAndroidRequest::getInstance()->nanotime());
	msgpack_pack_uint8(packer, msg_type);
	msgpack_pack_raw(packer, msg_len);
	msgpack_pack_raw_body(packer, msg, msg_len);

	if (!log_fp) log_fp = fopen(log_fullpath, "a");
	if (log_fp) {
		fwrite(buffer->data, 1, buffer->size, log_fp);
		if (ftell(log_fp) >= 0x10000) rotateLog(true);
	} else {
		CPFInterface::getInstance().platform().logging("fatal logging error occurred.");
	}
	msgpack_packer_free(packer);
	msgpack_sbuffer_free(buffer);
	pthread_mutex_unlock(&logging_mutex);
}

void KLBPlatformMetrics::reportImmediately(const char* msg) {
	pthread_mutex_lock(&report_mutex);

	long rss = 0;
	FILE* fp = fopen("/proc/self/statm", "r");
	if (!fp) {
		rss = -2;
	}
	if (fscanf(fp, "%*s%ld", &rss) != 1) {
		rss = -1;
	}
	if (fp) {
		fclose(fp);
	}

	if (rss > 0) {
		rss *= page_size;
		malloc_info = mallinfo();
		rss -= malloc_info.fordblks;
	}

	getrusage(RUSAGE_SELF, &basic_metrics);
	int msg_len = snprintf(
		log_buf,
		log_buf_len - 1,
		"PfMtrx: utime=%ld, stime=%ld, maxrss=%ld, rss=%ld, diff=%ld, fdcnt=%d \"%s\"",
		basic_metrics.ru_utime.tv_sec * 1000 + basic_metrics.ru_utime.tv_usec / 1000,
		basic_metrics.ru_stime.tv_sec * 1000 + basic_metrics.ru_stime.tv_usec / 1000,
		basic_metrics.ru_maxrss << 10,
		rss,
		rss - last_rss,
		getFdCount(),
		msg
	);
	if (!msg[0]) {
		log_buf[msg_len - 3] = '\0';
		msg_len -= 3;
	}
	CPFInterface::getInstance().platform().logging(log_buf);
	appendLog(log_buf, msg_len, MSGTYPE_RES_REPORT);
	last_rss = rss;

	pthread_mutex_unlock(&report_mutex);
}

int KLBPlatformMetrics::getFdCount() {
	int fd_count = 0;
	DIR* dir = opendir("/proc/self/fd");
	if (dir) {
		while (readdir(dir)) {
			fd_count++;
		}
		closedir(dir);
		fd_count -= 2;
	} else {
		fd_count = -1;
	}
	return fd_count;
}

void KLBPlatformMetrics::getOrGenerateDeviceIdent(const char* basefile_path) {
	size_t path_len = strlen(basefile_path) + 5;
	char path[path_len];
	snprintf(path, path_len, "%s.dev", basefile_path);

	struct stat info;
	char ident[256];
	FILE* fp;
	if (stat(path, &info)) {
		jvalue value;
		CAndroidRequest::getInstance()->callJavaMethod(NULL, value, "generateDeviceIdent", 'S', "");
		const char* generated = CJNI::getJNIEnv()->GetStringUTFChars(static_cast<jstring>(value.l), NULL);
		int generated_len = strlen(generated);
		strncpy(ident, generated, generated_len + 1);
		CJNI::getJNIEnv()->ReleaseStringUTFChars(static_cast<jstring>(value.l), generated);
		device_ident_len = generated_len;

		fp = fopen(path, "w");
		if (fp) {
			fwrite(ident, 1, device_ident_len, fp);
			fclose(fp);
		} else {
			CPFInterface::getInstance().platform().logging("failed to open devident file for writing: %s", path);
		}
	} else {
		fp = fopen(path, "r");
		if (fp) {
			device_ident_len = fread(ident, 1, sizeof(ident), fp);
			fclose(fp);
		} else {
			CPFInterface::getInstance().platform().logging("failed to open existing devident file: %s", path);
		}
	}

	if (device_ident_len) {
		device_ident = (char*)calloc(device_ident_len + 1, 1);
		strncpy(device_ident, ident, device_ident_len + 1);
	}
}

int KLBPlatformMetrics::generateDeviceIdent(char* outbuf, int buflen)
{
	jvalue value;
	CAndroidRequest::getInstance()->callJavaMethod(
		NULL, value, "generateDeviceIdent", 'S', "");
	const char* generated = CJNI::getJNIEnv()->GetStringUTFChars(
		static_cast<jstring>(value.l), NULL);
	int generated_len = strlen(generated);
	strncpy(outbuf, generated, generated_len + 1);
	CJNI::getJNIEnv()->ReleaseStringUTFChars(
		static_cast<jstring>(value.l), generated);
	return generated_len;
}

inline int KLBPlatformMetrics::logMeasuringThreadCpu(u16 target) {
	struct timespec thread_cpu;
	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &thread_cpu);
	s64 thread_cpu_ns =
		thread_cpu.tv_sec * 1000000000L + thread_cpu.tv_nsec;

	msgpack_sbuffer* buffer = msgpack_sbuffer_new();
	msgpack_packer* packer = msgpack_packer_new(buffer, msgpack_sbuffer_write);
	msgpack_pack_uint16(packer, target);
	msgpack_pack_int(packer, gettid());
	msgpack_pack_long(packer, thread_cpu_ns);
	appendLog(
		buffer->data, buffer->size, MSGTYPE_THREAD_MEASURE);
	msgpack_packer_free(packer);
	msgpack_sbuffer_free(buffer);
	return 0;
}

void KLBPlatformMetrics::measureThreadCpu(u16 target) {
	pthread_mutex_lock(&thlog_mutex);
	logMeasuringThreadCpu(target);
	pthread_mutex_unlock(&thlog_mutex);
}

void KLBPlatformMetrics::logFrameSummary(int deltaT, s64 frameProcStart) {
	msgpack_sbuffer* buffer = msgpack_sbuffer_new();
	msgpack_packer* packer = msgpack_packer_new(buffer, msgpack_sbuffer_write);
	msgpack_pack_uint16(packer, deltaT);
	msgpack_pack_long(packer, frameProcStart);
	appendLog(buffer->data, buffer->size, MSGTYPE_FRAME_SUMMARY);
	msgpack_packer_free(packer);
	msgpack_sbuffer_free(buffer);
}
