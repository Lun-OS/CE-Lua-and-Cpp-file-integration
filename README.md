# CE-Lua与C++集成工具（ImGui界面版）

## 版本特性（v3.1 修复版 + ImGui增强）

- 修复多次读取结果缓存冲突问题
- 增强原子文件操作的稳定性
- 新增带重试的读取接口
- 优化数字解析算法，支持更多格式
- 完善异常处理机制，提升崩溃恢复能力
- **新增ImGui可视化界面**：提供直观的内存操作交互面板
- **实时数据监控**：支持内存值变化实时图表展示
- **操作历史记录**：通过ImGui窗口记录并回溯所有内存读写操作
- **多标签页设计**：分离内存操作、指针解析、模块信息等功能区域

通过上述设计，C++端与Lua端形成高效协同，在保证通信可靠性的同时，将单次命令响应延迟稳定在30ms左右，结合ImGui的即时模式UI特性，满足实时内存调试和游戏辅助开发的性能与交互需求。


## 项目简介

本项目提供了一套完整的跨语言通信方案，通过文件系统实现C++程序与CE Lua脚本的高效交互。C++端通过`CEBridge`类库发送命令，CE端通过Lua脚本处理命令并返回结果，**新增ImGui图形界面**，支持内存读写、指针解析、模块信息获取等功能的可视化操作，适用于游戏辅助开发、内存调试等场景。


## 功能特点

- 支持内存读写（整数、浮点数、字节等类型）
- 指针多级偏移解析（单级/多级指针）
- 模块基址获取与模块偏移计算
- 断点设置与移除
- 寄存器值读取（断点状态下）
- **ImGui界面特性**：
  - 响应式布局，适配不同窗口尺寸
  - 操作参数实时校验与提示
  - 结果数据格式化显示（十进制/十六进制切换）
  - 可自定义界面主题与布局
- 高性能优化：
  - 字符串池减少重复内存分配
  - 数字解析缓存加速地址计算
  - 批量命令处理与结果写入
  - 日志缓冲与定时刷新
  - ImGui渲染与通信逻辑异步处理，避免界面卡顿


## 环境要求

- 操作系统：Windows
- 编译环境：支持C++17的编译器（如Visual Studio 2019+）
- Cheat Engine：7.0+（需支持Lua扩展）
- 运行时：.NET Framework 4.0+（CE依赖）
- **ImGui依赖**：
  - Dear ImGui v1.89+
  - 窗口后端：GLFW 3.3+ 或 Win32 API
  - 渲染后端：DirectX 11/12 或 OpenGL 3.3+


## 安装步骤

1. **编译C++客户端（含ImGui）**
   ```bash
   # 克隆仓库（包含ImGui子模块）
   git clone --recursive https://github.com/Lun_OS/CE-Lua-and-Cpp-file-integration.git
   cd CE-Lua-and-Cpp-file-integration
   
   # 初始化ImGui子模块（若未自动拉取）
   git submodule update --init --recursive
   
   # 使用Visual Studio打开项目，启用ImGui编译选项
   # 或使用CMake
   mkdir build && cd build
   cmake -DENABLE_IMGUI=ON ..
   make
   ```

2. **部署CE Lua脚本**
   - 打开Cheat Engine，加载`ce接口.LUA`脚本
   - 脚本加载后会显示初始化信息，提示使用`QAQ()`启动服务


## 使用方法

### 1. 启动服务与界面

- **CE端**：在CE的Lua控制台执行
  ```lua
  QAQ()  -- 启动桥接服务
  ```

- **C++端（ImGui界面）**：初始化并启动图形界面
  ```cpp
  #include "CEBridge.h"
  #include "ImGuiUI.h"  // 新增ImGui界面头文件
  
  int main() {
      // 配置桥接参数
      CEBridge::BridgeConfig config;
      config.verbose = true;  // 启用详细日志
      
      // 创建客户端实例
      CEBridge::Client bridge(config);
      
      // 初始化连接
      if (!bridge.initialize()) {
          std::cerr << "初始化失败: " << bridge.getLastError() << std::endl;
          return 1;
      }
      
      // 初始化并启动ImGui界面
      ImGuiUI ui(&bridge);  // 传入桥接实例
      ui.run();  // 启动UI主循环
      
      return 0;
  }
  ```

### 2. ImGui界面操作示例

- **内存读取**：在"内存操作"标签页输入地址（支持`game.exe+0x1234`格式），点击"读取"按钮
- **指针解析**：在"指针解析"标签页输入基地址与多级偏移，自动计算最终地址并读取值
- **模块信息**：在"模块管理"标签页选择目标进程模块，自动显示基址与大小
- **历史记录**：所有操作结果自动记录在"操作日志"标签页，支持筛选与复制


# C++端实现细节与优化机制

## 核心类与组件设计

CEBridge C++端通过`Client`类提供统一接口，配合多个优化组件实现高效的跨进程通信，**新增ImGuiUI组件**负责图形界面渲染，核心结构如下：

### 1. 配置管理（`BridgeConfig`）
与Lua端`CONFIG`结构对应，提供通信参数的统一配置：
```cpp
// 默认配置初始化（含ImGui相关）
BridgeConfig config;
config.basePath = "%LOCALAPPDATA%\\Temp\\QAQ\\";  // 通信文件目录
config.pollMs = 50;                               // 活跃轮询间隔
config.idleMs = 1000;                             // 空闲轮询间隔
config.defaultTimeout = 2000;                     // 默认超时时间
config.uiRefreshMs = 16;                          // ImGui界面刷新间隔（~60FPS）
```
支持自定义路径、轮询频率、缓存大小等关键参数，确保与Lua端配置协同工作。

### 2. 核心通信接口（`Client`类）
提供简洁的API封装底层文件操作，主要接口包括：
- `readMemory()`：读取指定地址内存（支持模块+偏移格式）
- `writeMemory()`：写入内存数据（支持多数据类型）
- `readPointer()`：解析多级指针链
- `getModuleBase()`：获取模块基址
- `executeCommands()`：批量执行命令（减少IO次数）

**示例：带重试机制的读取接口**
```cpp
// 自动重试失败的内存读取（最多3次）
CEBridge::CommandResult result;
if (bridge.readMemoryWithRetry("game.exe+0x00A1234", result, 3)) {
    std::cout << "读取结果: " << result.value << std::endl;
}
```

### 3. 性能优化组件
#### （1）字符串池（`StringPool`）

- **作用**：复用高频字符串（如地址、模块名），减少内存分配与释放开销
- **实现**：通过`std::unordered_map`缓存字符串，限制最大容量（默认1000）
- **效果**：降低字符串处理的时间复杂度，尤其在批量命令场景下提升明显

```cpp
// 字符串复用示例
std::string_view addr = stringPool_.intern("game.exe+0x1234");
```

#### （2）ImGui UI管理器（`ImGuiUI`）
- **作用**：封装ImGui初始化、渲染与事件处理，与通信逻辑解耦
- **实现**：
  - 独立线程处理UI渲染，避免阻塞通信轮询
  - 采用双缓冲区存储操作结果，减少UI与通信的锁竞争
  - 预编译常用UI组件（按钮、输入框），提升渲染效率
- **效果**：界面响应流畅，操作延迟<100ms

### 4. 通信可靠性保障
#### （2）动态轮询策略
- 活跃状态（有命令时）：使用`pollMs=50ms`高频检测
- 空闲状态（无命令时）：自动切换为`idleMs=1000ms`降低CPU占用
- 基于文件修改时间（MTime）的变更检测，减少无效读取


## 与Lua端的协同优化

C++端与Lua端通过以下机制协同实现30ms级通信延迟，同时保障ImGui界面流畅性：

| 优化方向       | C++端实现                          | Lua端实现                          |
|----------------|-----------------------------------|-----------------------------------|
| **缓存策略**   | 字符串池、数字解析缓存             | 模块基址缓存、结果缓存             |
| **批量处理**   | `executeCommands()`批量发送命令    | 一次性解析所有命令行               |
| **IO优化**     | 日志缓冲、原子写入                 | 结果缓冲、批量写入                 |
| **轮询策略**   | 动态调整轮询间隔                   | 基于空闲计数的间隔调整             |
| **UI协同**     | ImGui渲染与通信逻辑异步化          | 结果批量推送，减少UI刷新频率       |


## 错误处理与调试

- **错误跟踪**：通过`getLastError()`获取详细错误信息
- **日志级别**：`verbose`模式启用详细日志，包含通信过程关键节点
- **状态检查**：`isReady()`方法判断桥接是否初始化完成
- **ImGui调试**：`ui.showDebugWindow(true)`启用调试窗口，显示通信耗时与缓存状态

```cpp
if (!bridge.initialize()) {
    std::cerr << "初始化失败: " << bridge.getLastError() << std::endl;
}

// 启用ImGui调试窗口
ImGuiUI ui(&bridge);
ui.showDebugWindow(true);
ui.run();
```

### 5. 核心功能示例
#### 读取内存

```cpp
CEBridge::CommandResult result;
// 支持直接地址(0x123456)或模块+偏移(game.exe+0x1234)
if (bridge.readMemory("game.exe+0x00A1234", result)) {
    std::cout << "读取结果: " << result.value << std::endl;
}
```

#### 写入内存
```cpp
CEBridge::CommandResult result;
if (bridge.writeMemory("0x00A1234", "100", result)) {
    std::cout << "写入成功" << std::endl;
}
```

#### 获取模块基址
```cpp
CEBridge::CommandResult result;
if (bridge.getModuleBase("game.exe", result)) {
    std::cout << "模块基址: " << result.value << std::endl;
}
```


## 注意事项

1. **法律风险**：本工具仅用于合法的调试和学习目的，请勿用于未经授权的游戏修改或其他非法活动。
2. **兼容性**：
   - 需匹配目标进程的位数（32位/64位）
   - CE版本需支持Lua扩展API（如`readInteger`、`debug_setBreakpoint`等）
   - ImGui后端需与系统图形接口匹配（如Win10推荐DirectX 11）
3. **性能优化**：
   - 高频操作建议使用批量命令处理，减少文件IO开销
   - 复杂UI布局建议关闭实时刷新，使用手动刷新按钮


## 作者信息

- 作者：Lun.
- GitHub：[Lun-OS](https://github.com/Lun-OS)
- 联系方式：QQ 1596534228


## 许可证

[MIT](LICENSE)
