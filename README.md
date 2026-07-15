# VPS证书自动更新工具

一个轻量脚本，配合 cron 定期任务自动检查并更新 TLS 证书。

## 编译与安装

根据你的系统安装依赖：
- **Alpine:** `apk add curl-dev openssl-dev gcc libc-dev make`
- **Debian/Ubuntu:** `sudo apt install libcurl4-openssl-dev libssl-dev gcc make`

### 1. 一键编译
使用 `make` 传入你的配置（别忘了把命令换成你实际的重启命令，如 `nginx -s reload`）：

```shell
make DOMAIN="你的域名" API_KEY="你的API密钥" RELOADCMD="nginx -s reload"
```

### 2. 测试运行

```shell
./vps8_cert_updater
```

### 3. 丢进定时任务

编译没问题后，直接把它复制到每周定时任务目录：

```shell
sudo make install
```

## 说明

- 保存路径: /etc/ssl/certs/<你的域名>-fullchain.pem

- 脚本路径: /etc/periodic/weekly/cert_updater

- 逻辑: 如果文件不存在或距离过期少于 10 天，就会自动下载新证书并执行重启命令。
