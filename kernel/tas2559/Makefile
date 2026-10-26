# SPDX-License-Identifier: GPL-2.0-only
# Offline port only: probe remains blocked until safety validation is complete.
ccflags-y += -DCONFIG_TAS2559_CODEC
obj-m += snd-soc-tas2559.o
snd-soc-tas2559-y := tas2559-core.o tas2559-regmap.o tas2559-codec.o tas2559-playback.o
