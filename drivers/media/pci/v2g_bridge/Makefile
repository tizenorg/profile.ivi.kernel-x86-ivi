V4L2_MOD_DIR = drivers/media/video
V2G_BRIDGE_MOD_DIR = drivers/media/pci/v2g_bridge

ccflags-y := -I$(V4L2_MOD_DIR)

ifeq ($(CONFIG_V2G_BRIDGE_RELEASE), y)
	ccflags-y += -DRELEASE -DV2G_CAMERA_PROFILE
endif

ifeq ($(CONFIG_V2G_BRIDGE_DEBUG), y)
	ccflags-y += -DDEBUG -DV2G_CAMERA_PROFILE
endif

obj-y := v2g_bridge.o
