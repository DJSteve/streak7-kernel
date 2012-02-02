#
# Makefile for the video capture/playback device drivers.
#
ifneq ($(CONFIG_ICS),y)
obj-y				+= avp/
else
obj-y                           += avp-ics/
endif

obj-$(CONFIG_TEGRA_MEDIASERVER)	+= mediaserver/
obj-$(CONFIG_TEGRA_CAMERA)	+= tegra_camera.o
obj-$(CONFIG_VIDEO_OV5650)	+= ov5650.o
obj-$(CONFIG_VIDEO_OV2710)	+= ov2710.o
obj-$(CONFIG_VIDEO_SOC380)	+= soc380.o
obj-$(CONFIG_TORCH_SSL3250A)	+= ssl3250a.o
obj-$(CONFIG_VIDEO_SH532U)	+= sh532u.o
obj-$(CONFIG_VIDEO_AD5820)	+= ad5820.o

obj-$(CONFIG_VIDEO_OV5642)      += ov5642.o ov5642_comm.o ov5642_iq.o

obj-$(CONFIG_VIDEO_OV9665)	+= ov9665.o
obj-$(CONFIG_VIDEO_OV9665)	+= aptina1040.o

