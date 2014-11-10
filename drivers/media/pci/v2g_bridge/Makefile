V4L2_MOD_DIR = drivers/media/video
V2G_BRIDGE_MOD_DIR = drivers/media/pci/v2g_bridge

ccflags-y := -I$(EGD_MOD_DIR)/include -I$(V4L2_MOD_DIR)
ccflags-y += -DDEBUG -DV2G_CAMERA_PROFILE

ccflags-$(CONFIG_V2G_BRIDGE_RELEASE) += -DRELEASE -DV2G_CAMERA_PROFILE

obj-y := v2g_bridge.o
