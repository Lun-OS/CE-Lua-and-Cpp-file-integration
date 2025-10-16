#pragma once
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
// ============================================================
// CEBridge.h - CE Lua �������Ž�ͷ�ļ� (v3.1 �޸���)
// �޸��˶�ζ�ȡʱ�����������
// ����: Lun + �޸��汾
// ============================================================

#ifndef CE_BRIDGE_OPTIMIZED_H
#define CE_BRIDGE_OPTIMIZED_H

#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <cstdlib>
#include <ctime>

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <chrono>
#include <thread>
#include <functional>
#include <mutex>
#include <atomic>
#include <unordered_map>
#include <optional>
#include <iostream>
#include <iomanip>
#include <array>
#include <memory>

namespace CEBridge {

    namespace fs = std::filesystem;

    // ==================== ���ýṹ ====================
    struct BridgeConfig {
        std::string basePath;
        std::string commandFile;
        std::string resultFile;
        std::string logFile;
        std::string stopFlag;

        int pollMs = 50;                // ��ѯ���
        int idleMs = 1000;              // ���м��
        int idleThreshold = 5;          // ������ֵ
        int defaultTimeout = 2000;      // Ĭ�ϳ�ʱ
        int logFlushMs = 10000;         // ��־ˢ�¼��
        int maxCacheSize = 100;         // ��󻺴��С
        int batchReadSize = 64;         // ������ȡ��������С
        bool verbose = false;

        BridgeConfig() {
            // Use secure environment access on MSVC
#if defined(_WIN32)
            char* envBuf = nullptr;
            size_t envLen = 0;
            if (_dupenv_s(&envBuf, &envLen, "LOCALAPPDATA") == 0 && envBuf != nullptr) {
                basePath = std::string(envBuf) + "\\Temp\\QAQ\\";
                free(envBuf);
            }
            else {
                basePath = ".\\Temp\\QAQ\\";
            }
#else
            const char* env = std::getenv("LOCALAPPDATA");
            basePath = env ? (std::string(env) + "\\Temp\\QAQ\\") : ".\\Temp\\QAQ\\";
#endif

            if (basePath.back() != '\\' && basePath.back() != '/') basePath += "\\";

            commandFile = basePath + "command.txt";
            resultFile = basePath + "result.txt";
            logFile = basePath + "bridge_log.txt";
            stopFlag = basePath + "stop.flag";
        }
    };

    // ==================== �������ṹ ====================
    struct CommandResult {
        std::string address;
        std::string value;
        std::string status;
        std::string message;
        int64_t timestamp = 0;

        bool isOK() const { return status == "OK"; }
    };

    using LogCallback = std::function<void(const std::string& level, const std::string& msg)>;

    // ==================== �ַ����� (�Ż�: �����ظ��ַ�������) ====================
    class StringPool {
    private:
        std::unordered_map<std::string, std::shared_ptr<std::string>> pool_;
        mutable std::mutex mutex_;
        size_t maxSize_ = 1000;

    public:
        std::string_view intern(const std::string& str) {
            if (str.empty()) return {};

            std::lock_guard<std::mutex> lock(mutex_);

            auto it = pool_.find(str);
            if (it != pool_.end()) {
                return *it->second;
            }

            // ���Ƴش�С
            if (pool_.size() >= maxSize_) {
                pool_.clear();
            }

            auto ptr = std::make_shared<std::string>(str);
            pool_[str] = ptr;
            return *ptr;
        }

        void clear() {
            std::lock_guard<std::mutex> lock(mutex_);
            pool_.clear();
        }
    };

    // ==================== ���ֽ������� (�Ż�: �����ظ�����) ====================
    class NumberCache {
    private:
        std::unordered_map<std::string, int64_t> cache_;
        mutable std::mutex mutex_;
        size_t maxSize_ = 1000;

    public:
        std::optional<int64_t> parse(const std::string& str) {
            if (str.empty()) return std::nullopt;

            // ����·��: ��黺��
            {
                std::lock_guard<std::mutex> lock(mutex_);
                auto it = cache_.find(str);
                if (it != cache_.end()) {
                    return it->second;
                }
            }

            // ��������
            std::string trimmed = trim(str);
            if (trimmed.empty()) return std::nullopt;

            bool isNegative = false;
            size_t pos = 0;

            if (trimmed[0] == '-') {
                isNegative = true;
                pos = 1;
            }

            int64_t result = 0;

            try {
                // ���ʮ������
                if (trimmed.size() > pos + 1 && trimmed[pos] == '0' &&
                    (trimmed[pos + 1] == 'x' || trimmed[pos + 1] == 'X')) {
                    result = std::stoll(trimmed.substr(pos + 2), nullptr, 16);
                }
                else {
                    result = std::stoll(trimmed, nullptr, 10);
                }

                if (isNegative) result = -result;

                // ������
                std::lock_guard<std::mutex> lock(mutex_);
                if (cache_.size() < maxSize_) {
                    cache_[str] = result;
                }

                return result;
            }
            catch (...) {
                return std::nullopt;
            }
        }

        void clear() {
            std::lock_guard<std::mutex> lock(mutex_);
            cache_.clear();
        }

    private:
        static std::string trim(const std::string& str) {
            size_t start = str.find_first_not_of(" \t\r\n");
            if (start == std::string::npos) return "";
            size_t end = str.find_last_not_of(" \t\r\n");
            return str.substr(start, end - start + 1);
        }
    };

    // ==================== ��־������ (�Ż�: ����д��) ====================
    class LogBuffer {
    private:
        std::vector<std::string> buffer_;
        std::string logFile_;
        std::chrono::steady_clock::time_point lastFlush_;
        int flushIntervalMs_;
        mutable std::mutex mutex_;

        static constexpr size_t MAX_BUFFER_SIZE = 100;

    public:
        LogBuffer(const std::string& logFile, int flushMs)
            : logFile_(logFile), flushIntervalMs_(flushMs) {
            lastFlush_ = std::chrono::steady_clock::now();
        }

        void log(const std::string& level, const std::string& msg) {
            auto now = std::chrono::system_clock::now();
            auto time = std::chrono::system_clock::to_time_t(now);

            std::ostringstream oss;
#if defined(_MSC_VER)
            std::tm tm;
            localtime_s(&tm, &time);
            oss << "[" << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << "] "
                << level << " " << msg << "\n";
#else
            oss << "[" << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S") << "] "
                << level << " " << msg << "\n";
#endif

            std::lock_guard<std::mutex> lock(mutex_);
            buffer_.push_back(oss.str());

            // �Զ�ˢ��
            if (shouldFlush()) {
                flushInternal();
            }
        }

        void flush() {
            std::lock_guard<std::mutex> lock(mutex_);
            flushInternal();
        }

    private:
        bool shouldFlush() const {
            if (buffer_.size() >= MAX_BUFFER_SIZE) return true;

            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - lastFlush_).count();

            return elapsed >= flushIntervalMs_;
        }

        void flushInternal() {
            if (buffer_.empty()) return;

            try {
                std::ofstream ofs(logFile_, std::ios::app | std::ios::binary);
                if (ofs) {
                    for (const auto& line : buffer_) {
                        ofs << line;
                    }
                    ofs.close();
                }
            }
            catch (...) {}

            buffer_.clear();
            lastFlush_ = std::chrono::steady_clock::now();
        }
    };

    // ==================== ���ͻ����� ====================
    class Client {
    public:
        explicit Client(const BridgeConfig& cfg = BridgeConfig());
        ~Client();

        Client(const Client&) = delete;
        Client& operator=(const Client&) = delete;

        bool initialize();
        void cleanup();
        bool isReady() const { return initialized_.load(); }

        // ��Ҫ�ӿ�
        bool executeCommands(const std::vector<std::string>& commands,
            std::map<std::string, CommandResult>& results,
            int timeout = -1);

        bool readMemory(const std::string& address, CommandResult& result, int timeout = -1);
        bool writeMemory(const std::string& address, const std::string& value,
            CommandResult& result, int timeout = -1);

        bool getModuleBase(const std::string& moduleName, CommandResult& result, int timeout = -1);
        bool readPointer(const std::string& baseAddr, const std::vector<std::string>& offsets,
            CommandResult& result, int timeout = -1);

        // �����������ԵĶ�ȡ�ӿ�
        bool readMemoryWithRetry(const std::string& address, CommandResult& result,
            int maxRetries = 3, int timeout = -1);

        // ���ƽӿ�
        bool sendStopSignal();
        void setLogCallback(LogCallback cb) { logCallback_ = cb; }

        std::string getLastError() const {
            std::lock_guard<std::mutex> lock(mutex_);
            return lastError_;
        }

        const BridgeConfig& getConfig() const { return config_; }

    private:
        BridgeConfig config_;
        std::atomic<bool> initialized_;
        mutable std::mutex mutex_;

        LogCallback logCallback_;
        std::string lastError_;

        // �Ż����
        StringPool stringPool_;
        NumberCache numberCache_;
        std::unique_ptr<LogBuffer> logBuffer_;

        // ״̬����
        fs::file_time_type lastResultMTime_{};
        std::string lastResultContent_;
        int idleCount_;

        // Ԥ����Ļ�����
        std::vector<std::string> resultBuffer_;

        // ��������
        void log(const std::string& level, const std::string& msg);
        bool ensureDirectory();
        bool atomicWriteFile(const std::string& path, const std::string& content);
        bool waitForResultChange(std::string& outContent, int timeoutMs);
        std::map<std::string, CommandResult> parseResults(const std::string& text);
        uint64_t getFileMTime(const std::string& path) const;
        void trimString(std::string& s);

        // �������������ļ�
        bool cleanupResultFile();
    };

    // ==================== ʵ�� ====================

    inline void Client::trimString(std::string& s) {
        size_t start = s.find_first_not_of(" \t");
        if (start == std::string::npos) {
            s.clear();
            return;
        }
        size_t end = s.find_last_not_of(" \t");
        s = s.substr(start, end - start + 1);
    }

    inline Client::Client(const BridgeConfig& cfg)
        : config_(cfg), initialized_(false),
        lastResultMTime_{},
        idleCount_(0) {
        resultBuffer_.reserve(config_.batchReadSize);
    }

    inline Client::~Client() {
        if (initialized_.load()) {
            cleanup();
        }
    }

    inline void Client::log(const std::string& level, const std::string& msg) {
        if (logCallback_) {
            logCallback_(level, msg);
        }

        if (config_.verbose) {
            std::cerr << "[" << level << "] " << msg << std::endl;
        }

        if (logBuffer_) {
            logBuffer_->log(level, msg);
        }
    }

    inline bool Client::ensureDirectory() {
        try {
            fs::path p(config_.basePath);
            if (!fs::exists(p)) {
                fs::create_directories(p);
            }
            return true;
        }
        catch (const std::exception& e) {
            lastError_ = std::string("create dir failed: ") + e.what();
            return false;
        }
    }

    inline bool Client::cleanupResultFile() {
        try {
            std::error_code ec;
            if (fs::exists(config_.resultFile)) {
                fs::remove(config_.resultFile, ec);
                if (ec) {
                    log("WARN", std::string("�������ļ�ʧ��: ") + ec.message());
                    return false;
                }
            }
            lastResultContent_.clear();
            lastResultMTime_ = fs::file_time_type{};
            return true;
        }
        catch (const std::exception& e) {
            log("ERROR", std::string("�������ļ��쳣: ") + e.what());
            return false;
        }
    }

    inline bool Client::initialize() {
        std::lock_guard<std::mutex> lock(mutex_);

        try {
            if (!ensureDirectory()) return false;

            // ��ʼ����־������
            logBuffer_ = std::make_unique<LogBuffer>(config_.logFile, config_.logFlushMs);

            // ������ļ�
            std::error_code ec;
            fs::remove(config_.resultFile, ec);
            fs::remove(config_.commandFile, ec);
            fs::remove(config_.stopFlag, ec);

            lastResultMTime_ = fs::file_time_type{};
            lastResultContent_.clear();
            idleCount_ = 0;

            initialized_.store(true);
            log("INFO", "========== Bridge initialized (v3.1 �޸���) ==========");
            log("INFO", std::string("Base path: ") + config_.basePath);
            log("INFO", "�޸�: ��ζ�ȡ�����������");

            return true;
        }
        catch (const std::exception& e) {
            lastError_ = std::string("init failed: ") + e.what();
            log("ERROR", lastError_);
            return false;
        }
    }

    inline void Client::cleanup() {
        std::lock_guard<std::mutex> lock(mutex_);

        if (logBuffer_) {
            logBuffer_->flush();
        }

        initialized_.store(false);
        stringPool_.clear();
        numberCache_.clear();

        log("INFO", "========== Bridge cleaned up ==========");
    }

    inline bool Client::atomicWriteFile(const std::string& path, const std::string& content) {
        try {
            std::string tmpPath = path + ".tmp";

            // д����ʱ�ļ�
            {
                std::ofstream ofs(tmpPath, std::ios::binary);
                if (!ofs) {
                    lastError_ = "failed to open temp file: " + tmpPath;
                    return false;
                }
                ofs << content;
                ofs.flush();
            }

            // ԭ��������
            std::error_code ec;
            fs::remove(path, ec);
            fs::rename(tmpPath, path, ec);

            if (ec) {
                lastError_ = std::string("atomic rename failed: ") + ec.message();
                fs::remove(tmpPath, ec);
                return false;
            }

            return true;
        }
        catch (const std::exception& e) {
            lastError_ = std::string("atomic write exception: ") + e.what();
            return false;
        }
    }

    inline uint64_t Client::getFileMTime(const std::string& path) const {
        try {
            if (!fs::exists(path)) return 0;

            auto ftime = fs::last_write_time(path);
            auto sctp = std::chrono::time_point_cast<std::chrono::milliseconds>(ftime);
            return static_cast<uint64_t>(sctp.time_since_epoch().count());
        }
        catch (...) {
            return 0;
        }
    }

    inline bool Client::waitForResultChange(std::string& outContent, int timeoutMs) {
        auto start = std::chrono::steady_clock::now();
        uint64_t baseline = getFileMTime(config_.resultFile);

        // ���������0��˵���ļ������ڣ��ȴ���������
        if (baseline == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            baseline = getFileMTime(config_.resultFile);
        }

        while (true) {
            uint64_t current = getFileMTime(config_.resultFile);

            if (current != 0 && current != baseline) {
                // ���ݵȴ���ȷ��д�����
                std::this_thread::sleep_for(std::chrono::milliseconds(10));

                try {
                    std::ifstream ifs(config_.resultFile, std::ios::binary);
                    if (ifs) {
                        std::ostringstream oss;
                        oss << ifs.rdbuf();
                        outContent = oss.str();

                        // ֻ���ڳɹ���ȡ����ʱ�Ÿ���״̬
                        if (!outContent.empty()) {
                            lastResultMTime_ = fs::last_write_time(config_.resultFile);
                            lastResultContent_ = outContent;
                            idleCount_ = 0;
                            return true;
                        }
                    }
                }
                catch (const std::exception& e) {
                    lastError_ = std::string("read result failed: ") + e.what();
                }
            }

            // ��̬˯�߼��
            int sleepMs = (idleCount_ >= config_.idleThreshold) ? config_.idleMs : config_.pollMs;
            std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));

            idleCount_++;

            // ��鳬ʱ
            if (timeoutMs >= 0) {
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - start).count();

                if (elapsed > timeoutMs) {
                    lastError_ = "wait for result timeout";
                    log("ERROR", lastError_);
                    return false;
                }
            }
        }
    }

    inline std::map<std::string, CommandResult> Client::parseResults(const std::string& text) {
        std::map<std::string, CommandResult> results;

        std::istringstream iss(text);
        std::string line;

        while (std::getline(iss, line)) {
            // �Ƴ���β�� \r
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }

            if (line.empty() || line[0] == '#') continue;

            // ������ʽ: KEY = VALUE ; status=... ; msg=... ; ts=...
            size_t eqPos = line.find('=');
            if (eqPos == std::string::npos) continue;

            CommandResult result;
            result.address = stringPool_.intern(
                line.substr(0, eqPos)).data();

            trimString(result.address);

            std::string rest = line.substr(eqPos + 1);
            size_t semiPos = rest.find(';');

            if (semiPos != std::string::npos) {
                result.value = rest.substr(0, semiPos);
            }
            else {
                result.value = rest;
            }
            trimString(result.value);

            // ��������
            size_t pos = 0;
            while (pos < rest.size()) {
                size_t nextSemi = rest.find(';', pos);
                std::string token;

                if (nextSemi == std::string::npos) {
                    token = rest.substr(pos);
                    pos = rest.size();
                }
                else {
                    token = rest.substr(pos, nextSemi - pos);
                    pos = nextSemi + 1;
                }

                trimString(token);
                if (token.empty()) continue;

                // ���� key=value
                size_t tokenEq = token.find('=');
                if (tokenEq != std::string::npos) {
                    std::string key = token.substr(0, tokenEq);
                    std::string val = token.substr(tokenEq + 1);
                    trimString(key);
                    trimString(val);

                    // ת��ΪСд�Ƚ�
                    for (auto& c : key) c = static_cast<char>(std::tolower(c));

                    if (key == "status") {
                        result.status = val;
                    }
                    else if (key == "msg") {
                        result.message = val;
                    }
                    else if (key == "ts") {
                        try {
                            result.timestamp = std::stoll(val);
                        }
                        catch (...) {}
                    }
                }
            }

            results[result.address] = result;
        }

        return results;
    }

    inline bool Client::executeCommands(const std::vector<std::string>& commands,
        std::map<std::string, CommandResult>& results,
        int timeout) {

        if (!initialized_.load()) {
            lastError_ = "not initialized";
            return false;
        }

        if (commands.empty()) {
            lastError_ = "empty commands";
            return false;
        }

        if (timeout < 0) timeout = config_.defaultTimeout;

        // ���������ı�
        std::ostringstream oss;
        for (const auto& cmd : commands) {
            oss << cmd << "\n";
        }
        std::string cmdText = oss.str();

        // ���л�д��
        std::lock_guard<std::mutex> lock(mutex_);

        // +++ �޸���ǿ������ɵĽ���ļ� +++
        if (!cleanupResultFile()) {
            log("WARN", "�������ļ�ʧ�ܣ�����ִ��...");
        }

        // ԭ��д�������ļ�
        if (!atomicWriteFile(config_.commandFile, cmdText)) {
            log("ERROR", lastError_);
            return false;
        }

        log("DEBUG", std::string("Executed ") + std::to_string(commands.size()) + " commands");

        // �ȴ����
        std::string resultText;
        if (!waitForResultChange(resultText, timeout)) {
            return false;
        }

        // �������
        results = parseResults(resultText);

        // +++ �޸��������ַ����أ������ڴ�й© +++
        stringPool_.clear();

        return !results.empty();
    }

    inline bool Client::readMemory(const std::string& address,
        CommandResult& result, int timeout) {

        std::map<std::string, CommandResult> results;
        std::vector<std::string> cmds = { "READ " + address };

        if (!executeCommands(cmds, results, timeout)) return false;

        if (!results.empty()) {
            result = results.begin()->second;
            return result.isOK();
        }

        lastError_ = "no result returned";
        return false;
    }

    inline bool Client::writeMemory(const std::string& address,
        const std::string& value, CommandResult& result, int timeout) {

        std::map<std::string, CommandResult> results;
        std::vector<std::string> cmds = { "WRITE " + address + " " + value };

        if (!executeCommands(cmds, results, timeout)) return false;

        if (!results.empty()) {
            result = results.begin()->second;
            return result.isOK();
        }

        lastError_ = "no result returned";
        return false;
    }

    inline bool Client::getModuleBase(const std::string& moduleName,
        CommandResult& result, int timeout) {

        std::map<std::string, CommandResult> results;
        std::vector<std::string> cmds = { "MODULE " + moduleName };

        if (!executeCommands(cmds, results, timeout)) return false;

        if (!results.empty()) {
            result = results.begin()->second;
            return result.isOK();
        }

        lastError_ = "no result returned";
        return false;
    }

    inline bool Client::readPointer(const std::string& baseAddr,
        const std::vector<std::string>& offsets, CommandResult& result, int timeout) {

        std::ostringstream oss;
        oss << "POINTER " << baseAddr;
        for (const auto& offset : offsets) {
            oss << " " << offset;
        }

        std::map<std::string, CommandResult> results;
        if (!executeCommands({ oss.str() }, results, timeout)) return false;

        if (!results.empty()) {
            result = results.begin()->second;
            return result.isOK();
        }

        lastError_ = "no result returned";
        return false;
    }

    inline bool Client::readMemoryWithRetry(const std::string& address,
        CommandResult& result, int maxRetries, int timeout) {

        for (int i = 0; i < maxRetries; ++i) {
            if (readMemory(address, result, timeout)) {
                return true;
            }

            if (i < maxRetries - 1) {
                log("WARN", std::string("��ȡʧ�ܣ����� ") + std::to_string(i + 1) + "/" + std::to_string(maxRetries));
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }

        return false;
    }

    inline bool Client::sendStopSignal() {
        try {
            ensureDirectory();
            std::ofstream ofs(config_.stopFlag, std::ios::binary);
            if (!ofs) {
                lastError_ = "cannot create stop flag";
                return false;
            }
            ofs << "STOP";
            ofs.close();
            log("INFO", "Stop signal sent");
            return true;
        }
        catch (const std::exception& e) {
            lastError_ = std::string("sendStopSignal failed: ") + e.what();
            return false;
        }
    }

} // namespace CEBridge

#endif // CE_BRIDGE_OPTIMIZED_H