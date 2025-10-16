## 版本特性（v3.1 修复版）

- 修复多次读取结果缓存冲突问题
- 增强原子文件操作的稳定性
- 新增带重试的读取接口
- 优化数字解析算法，支持更多格式
- 完善异常处理机制，提升崩溃恢复能力

通过上述设计，C++端与Lua端形成高效协同，在保证通信可靠性的同时，将单次命令响应延迟稳定在30ms左右，满足实时内存调试和游戏辅助开发的性能需求。


# CE-Lua与C++集成工具

一个高性能的Cheat Engine (CE) Lua脚本与C++程序通信桥接工具，支持内存读写、指针解析、模块信息获取等功能，适用于游戏辅助开发、内存调试等场景。

## 项目简介

本项目提供了一套完整的跨语言通信方案，通过文件系统实现C++程序与CE Lua脚本的高效交互。C++端通过`CEBridge`类库发送命令，CE端通过Lua脚本处理命令并返回结果，支持多种内存操作场景，具备缓存优化、批量处理等特性。

## 功能特点

- 支持内存读写（整数、浮点数、字节等类型）
- 指针多级偏移解析（单级/多级指针）
- 模块基址获取与模块偏移计算
- 断点设置与移除
- 寄存器值读取（断点状态下）
- 高性能优化：
  - 字符串池减少重复内存分配
  - 数字解析缓存加速地址计算
  - 批量命令处理与结果写入
  - 日志缓冲与定时刷新

## 环境要求

- 操作系统：Windows
- 编译环境：支持C++17的编译器（如Visual Studio 2019+）
- Cheat Engine：7.0+（需支持Lua扩展）
- 运行时：.NET Framework 4.0+（CE依赖）

## 安装步骤

1. **编译C++客户端**
   ```bash
   # 克隆仓库
   git clone https://github.com/Lun_OS/CE-Lua-and-Cpp-file-integration.git
   cd CE-Lua-and-Cpp-file-integration
   
   # 使用Visual Studio打开项目，编译生成可执行文件
   # 或使用CMake（如配置了CMakeLists.txt）
   mkdir build && cd build
   cmake ..
   make
   ```

2. **部署CE Lua脚本**
   - 打开Cheat Engine，加载`ce接口.LUA`脚本
   - 脚本加载后会显示初始化信息，提示使用`QAQ()`启动服务

## 使用方法

### 1. 启动服务

- **CE端**：在CE的Lua控制台执行
  ```lua
  QAQ()  -- 启动桥接服务
  ```

- **C++端**：初始化桥接客户端
  ```cpp
  #include "CEBridge.h"
  
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
      
      // 后续操作...
  }
  ```

# C++端实现细节与优化机制

## 核心类与组件设计

CEBridge C++端通过`Client`类提供统一接口，配合多个优化组件实现高效的跨进程通信，核心结构如下：

### 1. 配置管理（`BridgeConfig`）
与Lua端`CONFIG`结构对应，提供通信参数的统一配置：
```cpp
// 默认配置初始化
BridgeConfig config;
config.basePath = "%LOCALAPPDATA%\\Temp\\QAQ\\";  // 通信文件目录
config.pollMs = 50;                               // 活跃轮询间隔
config.idleMs = 1000;                             // 空闲轮询间隔
config.defaultTimeout = 2000;                     // 默认超时时间
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


#### （2）数字解析缓存（`NumberCache`）
- **作用**：缓存地址、偏移量的解析结果（支持十进制/十六进制）
- **优化点**：
  - 优先从缓存获取，避免重复调用`stoll`
  - 自动处理正负号和进制转换
  - 限制缓存大小防止内存溢出

```cpp
// 快速解析数字（带缓存）
auto value = numberCache_.parse("0x1234");  // 首次解析后缓存结果
```


#### （3）日志缓冲区（`LogBuffer`）
- **作用**：批量写入日志，减少磁盘IO次数
- **机制**：
  - 内存缓冲日志条目，达到阈值（100条）或定时（10秒）自动刷新
  - 线程安全设计，支持自定义日志回调

```cpp
// 日志回调示例
bridge.setLogCallback([](const std::string& level, const std::string& msg) {
    std::cout << "[" << level << "] " << msg << std::endl;
});
```


### 4. 通信可靠性保障

#### （1）原子文件操作
通过临时文件+重命名实现原子写入，避免C++与Lua端读写冲突：
```cpp
// 原子写入命令文件
bool atomicWriteFile(const std::string& path, const std::string& content) {
    std::string tmpPath = path + ".tmp";
    // 先写临时文件...
    fs::rename(tmpPath, path);  // 原子操作
}
```


#### （2）动态轮询策略
- 活跃状态（有命令时）：使用`pollMs=50ms`高频检测
- 空闲状态（无命令时）：自动切换为`idleMs=1000ms`降低CPU占用
- 基于文件修改时间（MTime）的变更检测，减少无效读取


#### （3）超时与重试机制
- 命令执行默认超时2秒，支持自定义超时时间
- `readMemoryWithRetry()`接口提供自动重试功能，应对临时通信失败


## 与Lua端的协同优化

C++端与Lua端通过以下机制协同实现30ms级通信延迟：

| 优化方向       | C++端实现                          | Lua端实现                          |
|----------------|-----------------------------------|-----------------------------------|
| **缓存策略**   | 字符串池、数字解析缓存             | 模块基址缓存、结果缓存             |
| **批量处理**   | `executeCommands()`批量发送命令    | 一次性解析所有命令行               |
| **IO优化**     | 日志缓冲、原子写入                 | 结果缓冲、批量写入                 |
| **轮询策略**   | 动态调整轮询间隔                   | 基于空闲计数的间隔调整             |


## 错误处理与调试

- **错误跟踪**：通过`getLastError()`获取详细错误信息
- **日志级别**：`verbose`模式启用详细日志，包含通信过程关键节点
- **状态检查**：`isReady()`方法判断桥接是否初始化完成

```cpp
if (!bridge.initialize()) {
    std::cerr << "初始化失败: " << bridge.getLastError() << std::endl;
}
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

#### 解析多级指针
```cpp
CEBridge::CommandResult result;
// 解析指针链：base + 0x18 + 0x20 + 0x8
std::vector<std::string> offsets = {"0x18", "0x20", "0x8"};
if (bridge.readPointer("game.exe+0x00A0000", offsets, result)) {
    std::cout << "指针最终值: " << result.value << std::endl;
}
```

#### 获取模块基址
```cpp
CEBridge::CommandResult result;
if (bridge.getModuleBase("game.exe", result)) {
    std::cout << "模块基址: " << result.value << std::endl;
}
```

### 3. 停止服务

- **CE端**：
  ```lua
  stopQAQ()  -- 停止桥接服务
  ```

- **C++端**：
  ```cpp
  bridge.cleanup();  // 清理资源
  ```

## 配置说明

可通过`BridgeConfig`（C++）和`CONFIG`（Lua）调整参数：
- `basePath`：通信文件存放路径（默认`%LOCALAPPDATA%\Temp\QAQ\`）
- `pollMs`：命令轮询间隔（默认50ms）
- `idleMs`：空闲状态等待间隔（默认1000ms）
- `maxCacheSize`：结果缓存最大数量（默认100）
- `verbose`/`enableLogging`：是否启用详细日志

## 注意事项

1. **法律风险**：本工具仅用于合法的调试和学习目的，请勿用于未经授权的游戏修改或其他非法活动。
2. **兼容性**：
   - 需匹配目标进程的位数（32位/64位）
   - CE版本需支持Lua扩展API（如`readInteger`、`debug_setBreakpoint`等）
3. **性能优化**：高频操作建议使用批量命令处理，减少文件IO开销。

## 作者信息

- 作者：Lun.
- GitHub：[Lun-OS](https://github.com/Lun-OS)
- 联系方式：QQ 1596534228

## 许可证

[MIT](LICENSE)
