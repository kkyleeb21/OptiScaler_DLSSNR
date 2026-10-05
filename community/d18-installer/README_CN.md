# DLSSNR D18 0.3.1 安装说明

运行主包中的 `D18Setup.exe`，从游戏列表或手动选择游戏 EXE。同一程序负责安装、升级和卸载；单独复制 EXE 也可运行。安装前退出游戏，自行提供兼容的 NR 文件，并查看验证等级；未验证但补丁兼容的文件需要额外确认。

`DLSSNR_D18_0.3.1_release.zip` 是精简主包。需要 D18 提供 FSR / XeSS 时勾选可选组件，选择 `DLSSNR_D18_0.3.1_optional_components.zip`；安装器会按需从发布页下载，也可指定本地 ZIP。包内不含 NVIDIA 运行库。

升级保留现有配置，之前通过 D18 使用 FSR / XeSS 的用户需要勾选可选组件。全新安装 NR 默认关闭，其他既有新装默认值保留；缺少新键的旧配置会使用核心默认值。

Insert 打开游戏内菜单；REFramework 加载路线的菜单键按安装选项设置。状态和诊断在独立窗口，“高级 → 曝光与 HDR 输入”提供 DX12 实验性白点估算；默认仅 007 First Light 开启。

安装器未签名，Windows 可能提示来源未知。鬼武者刚进游戏就打开菜单时可能无法拖动滑块且鼠标转动视角，关闭并重开菜单即可。

自动化入口：`D18Setup.exe --offline --data-root <工作目录> --request <请求JSON> --result <结果JSON>`。支持 Check / Install / Uninstall / ValidateNr / Discover / Meta / AuditDependencies；发现测试须在请求中用 scanRoots 指定假目录。退出码 0 成功、1 失败、2 参数错误。卸载只需 game 目录，不要求 EXE 存在。

底层 CLI 保留：`Install-D18.ps1 -GameDir <目录> -RuntimePath <NR> -NativeApi None|DX11|Vulkan -ProxyName dxgi.dll -REFramework Manual -Yes`；`Uninstall-D18.ps1 -GameDir <目录> -Yes`。

请阅读 RELEASE_NOTES_CN.md、RUNTIME_COMPATIBILITY.md、LICENSE 与 THIRD_PARTY_NOTICES.md。文件版本 0.3.1.0；文件校验不代表游戏兼容或画质验收。
