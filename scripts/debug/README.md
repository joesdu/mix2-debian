# 调试工具

完整用法见 [操作手册第 10 节](../../docs/操作手册.md#10-调试方法)。

在电脑上运行的：

- `make_dbg_boot.sh "<额外命令行>"`（WSL）：用 `~/mix2/out` 里的内核生成 `images/dev/boot-dbg.img`，命令行带 `loglevel=7 mix2.test panic=5`
- `bisect_boot.sh <镜像>`（Git Bash）：在内存里启动测试镜像，报告 PASS / FAIL，日志存到 `kernel/<名字>.log`
- `phone_kmsg.py <日志文件>`：从救援 initramfs 持续读取内核日志，直到连接断开
- `extract_dtb.py <boot.img> <输出.dtb>`：取出 boot.img 里附加的设备树
- `abl_strings.py <abl.img>`：解开 ABL 中 LZMA 压缩的段，搜索字符串

拷到手机 `/tmp/` 后用 sudo 运行的：

- `read_disp_regs.py`、`dump_pll.py`：显示链路寄存器（DSI、INTF、PHY、PLL）
- `labibb_regs.sh`、`labibb_regs_full.sh`、`labibb_status.sh`：PMI8998 LAB/IBB 寄存器（只读单个寄存器，**不要整块读 PMIC**）
- `touch_probe.py <event 设备> <秒数>`：记录触摸原始坐标
- `touch_reset.sh`：复位触摸芯片（会用到 `/tmp/touch_probe.py`）
- `dpms_cycle.sh <0|1>`、`dpms_first.sh`：熄屏再亮屏测试（后者会用到 `/tmp/read_disp_regs.py`）
- `bl_kick.sh`：WLED 过压中断频率
- `hwinfo.sh`、`rmem.sh`：板卡 ID、I2C、输入设备、保留内存等概况
