#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>
#include <curl/curl.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/evp.h>

#ifndef API_KEY
#define API_KEY "YOUR_API_KEY"
#endif

#ifndef DOMAIN
#define DOMAIN "example.com"
#endif

#ifndef CERT_RENEW_THRESHOLD_DAYS
#define CERT_RENEW_THRESHOLD_DAYS 10
#endif

#define CERT_PATH_FMT "/etc/ssl/certs/%s-fullchain.pem"
#define URL_FMT       "https://client:%s@vps8.zz.cd/api/client/certcenter/download"
#define POST_FMT      "domain=%s&type=fullchain&save=1"

struct memory {
	char *response;
	size_t size;
};

static size_t write_callback(char *contents, size_t size, size_t nmemb, void *userp) {
	size_t realsize = size * nmemb;
	struct memory *mem = (struct memory *)userp;

	char *ptr = realloc(mem->response, mem->size + realsize + 1);
	if(!ptr) {
		/* out of memory! */
		fprintf(stderr, "not enough memory (realloc returned NULL)\n");
		return 0;
	}

	mem->response = ptr;
	memcpy(&(mem->response[mem->size]), contents, realsize);
	mem->size += realsize;
	mem->response[mem->size] = 0;

	return realsize;
}

// 从内存数据中解析 X509 证书并获取 SHA-256 指纹
// 成功返回 1 并在 md_buf 中填入指纹，失败返回 0
int get_mem_cert_fingerprint(const char *cert_data, size_t data_len, unsigned char *md_buf, unsigned int *md_len) {
	BIO *bio = BIO_new_mem_buf(cert_data, (int)data_len);
	if (!bio) return 0;

	X509 *cert = PEM_read_bio_X509(bio, NULL, NULL, NULL);
	BIO_free(bio);

	if (!cert) {
		return 0;
	}

	int ret = X509_digest(cert, EVP_sha256(), md_buf, md_len);
	X509_free(cert);
	return ret;
}

int main() {
	char cert_path[256];
	snprintf(cert_path, sizeof(cert_path), CERT_PATH_FMT, DOMAIN);

	printf("Checking certificate status for domain [%s]...\n", DOMAIN);

	// ======== 检查本地证书过期时间 ========
	int day = -10086, sec = 0, has_local = 0;
	unsigned char local_md[EVP_MAX_MD_SIZE];
	unsigned int local_md_len = 0;
	FILE *fp = NULL;
	if (access(cert_path, F_OK) == 0) {
		fp = fopen(cert_path, "r");
		if (!fp) {
			fprintf(stderr, "Cannot open %s: %s\n", cert_path, strerror(errno));
			return -1; 
		}
		X509 *cert = PEM_read_X509(fp, NULL, NULL, NULL);
		if (cert) {
			// 获取证书过期时间
			const ASN1_TIME *not_after = X509_get0_notAfter(cert);
			// 获取当前时间
			ASN1_TIME *now = ASN1_TIME_new();
			if (!now) {
				fprintf(stderr, "Failed to ASN1_TIME_new()\n");
				X509_free(cert);
				fclose(fp);
				return -1;
			}
			ASN1_TIME_set(now, time(NULL));

			if (ASN1_TIME_diff(&day, &sec, now, not_after)) {
				if (day < CERT_RENEW_THRESHOLD_DAYS) {
					// 获取本地证书指纹
					has_local = X509_digest(cert, EVP_sha256(), local_md, &local_md_len);
				}
			}
			ASN1_TIME_free(now);
			X509_free(cert);
		} else {
			fprintf(stderr, "Failed to parse certificate.\n");
		}
		fclose(fp);
		fp = NULL;
	}

	// ==================
	if (day >= CERT_RENEW_THRESHOLD_DAYS && day !=  -10086) {
		// 证书过期时间大于阈值天数
		printf("Certificate will expire in %d days.\n", day);
		return 0;
	}

	printf("Fetching the latest certificate from VPS8...\n");
	
	curl_global_init(CURL_GLOBAL_ALL);

	// ======== 下载证书 ========
	CURL *curl;
	CURLcode res;
	struct memory chunk;
	chunk.response = NULL;
	chunk.size = 0;

	curl = curl_easy_init();
	if (!curl) {
		fprintf(stderr, "curl_easy_init() error\n");
		free(chunk.response);
		curl_global_cleanup();
		return -1;
	}

	// 拼装URL和POST表单
	char url[512];
	char post_fields[256];
	snprintf(url, sizeof(url), URL_FMT, API_KEY);
	snprintf(post_fields, sizeof(post_fields), POST_FMT, DOMAIN);
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_fields);

	// 使用内存回调函数
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);

	// 检查是否请求成功
	res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		fprintf(stderr, "curl_easy_perform() error: %s\n", curl_easy_strerror(res));
		goto err_cleanup;
	}

	// 检查HTTP Code是否是200
	long response_code = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
	if (response_code != 200) {
		fprintf(stderr, "API returned HTTP error code: %ld\n", response_code);
		goto err_cleanup;
	}

	// 获取新证书的指纹
	unsigned char mem_md[EVP_MAX_MD_SIZE];
	unsigned int mem_md_len = 0;
	if (get_mem_cert_fingerprint(chunk.response, chunk.size, mem_md, &mem_md_len) != 1) {
		fprintf(stderr, "TLS certificate parsing failed\n");
		goto err_cleanup;
	}

	// 对比证书指纹
	if (has_local && local_md_len > 0 && local_md_len == mem_md_len && memcmp(local_md, mem_md, local_md_len) == 0) {
		printf("Downloaded certificate fingerprint matches local certificate. No update needed.\n");
		// 下载的证书指纹无变化
		printf("Certificate was identical. Skipped reloading.\n");
		goto cleanup;
	} else {
		// 指纹不匹配或者本地无证书，直接写入指定路径
		fp = fopen(cert_path, "w");
		if (!fp) {
			perror("Failed to write certificate to disk");
			goto err_cleanup;
		}

		fwrite(chunk.response, 1, chunk.size, fp);
		fclose(fp);
		// 更新成功
		printf("New certificate successfully saved to: %s\n", cert_path);
#ifdef RELOADCMD
		// 执行 reload command
		printf("Executing reload command...\n");
		int ret = system(RELOADCMD);
		if (ret != 0) {
			fprintf(stderr, "Failed to reload service. Check permissions or command syntax.\n");
			goto err_cleanup;
		}
		printf("Service reloaded successfully!\n");
#endif
	}

cleanup:
	curl_easy_cleanup(curl);
	free(chunk.response);
	curl_global_cleanup();
	return 0;

err_cleanup:
	if (curl) curl_easy_cleanup(curl);
	free(chunk.response);
	curl_global_cleanup();
	return -1;
}
