#include "CEBridge.h"
#include <iostream>
#include <iomanip>
#include <limits>

void printSeparator() {
    std::cout << "\n========================================\n";
}

void printResult(const CEBridge::CommandResult& result) {
    std::cout << "地址: " << result.address << std::endl;
    std::cout << "值: " << result.value << std::endl;
    std::cout << "状态: " << result.status << std::endl;
    if (!result.message.empty()) {
        std::cout << "消息: " << result.message << std::endl;
    }
}

int main() {
    printSeparator();
    std::cout << "   CE Lua 桥接客户端 v2.0（增强版）" << std::endl;
    printSeparator();

    // 创建配置
    CEBridge::BridgeConfig config;
    config.verbose = true;

    // 创建客户端
    CEBridge::Client bridge(config);

    // 设置日志回调
    bridge.setLogCallback([](const std::string& level, const std::string& message) {
        std::cout << "[" << level << "] " << message << std::endl;
        });

    // 初始化
    if (!bridge.initialize()) {
        std::cerr << "初始化失败: " << bridge.getLastError() << std::endl;
        std::cout << "\n按回车键退出...";
        std::cin.get();
        return 1;
    }

    std::cout << "\n重要提示:" << std::endl;
    std::cout << "1. 请确保已在 CE 中加载增强版 Lua 脚本" << std::endl;
    std::cout << "2. 在 CE Lua 控制台执行: QAQ()" << std::endl;
    std::cout << "3. 确保目标进程已附加到 CE" << std::endl;
    std::cout << "\n按回车键继续..." << std::endl;
    std::cin.get();

    // 主菜单循环
    while (true) {
        printSeparator();
        std::cout << "请选择操作:" << std::endl;
        std::cout << "1. 读取内存地址" << std::endl;
        std::cout << "2. 写入内存地址" << std::endl;
        std::cout << "3. 单层指针读取" << std::endl;
        std::cout << "4. 多层指针读取" << std::endl;
        std::cout << "5. 获取模块基址" << std::endl;
        std::cout << "6. 读取模块偏移" << std::endl;
        std::cout << "7. 写入模块偏移" << std::endl;
        std::cout << "8. 设置断点" << std::endl;
        std::cout << "9. 移除断点" << std::endl;
        std::cout << "10. 获取寄存器值" << std::endl;
        std::cout << "11. 综合测试（模块+多层指针）" << std::endl;
        std::cout << "0. 退出程序" << std::endl;
        printSeparator();
        std::cout << "请输入选项: ";

        int choice;
        if (!(std::cin >> choice)) {
            std::cin.clear();
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            std::cout << "输入无效，请重试\n";
            continue;
        }
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

        if (choice == 0) {
            std::cout << "正在退出..." << std::endl;
            break;
        }

        CEBridge::CommandResult result;

        switch (choice) {
        case 1: { // 读取内存（支持模块名+偏移）
            std::cout << "\n输入地址（支持格式：0x1234 或 game.exe+0x1234）: ";
            std::string addr;
            std::getline(std::cin, addr);

            if (bridge.readMemory(addr, result)) {
                printSeparator();
                std::cout << "读取成功:" << std::endl;
                printResult(result);
            }
            else {
                std::cout << "读取失败: " << bridge.getLastError() << std::endl;
            }
            break;
        }

        case 2: { // 写入内存
            std::cout << "\n输入地址（支持格式：0x1234 或 game.exe+0x1234）: ";
            std::string addr;
            std::getline(std::cin, addr);

            std::cout << "输入要写入的数值: ";
            std::string value;
            std::getline(std::cin, value);

            if (bridge.writeMemory(addr, value, result)) {
                printSeparator();
                std::cout << "写入成功:" << std::endl;
                printResult(result);
            }
            else {
                std::cout << "写入失败: " << bridge.getLastError() << std::endl;
            }
            break;
        }

        case 3: { // 单层指针
            std::cout << "\n输入基址（十六进制，如 10A3F200 或 0x10A3F200）: ";
            std::string baseStr;
            std::getline(std::cin, baseStr);

            std::cout << "输入偏移（十六进制，如 18 或 0x18）: ";
            std::string offsetStr;
            std::getline(std::cin, offsetStr);

            std::vector<std::string> offsets = { offsetStr };
            if (bridge.readPointer(baseStr, offsets, result)) {
                printSeparator();
                std::cout << "指针读取成功:" << std::endl;
                printResult(result);
            }
            else {
                std::cout << "读取失败: " << bridge.getLastError() << std::endl;
            }
            break;
        }

        case 4: { // 多层指针
            std::cout << "\n输入基址（支持模块名，如 game.exe+0x1234 或 0x10A3F200）: ";
            std::string baseStr;
            std::getline(std::cin, baseStr);

            std::cout << "输入偏移层数: ";
            int layers;
            if (!(std::cin >> layers)) {
                std::cin.clear();
                std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                std::cout << "层数输入无效\n";
                break;
            }
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

            std::vector<std::string> offsets;
            for (int i = 0; i < layers; ++i) {
                std::cout << "输入第" << (i + 1) << "层偏移（十六进制）: 0x";
                std::string offsetStr;
                std::cin >> offsetStr;
                offsets.push_back(offsetStr);
            }
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

            if (bridge.readPointer(baseStr, offsets, result)) {
                printSeparator();
                std::cout << "多层指针读取成功:" << std::endl;
                printResult(result);
            }
            else {
                std::cout << "读取失败: " << bridge.getLastError() << std::endl;
            }
            break;
        }

        case 5: { // 获取模块基址
            std::cout << "\n输入模块名（如 game.exe）: ";
            std::string moduleName;
            std::getline(std::cin, moduleName);

            if (bridge.getModuleBase(moduleName, result)) {
                printSeparator();
                std::cout << "模块基址获取成功:" << std::endl;
                std::cout << moduleName << " 基址: " << result.value << std::endl;
            }
            else {
                std::cout << "获取失败: " << bridge.getLastError() << std::endl;
            }
            break;
        }

        case 6: { // 读取模块偏移
            std::cout << "\n输入模块名（如 game.exe）: ";
            std::string moduleName;
            std::getline(std::cin, moduleName);

            std::cout << "输入偏移（十六进制）: 0x";
            std::string offsetStr;
            std::cin >> offsetStr;
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

            std::string address = moduleName + "+0x" + offsetStr;
            
            if (bridge.readMemory(address, result)) {
                printSeparator();
                std::cout << "读取成功:" << std::endl;
                printResult(result);
            }
            else {
                std::cout << "读取失败: " << bridge.getLastError() << std::endl;
            }
            break;
        }

        case 7: { // 写入模块偏移
            std::cout << "\n输入模块名（如 game.exe）: ";
            std::string moduleName;
            std::getline(std::cin, moduleName);

            std::cout << "输入偏移（十六进制）: 0x";
            std::string offsetStr;
            std::cin >> offsetStr;

            std::cout << "输入要写入的数值: ";
            std::string value;
            std::cin >> value;
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

            std::string address = moduleName + "+0x" + offsetStr;

            if (bridge.writeMemory(address, value, result)) {
                printSeparator();
                std::cout << "写入成功:" << std::endl;
                printResult(result);
            }
            else {
                std::cout << "写入失败: " << bridge.getLastError() << std::endl;
            }
            break;
        }

        case 8: // 设置断点
        case 9: // 移除断点
        case 10: { // 获取寄存器值
            std::cout << "\n此功能在新版本中暂不支持" << std::endl;
            break;
        }

        case 11: { // 综合测试
            std::cout << "\n=== 综合测试：模块基址 + 多层指针 ===" << std::endl;
            std::cout << "示例：读取 [[game.exe+0x12AB40]+0x18]+0x20 的值\n" << std::endl;

            std::cout << "输入模块名: ";
            std::string moduleName;
            std::getline(std::cin, moduleName);

            std::cout << "输入模块偏移（十六进制）: 0x";
            std::string moduleOffsetStr;
            std::cin >> moduleOffsetStr;
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

            std::string baseStr = moduleName + "+0x" + moduleOffsetStr;

            std::vector<std::string> offsets;
            std::cout << "输入偏移层数: ";
            int layers;
            if (!(std::cin >> layers)) {
                std::cin.clear();
                std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                std::cout << "层数输入无效\n";
                break;
            }
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

            for (int i = 0; i < layers; ++i) {
                std::cout << "第" << (i + 1) << "层偏移（十六进制）: 0x";
                std::string offsetStr;
                std::cin >> offsetStr;
                offsets.push_back(offsetStr);
            }
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

            std::cout << "\n正在执行多层指针读取..." << std::endl;
            if (bridge.readPointer(baseStr, offsets, result)) {
                printSeparator();
                std::cout << "综合测试成功!" << std::endl;
                std::cout << "完整路径: [[" << baseStr << "]";
                for (const auto& offset : offsets) {
                    std::cout << "+0x" << offset;
                }
                std::cout << "]" << std::endl;
                printResult(result);
            }
            else {
                std::cout << "测试失败: " << bridge.getLastError() << std::endl;
            }
            break;
        }

        default:
            std::cout << "无效选项，请重新选择" << std::endl;
            break;
        }

        std::cout << "\n按回车键继续...";
        std::cin.get();
    }

    // 停止桥接服务
    std::cout << "\n是否停止 Lua 桥接服务？(y/n): ";
    char stopChoice;
    std::cin >> stopChoice;

    if (stopChoice == 'y' || stopChoice == 'Y') {
        if (bridge.sendStopSignal()) {
            std::cout << "停止信号已发送" << std::endl;
        }
        else {
            std::cout << "发送停止信号失败" << std::endl;
        }
    }

    std::cout << "\n程序已退出" << std::endl;
    return 0;
}

// 法律风险由自己承担
// 作者：Lun. github:Lun-OS  QQ:1596534228
