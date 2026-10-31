# 小米 MIX 2（chiron）上的 Debian 13

这台手机已经从 Android 换成了**原生 Debian 13（trixie）**，运行我们自己编译的主线 Linux 6.18.58 内核，不依赖任何 Android 组件。

📖 **完整的构建、刷机、使用和调试步骤见 [docs/操作手册.md](docs/操作手册.md)。**

## 硬件支持情况

| 硬件 | 状态 | 说明 |
|---|---|---|
| 显示 | ✅ | msm 显示驱动（DPU + DSI + JDI NT35596S 面板驱动），1080×2160 @ 60 Hz |
| GPU 加速 | ✅ | Adreno 540（freedreno，OpenGL 3.1 / OpenGL ES 3.1），桌面由 glamor 硬件加速 |
| 背光亮度 | ✅ | PMI8998 WLED，可在电源管理器 / 键盘亮度键调节 |
| 触摸屏 | ✅ | Synaptics S3330（in-cell），熄屏后摸屏幕即可唤醒 |
| 横屏 / 竖屏 | ✅ | 顶部面板的旋转按钮，或快捷键 Super+O（切换）、Super+方向键 |
| Wi‑Fi | ✅ | WCN3990，2.4/5 GHz，支持 WPA2/WPA3 |
| 蓝牙 | ✅ | 可连接蓝牙键盘、鼠标等 |
| 电池电量 / 充电 | ✅ | 面板显示电量，USB 充电正常 |
| 电源键、音量键、霍尔传感器 | ✅ | 电源键短按熄屏 / 亮屏，长按关机；音量键调节音量 |
| USB 联网（接电脑） | ✅ | 手机 172.16.42.1，电脑自动获得 172.16.42.2 |
| 存储 | ✅ | 根分区占满 userdata，约 52 GB |
| CPU | ⚠️ | 8 核可用，但没有调频；大核停在低频，已让调度器优先用小核 |
| 声音 | ⚠️ | 底部扬声器已能放出整首歌（ADSP → QUAT MI2S → TAS2559 功放）；目前用测试内核手动启动，听筒 / 耳机 / 录音尚不可用，见 [docs/audio-bringup.md](docs/audio-bringup.md) |
| USB 外接设备（OTG） | ⚠️ | 插上 Hub、键盘、鼠标、U 盘自动切到主机模式（手机输出 5 V），拔掉自动切回；顶部面板的 USB 按钮也可手动切换。不带充电器的 Hub 加鼠标已验证，带充电器的 Hub（边充边用）待实测 |
| 睡眠 / 待机 | ❌ | 已禁用（避免无法唤醒），不用时请关机 |
| 重力感应自动旋转、摄像头、指纹、NFC、移动网络 | ❌ | 尚未支持 |

## 日常使用

- 开机自动登录用户 **`mix2`**，默认密码 **`mix2`**。请在终端里用 `passwd` 修改。
- **Wi‑Fi / 蓝牙**：点顶部面板右侧的图标。
- **屏幕键盘**：点输入框时自动弹出，停靠在屏幕底部；中文输入法为 fcitx5（拼音）。
- **从电脑访问**：用 USB 线连接电脑后，`ssh mix2@172.16.42.1`。
- **软件更新**：`sudo apt update && sudo apt full-upgrade`。`rmtfs` 是打过补丁的版本，已被 `apt-mark hold` 锁定，请不要解除。

## 维护

### 重新编译内核并在手机上更新

电脑端（WSL Debian 中执行）：

```
bash /mnt/g/crazymix2/scripts/build_kernel.sh
bash /mnt/g/crazymix2/scripts/make_boot.sh
```

然后把结果拷到 `images/dev/`（`Image.gz`、`msm8998-xiaomi-chiron.dtb`、`initramfs.cpio.gz`、`modules.tar.gz`），在 Git Bash 里运行 `bash scripts/deploy_kernel.sh`：它会把内核和模块装到手机上，执行 `mix2-flash-boot` 写入 boot 分区（旧的备份在 `/boot/mix2/boot.img.prev`），然后重启。

> 注意：内核配置的改动如果影响结构体布局，必须同时更新模块，否则旧模块会崩溃。

想先试新内核而不写分区：手机进 FASTBOOT 后 `fastboot boot <boot.img>`，只在内存里启动一次。

### 从头重新刷机（会清空手机上的 Debian）

1. 电脑上生成镜像：`build_rootfs.sh` → `build_kernel.sh` → `rebuild_image.sh`（都在 WSL 里以 root 运行）
2. 手机同时按住 **电源 + 音量减** 进入 FASTBOOT
3. 电脑上运行：`python G:\crazymix2\scripts\flash_rootfs.py`

> **不要用 `fastboot flash userdata` 刷稀疏镜像**：MIX 2 的 bootloader 处理稀疏镜像有 bug，会悄悄写坏文件。`flash_rootfs.py` 会通过 USB 网络写入原始镜像并做完整的 sha256 校验。

### 救援模式

系统起不来时：电源 + 音量减 进 FASTBOOT，然后

```
fastboot boot G:\crazymix2\images\boot-rescue.img
```

手机会进入内存中的救援系统，电脑上 `telnet 172.16.42.1`（或用 `scripts/phone_sh.py` 发命令）即可挂载 `/dev/sda17` 修复。若 initramfs 找不到根分区，也会自动进入这个模式。

### 恢复 Android

刷机前的完整备份在 `G:\crazymix2\backup\`：

- `partitions\`：除 userdata 外全部 73 个分区（含 persist、modemst1/2、fsg 等 IMEI/校准数据）
- `userfiles-sdcard.tar`：原内部存储里的个人文件
- `firmware\`：原厂固件和设备树

恢复步骤：FASTBOOT 下执行 `fastboot flash boot backup\partitions\boot.img`，再进入 recovery 选择清除数据（userdata 现在是 Debian 的 ext4），重启即回到原来的 PixelExperience。system、vendor 等分区从未改动过。

## 让这台手机跑起来的关键修复

| 问题 | 原因 | 解决 |
|---|---|---|
| 内核启动即卡死 | defconfig 内核解压后 42 MB，和 bootloader 放 ramdisk 的位置重叠 | 只保留高通平台，内核缩到 27 MB |
| 屏幕黑屏 | 主线时钟驱动把 MMSS SMMU 的电源域切成软件控制后断电，显示控制器读不到帧缓冲 | 内核补丁让 BIMC_SMMU GDSC 常开 |
| 触摸无响应 | 触摸芯片被 bootloader 留在复位状态，主线驱动不处理复位脚 | 把复位脚建模成带 200 ms 延时的稳压器 |
| 刷进去的系统文件损坏 | bootloader 不写稀疏镜像的 FILL 块 | 改为救援系统里 dd 原始镜像并校验 |
| 蓝牙不可用 | 缺出厂 MAC；固件不支持一条 LE 命令；固件补丁与 NVM 不配套；rfkill 屏蔽 | 在 `kernel/local.dtsi` 写入出厂地址、内核补丁、改用原厂配套固件 |
| Wi‑Fi 收不到信号 | 射频电源 L25 只有 3.1 V，上游 BOB 没有余量 | L25 3.312 V、BOB 3.6 V（与原厂一致） |
| Wi‑Fi 固件反复崩溃、拖垮 modem | ① modem 诊断通道没人读，看门狗每 ~19 秒杀一次 WLAN；② 扫描到无校准的 169/173 信道断言；③ rmtfs 拒绝两个 OEM 存储请求；④ 板级数据误用开发板的 | 加入 diag-router、限制频段、给 rmtfs 打补丁、用手机自己的 bdwlan.bin |
| 电量不显示 | 主线没有 PMI8998 电量计驱动 | 引入 porthole 项目的 qcom_fg 驱动 |
| 启用 msm 显示驱动后开机卡死 | SMMU 注册时就给显示控制器挂上了空页表，bootloader 的开机画面每次读取都出错，中断风暴拖死系统 | 让 MSM8998 MDSS 使用直通域（补丁 0018） |
| msm 接管后面板命令超时 | bootloader 的显示管线仍在运行 | 设备树给 MDSS 加上复位线 `MDSS_BCR` |
| 触摸偏移、暗角、残影、闪烁 | in-cell 触摸和显示共用电极，要和 DSI 时序同步；原厂设备树写的 burst 模式和 msm 自己算的 D-PHY 时序都和 bootloader 实际用的不一样 | 读回 bootloader 的 DSI/PHY 寄存器，照抄时序、non-burst 模式和 D-PHY 参数（补丁 0021） |
| 熄屏后再也亮不起来 | ① porthole 的 VCO 公式补丁在这台手机上是错的，PLL 超出规格；② mmcc 缺少 byte_intf 分频器，字节时钟被拉低一半；③ 背光 CABC 让 WLED 不停过压；④ 面板一旦收到睡眠命令就再也不响应；⑤ 面板初始化时触摸芯片必须在运行 | 撤回该补丁、补上分频器（补丁 0022）、关闭 CABC；熄屏时不让面板睡眠、触摸芯片保持运行，亮屏时重新初始化 |
| 扬声器无声，DSP 收下音频却从不播放 | 机器驱动把 QUAT MI2S 配成“等外部时钟”，而 TAS2559 功放本身也是时钟从设备，没人产生 BCLK/WS | 改由 SoC 提供时钟（补丁 0028）；用 APR 报文跟踪定位 |
| 扬声器声音很小 | 实验功放驱动把增益固定在最低档 0 | 改为参数，按原厂用 15；系统音量走 ALSA softvol，由音量键调节 |
| 屏幕不显示、触摸和电源键熄屏失效、蓝牙反复崩溃 | 模块目录被换成另一次构建（同名 release），缺面板驱动且结构体不匹配 | 恢复与内核配套的原模块；每个测试内核使用独立的 release 名和完整模块目录 |

Wi‑Fi、电量计等多项修复参考了 [porthole-dev/pmaports](https://github.com/porthole-dev/pmaports) 中对 Pixel 2（同为骁龙 835）的详细记录。

## 许可证

本仓库中原创的部分（脚本、文档、initramfs、`overlay/`、`skel/`、`tools/` 等）以 [MIT 许可证](LICENSE) 发布。以下部分派生自其他项目，**沿用原来的许可证**：

| 路径 | 许可证 | 来源 |
|---|---|---|
| `kernel/patches/`、`kernel/patches-dropped/` | GPL-2.0（与所修改的 Linux 内核文件相同） | 对 Linux 内核的修改，部分补丁来自 [porthole-dev/pmaports](https://github.com/porthole-dev/pmaports)，保留了原作者署名 |
| `kernel/msm8998-xiaomi-chiron.dts`、`kernel/local.dtsi.example` | BSD-3-Clause | 派生自上游内核的 `msm8998-xiaomi-sagit.dts` |
| `kernel/tas2559/` | GPL-2.0 | TAS2559/TAS2560 功放驱动，移植自 LineageOS `android_kernel_xiaomi_msm8998`，保留 TI / 小米版权声明 |
| `packages/rmtfs/` | BSD-3-Clause | [linux-msm/rmtfs](https://github.com/linux-msm/rmtfs) 及我们的补丁 |
| `packages/diag/` | BSD-3-Clause | [linux-msm/diag](https://github.com/linux-msm/diag) |

仓库不包含任何手机固件。modem、ADSP、Wi‑Fi、蓝牙、GPU 等固件的版权归高通和小米所有，需要从你自己手机的 vendor 分区提取（见操作手册第 3 节）。

## 目录说明

| 路径 | 内容 |
|---|---|
| `kernel/` | MIX 2 设备树 `msm8998-xiaomi-chiron.dts`、本机 Wi‑Fi/蓝牙地址模板 `local.dtsi.example`、内核配置片段 `chiron.config`、`patches/`（`patches-dropped/` 里是试过但证明不适用的补丁及原因）、`tas2559/`（扬声器功放模块和 QUAT 设备树片段） |
| `docs/` | 操作手册、音频适配状态 `audio-bringup.md` |
| `scripts/` | 构建、打包、刷机、救援相关脚本 |
| `overlay/`、`skel/` | 拷进根文件系统的配置文件和桌面设置 |
| `packages/` | 重新打包的 `rmtfs`、`qcom-diag`（diag-router） |
| `initramfs/`、`initramfs-test/` | 正式启动 / 调试用 initramfs |
| `images/` | 生成的 boot.img、救援镜像、系统镜像 |
| `backup/` | 刷机前的 Android 完整备份 |
| `tools/` | `make_board2.py`（Wi‑Fi 板级数据）、`mix2-reboot.c` |
