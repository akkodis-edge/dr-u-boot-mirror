ifeq ($(CONFIG_SPL_BUILD),y)
obj- := __dummy__.o
obj-$(CONFIG_SPL_LIBNVRAM) += libnvram/libnvram.o libnvram/crc32.o
obj-$(CONFIG_SPL_DR_PLATFORM_HEADER) += platform_header.o
obj-$(CONFIG_SPL_DR_IMX8M_DDRC) += imx8m_ddrc_parse.o
else
libnvram-y := libnvram/libnvram.o libnvram/crc32.o
obj-$(CONFIG_DR_NVRAM) += nvram.o libnvram.o
obj-$(CONFIG_CMD_DR_NVRAM) += nvram_cmd.o
obj-$(CONFIG_CMD_DR_SYSTEM_BOOT) += system_boot.o
obj-$(CONFIG_CMD_DR_ANDROID_BOOT) += android_boot.o
obj-$(CONFIG_DR_PLATFORM_HEADER) += platform_header.o
obj-$(CONFIG_DR_IMX8M_DDRC) += imx8m_ddrc_parse.o
endif
