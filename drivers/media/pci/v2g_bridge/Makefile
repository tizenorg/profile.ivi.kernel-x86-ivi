# Really simple kbuild makefile for an out of tree build.
# Make sure $EGD_ROOT and $KERNELDIR are set correctly.
#EGD_MOD_DIR = drivers/gpu/drm/emgd
V4L2_MOD_DIR = drivers/media/video
V2G_BRIDGE_MOD_DIR = drivers/staging/v2g_bridge

ccflags-y := -I$(EGD_MOD_DIR)/include \
	-I$(V4L2_MOD_DIR) \

#ccflags-$(CONFIG_V2G_BRIDGE_DEBUG) += -DDEBUG -DV2G_CAMERA_PROFILE
ccflags-y += -DDEBUG -DV2G_CAMERA_PROFILE

ccflags-$(CONFIG_V2G_BRIDGE_RELEASE) += -DRELEASE -DV2G_CAMERA_PROFILE

#obj-$(CONFIG_V2G_BRIDGE) := v2g_bridge.o 
obj-y := v2g_bridge.o 
