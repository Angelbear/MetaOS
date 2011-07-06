# Copyright 2006 The Android Open Source Project

LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
	rild.c


LOCAL_SHARED_LIBRARIES := \
	libcutils \
	libril

ifeq ($(TARGET_ARCH),arm)
LOCAL_SHARED_LIBRARIES += libdl
endif # arm

LOCAL_CFLAGS := -DRIL_SHLIB

LOCAL_MODULE:= rild

include $(BUILD_EXECUTABLE)

# For radiooptions binary
# =======================
include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
	radiooptions.c

LOCAL_SHARED_LIBRARIES := \
	libcutils \

LOCAL_CFLAGS := \

LOCAL_MODULE:= radiooptions
LOCAL_MODULE_TAGS := debug

include $(BUILD_EXECUTABLE)

# For radiooptions binary
# =======================
include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
    sendmessage.c

LOCAL_SHARED_LIBRARIES := \
    libcutils \

LOCAL_CFLAGS := \

LOCAL_MODULE:= sendmessage
LOCAL_MODULE_TAGS := debug

include $(BUILD_EXECUTABLE)

# For radiooptions binary
# =======================
include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
    sendPDU.c

LOCAL_SHARED_LIBRARIES := \
    libcutils \

LOCAL_CFLAGS := \

LOCAL_MODULE:= sendpdu
LOCAL_MODULE_TAGS := debug

include $(BUILD_EXECUTABLE)

