# Copyright 2006 The Android Open Source Project

#/******************************************************************************
#*(C) Copyright 2008 Marvell International Ltd.
#* All Rights Reserved
#******************************************************************************/

# XXX using libutils for simulator build only...
#
LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
    marvell-ril.c \
    ril-cc.c \
    ril-mm.c \
    ril-ps.c \
    ril-ss.c \
    ril-msg.c \
    ril-sim.c \
    ril-dev.c \
    atchannel.c \
    dataapi.c \
    misc.c \
    at_tok.c

LOCAL_SHARED_LIBRARIES := \
	libcutils libutils libril libnetutils

	# for asprinf
LOCAL_CFLAGS := -D_GNU_SOURCE 

ifneq ($(HAS_TD_MODEM),true)
LOCAL_CFLAGS += -DDKB_CP
else
LOCAL_CFLAGS += -DBROWNSTONE_CP
endif

LOCAL_C_INCLUDES := $(KERNEL_HEADERS)

#build shared library
LOCAL_SHARED_LIBRARIES += \
	libcutils libutils
LOCAL_LDLIBS += -lpthread
LOCAL_CFLAGS += -DRIL_SHLIB
LOCAL_MODULE:= libmarvell-ril
include $(BUILD_SHARED_LIBRARY)
