ccflags-y := -Iinclude/drm

ipvr-y := \
        ipvr_drv.o \
        ipvr_bo.o \
        ipvr_exec.o \
        ipvr_fence.o \
        ipvr_gem.o \
        ipvr_mmu.o \
        ipvr_debug.o \
        ipvr_trace.o \
        ved_pm.o \
        ved_cmd.o \
        ved_fw.o

obj-$(CONFIG_DRM_IPVR) += ipvr.o

CFLAGS_ipvr_trace.o := -I$(src)
