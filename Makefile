# ==========================================
#  Certificate Updater Automation Makefile
# ==========================================

# 默认配置（你也可以在编译时通过命令行覆盖它们）
DOMAIN     ?= example.com
API_KEY    ?= YOUR_API_KEY
#RELOADCMD  ?= service httpd restart

# 编译参数与目标
TARGET     = vps8_cert_updater
SRC        = main.c
CC         = gcc
CFLAGS     = -Wall -O2
LIBS       = -lcurl -lcrypto

# 注入宏定义
DEFS       = -DDOMAIN="\"$(DOMAIN)\"" \
             -DAPI_KEY="\"$(API_KEY)\"" \
             -DRELOADCMD="\"$(RELOADCMD)\""

.PHONY: all clean install help

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(DEFS) $(LIBS)
	@echo "--------------------------------------------------"
	@echo " Successfully built $(TARGET) for [$(DOMAIN)]!"
	@echo "--------------------------------------------------"

clean:
	rm -f $(TARGET)
	@echo "Cleaned up workspace."

install: $(TARGET)
	@if [ -d /etc/periodic/weekly ]; then \
		cp $(TARGET) /etc/periodic/weekly/$(TARGET); \
		echo "Installed $(TARGET) to /etc/periodic/weekly/"; \
	else \
		echo "Error: /etc/periodic/weekly directory not found!"; \
		exit 1; \
	fi

help:
	@echo "Usage:"
	@echo "  make                      - Build with default variables"
	@echo "  make DOMAIN=\"abc.com\" API_KEY=\"xyz\" RELOADCMD=\"rc-service nginx restart\""
	@echo "                            - Build with custom variables"
	@echo "  make install              - Copy compiled binary to /etc/periodic/weekly/"
	@echo "  make clean                - Remove compiled binary"
