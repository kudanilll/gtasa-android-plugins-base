LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)
LOCAL_CPP_EXTENSION := .cpp .cc
LOCAL_MODULE    := modtemplate
LOCAL_SRC_FILES := main.cpp \
                   mod/logger.cpp \
                   mod/config.cpp \
                   mod/texture_loader.cpp \
                   mod/thirdparty/imgui/imgui.cpp \
                   mod/thirdparty/imgui/imgui_draw.cpp \
                   mod/thirdparty/imgui/imgui_tables.cpp \
                   mod/thirdparty/imgui/imgui_widgets.cpp \
                   plugin-sdk/renderware/imgui/imgui_impl_renderware.cpp
LOCAL_CFLAGS += -O2 -DNDEBUG -std=c++17
LOCAL_CPPFLAGS += -Wall -Wextra -Wno-unused-parameter -fvisibility=hidden -fvisibility-inlines-hidden
ifeq ($(TARGET_ARCH_ABI),armeabi-v7a)
    LOCAL_CFLAGS += -mfloat-abi=softfp
endif
LOCAL_C_INCLUDES += $(LOCAL_PATH) $(LOCAL_PATH)/plugin-sdk $(LOCAL_PATH)/mod/thirdparty
LOCAL_LDLIBS += -llog
include $(BUILD_SHARED_LIBRARY)
