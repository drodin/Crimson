LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := sdl_ttf

LOCAL_CFLAGS := -I$(LOCAL_PATH) -I$(LOCAL_PATH)/../sdl/include -I$(LOCAL_PATH)/../freetype/include

LOCAL_CPP_EXTENSION := .cpp

LOCAL_SRC_FILES := SDL_ttf.c

include $(BUILD_STATIC_LIBRARY)

