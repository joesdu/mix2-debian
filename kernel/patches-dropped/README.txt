Dropped patches (kept for reference)

0013-porthole-drm-msm-dsi_phy_10nm-fix-VCO-rate.patch
  Wrong for the Mi MIX 2: the mainline x2 VCO formula matches the PLL as the
  bootloader programs it (DEC 47.9, VCO 1839.6 MHz, outdiv 2). With the patch
  msm programs DEC 95 = 3.68 GHz (above the 3.5 GHz limit); the clock tree then
  believes half the real rates and the link breaks after the first DPMS cycle.
