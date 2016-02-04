LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE    := sample_rooms
LOCAL_SRC_FILES := client_sample_rooms.c
LOCAL_CFLAGS 	+= -I./include
LOCAL_LDLIBS    += -llog -L./lib -lclient
#LOCAL_SHARED_LIBRARIES := libclient

include $(BUILD_EXECUTABLE)

###################################

include $(CLEAR_VARS)

LOCAL_MODULE    := sample_netplay
LOCAL_SRC_FILES := client_sample_netplay.c
LOCAL_CFLAGS 	+= -I./include
LOCAL_LDLIBS    += -llog -L./lib -lclient
#LOCAL_SHARED_LIBRARIES := libclient

include $(BUILD_EXECUTABLE)
