#pragma once
// ============================================================
// CEBridge.h - CE Lua 文件通讯桥接库（增强版）
// 版本: 2.0
// 新增：模块基址查询、多层指针、断点与寄存器读取
// 作者：Lun.  QQ:1596534228   github:Lun-OS
// 此脚本仅供学习，请勿调试于未授权程序，风险由自己承担
// ============================================================

#ifndef CE_BRIDGE_H
#define CE_BRIDGE_H

#include <string>
#include <vector>
#include <map>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <chrono>
#include <thread>
#include <functional>
#include <mutex>

namespace CEBridge {

    namespace fs = std::filesystem;

    // ==================== 结果结构体 ====================
    struct CommandResult {
        std::string address;      // 地址（字符串形式）
        std::string value;        // 值（字符串形式）
        std::string status;       // 状态: "OK" 或 "ERR"
        std::string message;      // 附加消息

        bool isSuccess() const { return status == "OK"; }

        int getIntValue() const {
            try { return std::stoi(value); }
            catch (...) { return 0; }
        }

        uint64_t getAddressValue() const {
            try { return std::stoull(value, nullptr, 16); }
            catch (...) { return 0; }
        }

        float getFloatValue() const {
            try { return std::stof(value); }
            catch (...) { return 0.0f; }
        }
    };

    // ==================== 配置结构体 ====================
    struct BridgeConfig {
        std::string basePath = []() {
            char* localAppData = nullptr;
            size_t len = 0;
            errno_t err = _dupenv_s(&localAppData, &len, "LOCALAPPDATA");

            std::string path;
            if (err == 0 && localAppData != nullptr) {
                path = std::string(localAppData) + "\\Temp\\QAQ\\";
                free(localAppData);
            }
            else {
                path = ".\\Temp\\QAQ\\";
            }

            std::replace(path.begin(), path.end(), '\\', '/');
            return path;
            }();

        int defaultTimeout = 1000;
        int retryCount = 5;
        int retryDelay = 500;
        bool autoCleanup = true;
        bool verboseLogging = false;
    };

    // ==================== 日志回调函数类型 ====================
    using LogCallback = std::function<void(const std::string& level, const std::string& message)>;

    // ==================== CEBridge 主类 ====================
    class Client {
    public:
        explicit Client(const BridgeConfig& config = BridgeConfig());
        ~Client();

        Client(const Client&) = delete;
        Client& operator=(const Client&) = delete;

        // -------------------- 初始化与清理 --------------------
        bool initialize();
        void cleanup();
        bool isReady() const { return m_initialized; }

        // -------------------- 基础内存操作 --------------------

        // 读取内存（支持模块名+偏移格式，如 "game.exe+0x1234"）
        bool readMemory(const std::string& address, CommandResult& result, int timeout = -1);
        bool readMemory(uint64_t address, CommandResult& result, int timeout = -1);

        // 写入内存（支持模块名+偏移格式）
        bool writeMemory(const std::string& address, int value, CommandResult& result, int timeout = -1);
        bool writeMemory(uint64_t address, int value, CommandResult& result, int timeout = -1);

        // -------------------- 指针操作 --------------------

        // 单层指针：readPointer(base, offset)
        bool readPointer(uint64_t base, uint64_t offset, CommandResult& result, int timeout = -1);

        // 多层指针：readPointerChain(base, {offset1, offset2, offset3, ...})
        bool readPointerChain(uint64_t base, const std::vector<uint64_t>& offsets,
            CommandResult& result, int timeout = -1);

        // 多层指针（支持模块名）：readPointerChain("game.exe+0x1234", {0x18, 0x20, 0x10})
        bool readPointerChain(const std::string& base, const std::vector<uint64_t>& offsets,
            CommandResult& result, int timeout = -1);

        // -------------------- 模块操作 --------------------

        // 获取模块基址
        bool getModuleBase(const std::string& moduleName, uint64_t& baseAddress, int timeout = -1);

        // 从模块偏移读取内存
        bool readModuleOffset(const std::string& moduleName, uint64_t offset,
            CommandResult& result, int timeout = -1);

        // 向模块偏移写入内存
        bool writeModuleOffset(const std::string& moduleName, uint64_t offset, int value,
            CommandResult& result, int timeout = -1);

        // -------------------- 断点与寄存器 --------------------

        // 设置断点（支持模块名+偏移）
        bool setBreakpoint(const std::string& address, int timeout = -1);
        bool setBreakpoint(uint64_t address, int timeout = -1);

        // 移除断点
        bool removeBreakpoint(const std::string& address, int timeout = -1);
        bool removeBreakpoint(uint64_t address, int timeout = -1);

        // 获取所有寄存器值
        bool getRegisters(std::map<std::string, uint64_t>& registers, int timeout = -1);

        // 获取单个寄存器值
        bool getRegister(const std::string& regName, uint64_t& value, int timeout = -1);

        // -------------------- 批量操作 --------------------

        // 批量执行命令
        bool executeCommands(const std::vector<std::string>& commands,
            std::map<std::string, CommandResult>& results,
            int timeout = -1);

        // 发送自定义命令
        bool sendCustomCommand(const std::string& command, int timeout = -1);

        // -------------------- 高级功能 --------------------

        // 等待内存值变化
        bool waitForValueChange(uint64_t address, int currentValue,
            CommandResult& result, int timeout = -1, int pollInterval = 100);

        // 批量读取连续地址
        bool readMemoryRange(uint64_t startAddress, int count, int step,
            std::vector<CommandResult>& results, int timeout = -1);

        // -------------------- 控制操作 --------------------

        bool sendStopSignal();
        bool waitForLuaStop(int timeout = 5000);

        // -------------------- 实用工具 --------------------

        void setLogCallback(LogCallback callback) { m_logCallback = callback; }
        const BridgeConfig& getConfig() const { return m_config; }
        void updateConfig(const BridgeConfig& config) { m_config = config; }
        std::string getLastError() const { return m_lastError; }
        bool isLuaRunning(int timeout = 2000);
        std::vector<std::string> readLuaLog(int lastNLines = 10);

        // -------------------- 静态命令生成器 --------------------

        static std::string makeReadCommand(const std::string& address);
        static std::string makeReadCommand(uint64_t address);

        static std::string makeWriteCommand(const std::string& address, int value);
        static std::string makeWriteCommand(uint64_t address, int value);

        static std::string makePointerCommand(uint64_t base, uint64_t offset);
        static std::string makePointerChainCommand(uint64_t base, const std::vector<uint64_t>& offsets);
        static std::string makePointerChainCommand(const std::string& base, const std::vector<uint64_t>& offsets);

        static std::string makeModuleCommand(const std::string& moduleName);
        static std::string makeBreakpointCommand(const std::string& address);
        static std::string makeRemoveBreakpointCommand(const std::string& address);
        static std::string makeGetRegistersCommand();

        static std::string formatAddress(uint64_t address);
        static uint64_t parseAddress(const std::string& addressStr);

    private:
        BridgeConfig m_config;

        std::string m_commandFile;
        std::string m_resultFile;
        std::string m_logFile;
        std::string m_stopFlag;

        bool m_initialized;
        fs::file_time_type m_lastResultMTime;
        std::string m_lastError;

        LogCallback m_logCallback;
        std::mutex m_mutex;

        bool atomicWriteCommand(const std::string& content);
        bool waitForResult(int timeout);
        bool parseResultFile(std::map<std::string, CommandResult>& results);
        void log(const std::string& level, const std::string& message);
        void setError(const std::string& error);
        std::string getFullPath(const std::string& filename) const;
        bool fileExists(const std::string& filepath) const;
        bool removeFile(const std::string& filepath);
    };

    // ==================== 实现部分 ====================

    inline Client::Client(const BridgeConfig& config)
        : m_config(config)
        , m_commandFile(config.basePath + "command.txt")
        , m_resultFile(config.basePath + "result.txt")
        , m_logFile(config.basePath + "bridge_log.txt")
        , m_stopFlag(config.basePath + "stop.flag")
        , m_initialized(false)
        , m_lastResultMTime{}
        , m_logCallback(nullptr)
    {
    }

    inline Client::~Client() {
        if (m_config.autoCleanup) {
            cleanup();
        }
    }

    inline bool Client::initialize() {
        std::lock_guard<std::mutex> lock(m_mutex);

        try {
            if (!fs::exists(m_config.basePath)) {
                fs::create_directories(m_config.basePath);
                log("INFO", "已创建目录: " + m_config.basePath);
            }

            removeFile(m_commandFile);
            removeFile(m_resultFile);
            removeFile(m_stopFlag);

            m_lastResultMTime = fs::file_time_type::min();

            m_initialized = true;
            log("INFO", "桥接初始化成功");
            return true;
        }
        catch (const std::exception& e) {
            setError(std::string("初始化失败: ") + e.what());
            log("ERROR", m_lastError);
            return false;
        }
    }

    inline void Client::cleanup() {
        std::lock_guard<std::mutex> lock(m_mutex);

        try {
            removeFile(m_commandFile);
            removeFile(m_resultFile);
            removeFile(m_stopFlag);
            log("INFO", "文件清理完成");
        }
        catch (const std::exception& e) {
            log("WARN", std::string("清理时出错: ") + e.what());
        }

        m_initialized = false;
    }

    // -------------------- 基础内存操作实现 --------------------

    inline bool Client::readMemory(const std::string& address, CommandResult& result, int timeout) {
        std::string cmd = makeReadCommand(address);
        std::map<std::string, CommandResult> results;

        if (!executeCommands({ cmd }, results, timeout)) {
            return false;
        }

        // 查找结果（可能是原始地址或解析后的地址）
        auto it = results.find(address);
        if (it != results.end()) {
            result = it->second;
            return result.isSuccess();
        }

        // 如果没找到，取第一个结果
        if (!results.empty()) {
            result = results.begin()->second;
            return result.isSuccess();
        }

        setError("未找到地址 " + address + " 的结果");
        return false;
    }

    inline bool Client::readMemory(uint64_t address, CommandResult& result, int timeout) {
        return readMemory(formatAddress(address), result, timeout);
    }

    inline bool Client::writeMemory(const std::string& address, int value, CommandResult& result, int timeout) {
        std::string cmd = makeWriteCommand(address, value);
        std::map<std::string, CommandResult> results;

        if (!executeCommands({ cmd }, results, timeout)) {
            return false;
        }

        auto it = results.find(address);
        if (it != results.end()) {
            result = it->second;
            return result.isSuccess();
        }

        if (!results.empty()) {
            result = results.begin()->second;
            return result.isSuccess();
        }

        setError("写入后未能验证地址 " + address);
        return false;
    }

    inline bool Client::writeMemory(uint64_t address, int value, CommandResult& result, int timeout) {
        return writeMemory(formatAddress(address), value, result, timeout);
    }

    // -------------------- 指针操作实现 --------------------

    inline bool Client::readPointer(uint64_t base, uint64_t offset, CommandResult& result, int timeout) {
        std::string cmd = makePointerCommand(base, offset);
        std::map<std::string, CommandResult> results;

        if (!executeCommands({ cmd }, results, timeout)) {
            return false;
        }

        if (!results.empty()) {
            result = results.begin()->second;
            return result.isSuccess();
        }

        setError("未获取到指针结果");
        return false;
    }

    inline bool Client::readPointerChain(uint64_t base, const std::vector<uint64_t>& offsets,
        CommandResult& result, int timeout) {
        std::string cmd = makePointerChainCommand(base, offsets);
        std::map<std::string, CommandResult> results;

        if (!executeCommands({ cmd }, results, timeout)) {
            return false;
        }

        if (!results.empty()) {
            result = results.begin()->second;
            return result.isSuccess();
        }

        setError("未获取到多层指针结果");
        return false;
    }

    inline bool Client::readPointerChain(const std::string& base, const std::vector<uint64_t>& offsets,
        CommandResult& result, int timeout) {
        std::string cmd = makePointerChainCommand(base, offsets);
        std::map<std::string, CommandResult> results;

        if (!executeCommands({ cmd }, results, timeout)) {
            return false;
        }

        if (!results.empty()) {
            result = results.begin()->second;
            return result.isSuccess();
        }

        setError("未获取到多层指针结果");
        return false;
    }

    // -------------------- 模块操作实现 --------------------

    inline bool Client::getModuleBase(const std::string& moduleName, uint64_t& baseAddress, int timeout) {
        std::string cmd = makeModuleCommand(moduleName);
        std::map<std::string, CommandResult> results;

        if (!executeCommands({ cmd }, results, timeout)) {
            return false;
        }

        auto it = results.find(moduleName);
        if (it != results.end() && it->second.isSuccess()) {
            baseAddress = it->second.getAddressValue();
            return true;
        }

        setError("无法获取模块 " + moduleName + " 的基址");
        return false;
    }

    inline bool Client::readModuleOffset(const std::string& moduleName, uint64_t offset,
        CommandResult& result, int timeout) {
        std::ostringstream oss;
        oss << moduleName << "+" << formatAddress(offset);
        return readMemory(oss.str(), result, timeout);
    }

    inline bool Client::writeModuleOffset(const std::string& moduleName, uint64_t offset, int value,
        CommandResult& result, int timeout) {
        std::ostringstream oss;
        oss << moduleName << "+" << formatAddress(offset);
        return writeMemory(oss.str(), value, result, timeout);
    }

    // -------------------- 断点与寄存器实现 --------------------

    inline bool Client::setBreakpoint(const std::string& address, int timeout) {
        std::string cmd = makeBreakpointCommand(address);
        return sendCustomCommand(cmd, timeout);
    }

    inline bool Client::setBreakpoint(uint64_t address, int timeout) {
        return setBreakpoint(formatAddress(address), timeout);
    }

    inline bool Client::removeBreakpoint(const std::string& address, int timeout) {
        std::string cmd = makeRemoveBreakpointCommand(address);
        return sendCustomCommand(cmd, timeout);
    }

    inline bool Client::removeBreakpoint(uint64_t address, int timeout) {
        return removeBreakpoint(formatAddress(address), timeout);
    }

    inline bool Client::getRegisters(std::map<std::string, uint64_t>& registers, int timeout) {
        std::string cmd = makeGetRegistersCommand();
        std::map<std::string, CommandResult> results;

        if (!executeCommands({ cmd }, results, timeout)) {
            return false;
        }

        registers.clear();
        for (const auto& [key, result] : results) {
            if (result.isSuccess() && key != "REGS") {
                registers[key] = result.getAddressValue();
            }
        }

        return !registers.empty();
    }

    inline bool Client::getRegister(const std::string& regName, uint64_t& value, int timeout) {
        std::map<std::string, uint64_t> registers;
        if (!getRegisters(registers, timeout)) {
            return false;
        }

        auto it = registers.find(regName);
        if (it != registers.end()) {
            value = it->second;
            return true;
        }

        setError("寄存器 " + regName + " 未找到");
        return false;
    }

    // -------------------- 批量操作实现 --------------------

    inline bool Client::executeCommands(const std::vector<std::string>& commands,
        std::map<std::string, CommandResult>& results,
        int timeout) {
        if (!m_initialized) {
            setError("桥接未初始化");
            return false;
        }

        if (commands.empty()) {
            setError("命令列表为空");
            return false;
        }

        if (timeout < 0) {
            timeout = m_config.defaultTimeout;
        }

        std::ostringstream oss;
        for (const auto& cmd : commands) {
            oss << cmd << "\n";
        }

        bool writeSuccess = false;
        for (int i = 0; i < m_config.retryCount; ++i) {
            if (atomicWriteCommand(oss.str())) {
                writeSuccess = true;
                break;
            }
            if (i < m_config.retryCount - 1) {
                std::this_thread::sleep_for(std::chrono::milliseconds(m_config.retryDelay));
            }
        }

        if (!writeSuccess) {
            setError("命令写入失败");
            return false;
        }

        if (!waitForResult(timeout)) {
            setError("等待结果超时");
            return false;
        }

        return parseResultFile(results);
    }

    inline bool Client::sendCustomCommand(const std::string& command, int timeout) {
        std::map<std::string, CommandResult> results;
        return executeCommands({ command }, results, timeout);
    }

    // -------------------- 高级功能实现 --------------------

    inline bool Client::waitForValueChange(uint64_t address, int currentValue,
        CommandResult& result, int timeout, int pollInterval) {
        if (timeout < 0) {
            timeout = m_config.defaultTimeout;
        }

        auto startTime = std::chrono::steady_clock::now();

        while (true) {
            if (readMemory(address, result, pollInterval)) {
                int newValue = result.getIntValue();
                if (newValue != currentValue) {
                    log("INFO", "检测到值变化: " + std::to_string(currentValue) +
                        " -> " + std::to_string(newValue));
                    return true;
                }
            }

            auto elapsed = std::chrono::steady_clock::now() - startTime;
            if (std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() > timeout) {
                setError("等待值变化超时");
                return false;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(pollInterval));
        }
    }

    inline bool Client::readMemoryRange(uint64_t startAddress, int count, int step,
        std::vector<CommandResult>& results, int timeout) {
        std::vector<std::string> commands;
        for (int i = 0; i < count; ++i) {
            uint64_t addr = startAddress + (i * step);
            commands.push_back(makeReadCommand(addr));
        }

        std::map<std::string, CommandResult> resultMap;
        if (!executeCommands(commands, resultMap, timeout)) {
            return false;
        }

        results.clear();
        for (const auto& [addr, result] : resultMap) {
            results.push_back(result);
        }

        return !results.empty();
    }

    // -------------------- 控制操作实现 --------------------

    inline bool Client::sendStopSignal() {
        std::lock_guard<std::mutex> lock(m_mutex);

        try {
            std::ofstream ofs(m_stopFlag);
            if (!ofs) {
                setError("无法创建停止标志文件");
                return false;
            }
            ofs << "STOP";
            log("INFO", "已发送停止信号");
            return true;
        }
        catch (const std::exception& e) {
            setError(std::string("发送停止信号失败: ") + e.what());
            return false;
        }
    }

    inline bool Client::waitForLuaStop(int timeout) {
        auto startTime = std::chrono::steady_clock::now();

        while (true) {
            auto logs = readLuaLog(5);
            for (const auto& line : logs) {
                if (line.find("Bridge stopped normally") != std::string::npos) {
                    log("INFO", "确认 Lua 端已正常停止");
                    return true;
                }
            }

            auto elapsed = std::chrono::steady_clock::now() - startTime;
            if (std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() > timeout) {
                setError("等待 Lua 停止超时");
                return false;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }

    inline bool Client::isLuaRunning(int timeout) {
        if (!fileExists(m_logFile)) {
            return false;
        }

        try {
            auto mtime1 = fs::last_write_time(m_logFile);
            std::this_thread::sleep_for(std::chrono::milliseconds(timeout));

            if (!fileExists(m_logFile)) {
                return false;
            }

            auto mtime2 = fs::last_write_time(m_logFile);
            return mtime2 > mtime1;
        }
        catch (...) {
            return false;
        }
    }

    inline std::vector<std::string> Client::readLuaLog(int lastNLines) {
        std::vector<std::string> lines;

        try {
            if (!fileExists(m_logFile)) {
                return lines;
            }

            std::ifstream ifs(m_logFile);
            if (!ifs) {
                return lines;
            }

            std::string line;
            std::vector<std::string> allLines;
            while (std::getline(ifs, line)) {
                allLines.push_back(line);
            }

            int start = std::max(0, static_cast<int>(allLines.size()) - lastNLines);
            for (int i = start; i < allLines.size(); ++i) {
                lines.push_back(allLines[i]);
            }
        }
        catch (...) {
        }

        return lines;
    }

    // -------------------- 内部方法实现 --------------------

    inline bool Client::atomicWriteCommand(const std::string& content) {
        const std::string tmpFile = m_config.basePath + "command.tmp";

        try {
            {
                std::ofstream ofs(tmpFile, std::ios::binary);
                if (!ofs) {
                    return false;
                }
                ofs << content;
                ofs.flush();
            }

            if (fileExists(m_commandFile)) {
                removeFile(m_commandFile);
            }
            fs::rename(tmpFile, m_commandFile);

            log("DEBUG", "命令已写入");
            return true;
        }
        catch (const std::exception& e) {
            setError(std::string("写入命令失败: ") + e.what());
            return false;
        }
    }

    inline bool Client::waitForResult(int timeout) {
        auto startTime = std::chrono::steady_clock::now();

        while (true) {
            try {
                if (!fileExists(m_resultFile)) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));

                    auto elapsed = std::chrono::steady_clock::now() - startTime;
                    if (std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() > timeout) {
                        return false;
                    }
                    continue;
                }

                auto currentMTime = fs::last_write_time(m_resultFile);

                if (currentMTime > m_lastResultMTime) {
                    m_lastResultMTime = currentMTime;
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    return true;
                }
            }
            catch (const std::exception& e) {
                log("WARN", std::string("等待结果时出错: ") + e.what());
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            auto elapsed = std::chrono::steady_clock::now() - startTime;
            if (std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() > timeout) {
                return false;
            }
        }
    }

    inline bool Client::parseResultFile(std::map<std::string, CommandResult>& results) {
        results.clear();

        try {
            std::ifstream ifs(m_resultFile);
            if (!ifs) {
                setError("无法打开结果文件");
                return false;
            }

            std::string line;
            while (std::getline(ifs, line)) {
                CommandResult result;

                size_t eqPos = line.find('=');
                if (eqPos == std::string::npos) continue;

                result.address = line.substr(0, eqPos);
                result.address.erase(0, result.address.find_first_not_of(" \t"));
                result.address.erase(result.address.find_last_not_of(" \t") + 1);

                size_t semiPos = line.find(';', eqPos);
                if (semiPos != std::string::npos) {
                    result.value = line.substr(eqPos + 1, semiPos - eqPos - 1);
                }
                else {
                    result.value = line.substr(eqPos + 1);
                }
                result.value.erase(0, result.value.find_first_not_of(" \t"));
                result.value.erase(result.value.find_last_not_of(" \t") + 1);

                size_t statusPos = line.find("status=");
                if (statusPos != std::string::npos) {
                    size_t statusEnd = line.find(';', statusPos);
                    std::string statusPart;
                    if (statusEnd != std::string::npos) {
                        statusPart = line.substr(statusPos + 7, statusEnd - statusPos - 7);
                    }
                    else {
                        statusPart = line.substr(statusPos + 7);
                    }
                    statusPart.erase(0, statusPart.find_first_not_of(" \t"));
                    statusPart.erase(statusPart.find_last_not_of(" \t") + 1);
                    result.status = statusPart;
                }

                size_t msgPos = line.find("msg=");
                if (msgPos != std::string::npos) {
                    result.message = line.substr(msgPos + 4);
                    result.message.erase(0, result.message.find_first_not_of(" \t"));
                    result.message.erase(result.message.find_last_not_of(" \t") + 1);
                }

                results[result.address] = result;
            }

            return !results.empty();
        }
        catch (const std::exception& e) {
            setError(std::string("解析结果文件失败: ") + e.what());
            return false;
        }
    }

    inline void Client::log(const std::string& level, const std::string& message) {
        if (m_logCallback) {
            m_logCallback(level, message);
        }
    }

    inline void Client::setError(const std::string& error) {
        m_lastError = error;
    }

    inline std::string Client::getFullPath(const std::string& filename) const {
        return m_config.basePath + filename;
    }

    inline bool Client::fileExists(const std::string& filepath) const {
        return fs::exists(filepath);
    }

    inline bool Client::removeFile(const std::string& filepath) {
        try {
            if (fileExists(filepath)) {
                fs::remove(filepath);
                return true;
            }
            return false;
        }
        catch (...) {
            return false;
        }
    }

    // -------------------- 静态方法实现 --------------------

    inline std::string Client::makeReadCommand(const std::string& address) {
        return "READ " + address;
    }

    inline std::string Client::makeReadCommand(uint64_t address) {
        return "READ " + formatAddress(address);
    }

    inline std::string Client::makeWriteCommand(const std::string& address, int value) {
        return "WRITE " + address + " " + std::to_string(value);
    }

    inline std::string Client::makeWriteCommand(uint64_t address, int value) {
        return "WRITE " + formatAddress(address) + " " + std::to_string(value);
    }

    inline std::string Client::makePointerCommand(uint64_t base, uint64_t offset) {
        return "POINTER " + formatAddress(base) + " " + formatAddress(offset);
    }

    inline std::string Client::makePointerChainCommand(uint64_t base, const std::vector<uint64_t>& offsets) {
        std::ostringstream oss;
        oss << "POINTER " << formatAddress(base);
        for (uint64_t offset : offsets) {
            oss << " " << formatAddress(offset);
        }
        return oss.str();
    }

    inline std::string Client::makePointerChainCommand(const std::string& base, const std::vector<uint64_t>& offsets) {
        std::ostringstream oss;
        oss << "POINTER " << base;
        for (uint64_t offset : offsets) {
            oss << " " << formatAddress(offset);
        }
        return oss.str();
    }

    inline std::string Client::makeModuleCommand(const std::string& moduleName) {
        return "MODULE " + moduleName;
    }

    inline std::string Client::makeBreakpointCommand(const std::string& address) {
        return "BREAKPOINT " + address;
    }

    inline std::string Client::makeRemoveBreakpointCommand(const std::string& address) {
        return "REMOVE_BREAKPOINT " + address;
    }

    inline std::string Client::makeGetRegistersCommand() {
        return "GETREGS";
    }

    inline std::string Client::formatAddress(uint64_t address) {
        std::ostringstream oss;
        oss << "0x" << std::hex << std::uppercase << address;
        return oss.str();
    }

    inline uint64_t Client::parseAddress(const std::string& addressStr) {
        try {
            std::string str = addressStr;
            if (str.substr(0, 2) == "0x" || str.substr(0, 2) == "0X") {
                str = str.substr(2);
            }
            return std::stoull(str, nullptr, 16);
        }
        catch (...) {
            return 0;
        }
    }

} // namespace CEBridge

#endif // CE_BRIDGE_H