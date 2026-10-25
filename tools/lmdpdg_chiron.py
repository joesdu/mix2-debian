"""Run lmdpdg on the Xiaomi chiron stock DTB, whose panels live directly below
/soc instead of below the qcom,mdss_mdp node that lmdpdg searches.

usage: python3 -I lmdpdg_chiron.py <lmdpdg dir> <android-live.dtb> [lmdpdg options]
"""
import sys

lmdpdg_dir = sys.argv.pop(1)
sys.path.insert(0, lmdpdg_dir)

import panel  # noqa: E402

PANELS = ["/soc/qcom,mdss_dsi_jdi_fhd_nt35596s_video"]


def find(fdt):
    for path in PANELS:
        yield fdt.path_offset(path)


panel.Panel.find = staticmethod(find)
sys.argv[0] = lmdpdg_dir + "/lmdpdg.py"
exec(compile(open(sys.argv[0]).read(), sys.argv[0], "exec"))
