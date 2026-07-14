# ==========================================
#  Certificate Updater Automation Makefile
# ==========================================

# 默认配置
API_KEY    ?= YOUR_API_KEY
DOMAIN     ?= example.com
#RELOADCMD  ?= service httpd restart

# 编译参数与目标
TARGET     = vps8_cert_updater
SRC        = main.c
CC         = gcc
CFLAGS     = -Wall -O2
LIBS       = -lcurl -lcrypto

# 注入宏定义
DEFS       = -DDOMAIN="\"$(DOMAIN)\"" \
             -DAPI_KEY="\"$(API_KEY)\""

ifneq ($(RELOADCMD),)
DEFS      += -DRELOADCMD="\"$(RELOADCMD)\""
endif

.PHONY: all clean install help

ifeq ($(DOMAIN),example.com)
$(error ❌ Error: DOMAIN is not defined! Use 'make DOMAIN="yourdomain.com"')
endif

ifeq ($(API_KEY),YOUR_API_KEY)
$(error ❌ Error: API_KEY is not defined! Use 'make API_KEY="your_key"')
endif

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
