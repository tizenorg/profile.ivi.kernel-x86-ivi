V4L2_MOD_DIR = drivers/media/video
V2G_BRIDGE_MOD_DIR = drivers/media/pci/v2g_bridge

ccflags-y := -I$(V4L2_MOD_DIR)

ifeq ($(CONFIG_V2G_BRIDGE_DEBUG), y)
	ccflags-$(CONFIG_V2G_BRIDGE_DEBUG) += -DDEBUG
endif

obj-y := v2g_bridge.o
