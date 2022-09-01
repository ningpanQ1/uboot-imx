ifdef CONFIG_TARGET_ADV_EAMB9918
obj-y += imx8mm_advantech.o
obj-y += ../../freescale/common/

ifdef CONFIG_SPL_BUILD
obj-y += spl.o
ifdef CONFIG_IMX8M_4G_LPDDR4
obj-y += lpddr4_timing_4g.o
else
obj-y += lpddr4_timing_eamb9918a1_2g.o
endif
endif

endif
