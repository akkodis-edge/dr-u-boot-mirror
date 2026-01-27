obj-$(CONFIG_$(PHASE_)AKE_PLATFORM_HEADER) += platform_header.o
obj-$(CONFIG_$(PHASE_)AKE_IMX8M_DDRC) += imx8m_ddrc_parse.o
obj-$(CONFIG_$(PHASE_)AKE_LIBNVRAM) += libnvram/libnvram.o libnvram/crc32.o
ifeq ($(CONFIG_SPL_BUILD),y)
obj- := __dummy__.o
obj-$(CONFIG_SPL_AKE_COMMON) += spl_common.o
else
obj-$(CONFIG_AKE_NVRAM) += nvram.o
obj-$(CONFIG_CMD_AKE_NVRAM) += nvram_cmd.o
obj-$(CONFIG_CMD_AKE_SYSTEM_BOOT) += system_boot.o
obj-$(CONFIG_CMD_AKE_ANDROID_BOOT) += android_boot.o
endif
