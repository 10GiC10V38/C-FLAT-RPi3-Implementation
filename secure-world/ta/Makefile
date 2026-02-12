CFG_TEE_TA_LOG_LEVEL ?= 3

# UUID from cflat_ta.h
BINARY=a1b2c3d4-5678-9abc-def0-123456789abc

-include $(TA_DEV_KIT_DIR)/mk/ta_dev_kit.mk

ifeq ($(wildcard $(TA_DEV_KIT_DIR)/mk/ta_dev_kit.mk), )
$(error TA_DEV_KIT_DIR is not correctly defined or invalid)
endif
