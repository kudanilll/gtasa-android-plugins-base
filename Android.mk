LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)
LOCAL_CPP_EXTENSION := .cpp .cc
LOCAL_MODULE    := modtemplate
LOCAL_SRC_FILES := main.cpp mod/logger.cpp mod/config.cpp
LOCAL_CFLAGS += -O2 -DNDEBUG -std=c++17
ifeq ($(TARGET_ARCH_ABI),armeabi-v7a)
    LOCAL_CFLAGS += -mfloat-abi=softfp
endif
LOCAL_C_INCLUDES += $(LOCAL_PATH) $(LOCAL_PATH)/plugin-sdk
LOCAL_LDLIBS += -llog
include $(BUILD_SHARED_LIBRARY)
