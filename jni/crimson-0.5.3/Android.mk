LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := application

APP_SUBDIRS := $(patsubst $(LOCAL_PATH)/%, %, $(shell find $(LOCAL_PATH)/src/cf -type d))
APP_SUBDIRS += $(patsubst $(LOCAL_PATH)/%, %, $(shell find $(LOCAL_PATH)/src/common -type d))

LOCAL_CFLAGS := $(foreach D, $(APP_SUBDIRS), -I$(LOCAL_PATH)/$(D)) \
				-I$(LOCAL_PATH)/../sdl/include \
				-I$(LOCAL_PATH)/../sdl_ttf \

LOCAL_CFLAGS += $(APPLICATION_ADDITIONAL_CFLAGS)

LOCAL_CFLAGS += \
    -D_GNU_SOURCE=1 -D_REENTRANT -DPACKAGE_NAME=\"Crimson\ Fields\" -DPACKAGE_TARNAME=\"crimson\" -DPACKAGE_VERSION=\"0.5.3\" -DPACKAGE_STRING=\"Crimson\ Fields\ 0.5.3\" -DPACKAGE_BUGREPORT=\"jensgr@gmx.net\" -DPACKAGE=\"crimson\" -DVERSION=\"0.5.3\" -DSTDC_HEADERS=1 -DHAVE_SYS_TYPES_H=1 -DHAVE_SYS_STAT_H=1 -DHAVE_STDLIB_H=1 -DHAVE_STRING_H=1 -DHAVE_MEMORY_H=1 -DHAVE_STRINGS_H=1 -DHAVE_INTTYPES_H=1 -DHAVE_STDINT_H=1 -DHAVE_UNISTD_H=1 -DDISABLE_SOUND=1 -DDISABLE_NETWORK=1 -DHAVE_LIBZ=1 -DHAVE_DIRENT_H=1 -DHAVE_STRCASECMP=1 -DHAVE_STRNCASECMP=1 -DCF_DATADIR=\"\\/sdcard\\/$(SDL_CURDIR_PATH)\\/\"

#Change C++ file extension as appropriate
LOCAL_CPP_EXTENSION := .cpp

LOCAL_SRC_FILES := $(foreach F, $(APP_SUBDIRS), $(addprefix $(F)/,$(notdir $(wildcard $(LOCAL_PATH)/$(F)/*.cpp))))
# Uncomment to also add C sources
LOCAL_SRC_FILES += $(foreach F, $(APP_SUBDIRS), $(addprefix $(F)/,$(notdir $(wildcard $(LOCAL_PATH)/$(F)/*.c))))

include $(BUILD_STATIC_LIBRARY)
