#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <curl/curl.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

#ifndef API_KEY
#define API_KEY "YOUR_API_KEY"
#endif

#ifndef DOMAIN
#define DOMAIN "example.com"
#endif

#define CERT_PATH_FMT "/etc/ssl/certs/%s-fullchain.pem"
#define URL_FMT       "https://client:%s@vps8.zz.cd/api/client/certcenter/download"
#define POST_FMT      "domain=%s&type=fullchain&save=1"

// 检查证书是否在10天内过期
int check_cert_expiry(const char *filepath) {
	FILE *fp = fopen(filepath, "r");
	if (!fp) {
		printf("Certificate file not found. Needs a new download.\n");
		return 1; 
	}

	X509 *cert = PEM_read_X509(fp, NULL, NULL, NULL);
	fclose(fp);

	if (!cert) {
		printf("Failed to parse certificate. Treating as expired.\n");
		return 1;
	}

	ASN1_TIME *not_after = X509_getm_notAfter(cert);
	int day = 0, sec = 0;

	// 检查证书时间是否在 10 天内过期
	if (X509_cmp_current_time(not_after) <= 0) {
		printf("Certificate has already expired.\n");
		X509_free(cert);
		return 1;
	}
	if (ASN1_TIME_diff(&day, &sec, NULL, not_after)) {
		if (day < 10) {
			printf("Certificate will expire in %d days. Update required.\n", day);
			X509_free(cert);
			return 1;
		}
	}

	printf("Certificate is safe. Remaining days: %d\n", day);
	X509_free(cert);
	return 0;
}

// 下载证书并保存
int download_cert(const char *filepath) {
	CURL *curl_handle;
	CURLcode res;
	long response_code = 0;
	int success = 0;

	char tmppath[strlen(filepath) + 5];
	snprintf(tmppath, sizeof(tmppath), "%s.tmp", filepath);

	FILE *fp = fopen(tmppath, "w");
	if (!fp) {
		fprintf(stderr, "Failed to open temporary file for writing: %s\n", tmppath);
		return 0;
	}

	curl_handle = curl_easy_init();
	if (!curl_handle) {
		fclose(fp);
		remove(tmppath);
		return 0;
	}

	// 拼接 URL 和 POST 参数
	char url[512];
	char post_fields[256];
	snprintf(url, sizeof(url), URL_FMT, API_KEY);
	snprintf(post_fields, sizeof(post_fields), POST_FMT, DOMAIN);

	curl_easy_setopt(curl_handle, CURLOPT_URL, url);
	curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, post_fields);
	curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, fp);

	res = curl_easy_perform(curl_handle);

	if (res == CURLE_OK) {
		curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &response_code);
		if (response_code == 200) {
			success = 1;
		} else {
			fprintf(stderr, "API returned HTTP error code: %ld\n", response_code);
		}
	} else {
		fprintf(stderr, "curl_easy_perform() error: %s\n", curl_easy_strerror(res));
	}

	curl_easy_cleanup(curl_handle);
	fclose(fp);

	if (success) {
		if (rename(tmppath, filepath) == 0) {
			printf("New certificate successfully saved to: %s\n", filepath);
			return 1;
		} else {
			perror("Failed to replace old certificate file with temporary file");
			remove(tmppath);
			return 0;
		}
	} else {
		remove(tmppath);
		return 0;
	}
}

int main() {
	char cert_path[256];
	snprintf(cert_path, sizeof(cert_path), CERT_PATH_FMT, DOMAIN);

	printf("Checking certificate status for domain [%s]...\n", DOMAIN);

	if (check_cert_expiry(cert_path)) {
		printf("Fetching the latest certificate from server...\n");
		
		curl_global_init(CURL_GLOBAL_ALL);
		if (download_cert(cert_path)) {
			curl_global_cleanup();

#ifdef RELOADCMD
			printf("Executing reload command...\n");
			int ret = system(RELOADCMD);
			if (ret == 0) {
				printf("Service reloaded successfully!\n");
				return 0;
			} else {
				fprintf(stderr, "Failed to reload service. Check permissions or command syntax.\n");
				return 3;
			}
#endif
		} else {
			fprintf(stderr, "Certificate update failed.\n");
			curl_global_cleanup();
			return 1;
		}
	}

	return 0;
}
