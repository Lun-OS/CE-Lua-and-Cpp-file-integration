#include "CEBridge.h"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_impl_dx11.h"
#include <d3d11.h>
#include <tchar.h>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <array>

#include <tlhelp32.h>

// Helper function to check if a window is a main window
BOOL IsMainWindow(HWND handle)
{
    return GetWindow(handle, GW_OWNER) == (HWND)0 && IsWindowVisible(handle);
}

// Structure to pass data to EnumWindows callback
struct EnumWindowsData {
    DWORD process_id;
    HWND window_handle;
};

// Callback function for EnumWindows
BOOL CALLBACK EnumWindowsCallback(HWND handle, LPARAM lParam)
{
    EnumWindowsData& data = *(EnumWindowsData*)lParam;
    DWORD process_id = 0;
    GetWindowThreadProcessId(handle, &process_id);
    if (data.process_id != process_id || !IsMainWindow(handle))
        return TRUE;
    data.window_handle = handle;
    return FALSE;
}

// Function to find the main window handle of a process by its ID
HWND FindMainWindow(DWORD process_id)
{
    EnumWindowsData data;
    data.process_id = process_id;
    data.window_handle = 0;
    EnumWindows(EnumWindowsCallback, (LPARAM)&data);
    return data.window_handle;
}

// Function to find the process ID by executable name
DWORD FindProcessId(const std::wstring& processName)
{
    PROCESSENTRY32 processInfo;
    processInfo.dwSize = sizeof(processInfo);

    HANDLE processesSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);
    if (processesSnapshot == INVALID_HANDLE_VALUE)
        return 0;

    // 遍历所有进程，查找匹配的进程名
    if (Process32First(processesSnapshot, &processInfo))
    {
        do
        {
            if (!processName.compare(processInfo.szExeFile))
            {
                CloseHandle(processesSnapshot);
                return processInfo.th32ProcessID;
            }
        } while (Process32Next(processesSnapshot, &processInfo));
    }

    while (Process32Next(processesSnapshot, &processInfo))
    {
        if (!processName.compare(processInfo.szExeFile))
        {
            CloseHandle(processesSnapshot);
            return processInfo.th32ProcessID;
        }
    }
    
    CloseHandle(processesSnapshot);
    return 0;
}

// Direct3D variables
static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

// Forward declarations
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Application state
struct AppState {
    CEBridge::Client* bridge = nullptr;
    bool initialized = false;

    // Log system
    std::vector<std::string> logs;
    bool autoScroll = true;

    // Input buffer
    char addressInput[256] = "";
    char valueInput[64] = "";
    char moduleNameInput[128] = "";
    char offsetInput[64] = "";
    char baseAddressInput[256] = "";
    int layerCount = 2;
    std::vector<std::array<char, 64>> offsetInputs;

    // Result display
    CEBridge::CommandResult lastResult;
    bool hasResult = false;
    std::map<std::string, uint64_t> registers;

    // UI state
    int currentTab = 0;
    bool showAbout = false;
    bool showSettings = false;

    // History
    std::vector<std::string> addressHistory;
    int maxHistory = 20;

    // Settings options
    bool verboseLogging = true;
    bool hexDisplay = true;

    AppState() {
        offsetInputs.resize(10);
        for (int i = 0; i < 10; i++) {
            offsetInputs[i].fill(0);
        }
    }

    void addToHistory(const std::string& addr) {
        // 移除重复
        auto it = std::find(addressHistory.begin(), addressHistory.end(), addr);
        if (it != addressHistory.end()) {
            addressHistory.erase(it);
        }
        addressHistory.insert(addressHistory.begin(), addr);
        if (addressHistory.size() > maxHistory) {
            addressHistory.pop_back();
        }
    }
};

static AppState g_appState;

// Add log to log system
void AddLog(const std::string& message) {
    g_appState.logs.push_back(message);
    if (g_appState.logs.size() > 1000) {
        g_appState.logs.erase(g_appState.logs.begin());
    }
}

// Format address as hexadecimal format
std::string FormatAddress(uint64_t addr) {
    std::stringstream ss;
    ss << "0x" << std::hex << std::uppercase << std::setw(16) << std::setfill('0') << addr;
    return ss.str();
}

// Initialize bridge
void InitializeBridge() {
    if (g_appState.initialized) return;

    CEBridge::BridgeConfig config;
    config.verboseLogging = true;

    g_appState.bridge = new CEBridge::Client(config);

    // Set log callback
    g_appState.bridge->setLogCallback([](const std::string& level, const std::string& message) {
        AddLog("[" + level + "] " + message);
        });

    if (g_appState.bridge->initialize()) {
        g_appState.initialized = true;
        AddLog("[成功] 桥接客户端初始化成功");
    }
    else {
        AddLog("[错误] 初始化失败: " + g_appState.bridge->getLastError());
    }
}

// Render result panel
void RenderResultPanel() {
    if (!g_appState.hasResult) return;

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Operation Result:");
    ImGui::Text("Address: %s", g_appState.lastResult.address.c_str());
    long long displayValue = 0;
    try {
        displayValue = std::stoll(g_appState.lastResult.value);
    } catch (const std::exception& e) {
        // Handle conversion error, e.g., log it or set a default value
        AddLog("Error converting value to long long: " + std::string(e.what()));
    }
    ImGui::Text("Value: %lld (0x%llX)", displayValue, displayValue);
    ImGui::Text("Status: %s", g_appState.lastResult.status.c_str());
    ImGui::TextWrapped("Info: %s", g_appState.lastResult.message.c_str());
}

// Render log window
void RenderLogWindow() {
    ImGui::Begin("Log Window", nullptr, ImGuiWindowFlags_HorizontalScrollbar);

    if (ImGui::Button("Clear Log")) {
        g_appState.logs.clear();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Auto Scroll", &g_appState.autoScroll);

    ImGui::Separator();
    ImGui::BeginChild("LogContent");

    for (const auto& log : g_appState.logs) {
        // 根据日志级别着色
        if (log.find("[ERROR]") != std::string::npos || log.find("[错误]") != std::string::npos) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", log.c_str());
        }
        else if (log.find("[SUCCESS]") != std::string::npos || log.find("[成功]") != std::string::npos) {
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s", log.c_str());
        }
        else if (log.find("[WARN]") != std::string::npos || log.find("[警告]") != std::string::npos) {
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.4f, 1.0f), "%s", log.c_str());
        }
        else {
            ImGui::Text("%s", log.c_str());
        }
    }

    if (g_appState.autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
        ImGui::SetScrollHereY(1.0f);
    }

    ImGui::EndChild();
    ImGui::End();
}

// Render main window
void RenderMainWindow() {
    ImGui::Begin("CE Lua Bridge Client v2.0 (ImGui Version)", nullptr, ImGuiWindowFlags_MenuBar);

    // Menu Bar
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Exit", "Alt+F4")) {
                ::PostQuitMessage(0);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("About")) {
                g_appState.showAbout = true;
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    // Initial status
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Important Notes:");
    ImGui::BulletText("Please ensure the Lua script is loaded and started in CE.");
    ImGui::BulletText("Execute: QAQ() in CE Lua console.");
    ImGui::BulletText("Ensure the target process is attached to CE.");

    if (g_appState.initialized) {
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Bridge Initialized.");
    }
    else {
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Bridge Not Initialized.");
    }

    if (ImGui::Button("Initialize Bridge", ImVec2(200, 40))) {
        InitializeBridge();
    }

    // Tabs
    if (ImGui::BeginTabBar("##Tabs")) {
        if (ImGui::BeginTabItem("Basic Operations")) {
            ImGui::Spacing();

            // Read Memory
            ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "Read Memory Address");
            ImGui::InputText("##ReadAddr", g_appState.addressInput, sizeof(g_appState.addressInput));
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Supported formats: 0x1234 or game.exe+0x1234");
            }

            if (ImGui::Button("Read", ImVec2(100, 0))) {
                if (g_appState.bridge->readMemory(g_appState.addressInput, g_appState.lastResult)) {
                    g_appState.hasResult = true;
                    AddLog("[SUCCESS] Memory read successfully");
                }
                else {
                    AddLog("[ERROR] Read failed: " + g_appState.bridge->getLastError());
                }
            }

            ImGui::Separator();

            // Write Memory
            ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "Write Memory Address");
            ImGui::InputText("##WriteAddr", g_appState.addressInput, sizeof(g_appState.addressInput));
            ImGui::InputText("New Value", g_appState.valueInput, sizeof(g_appState.valueInput));

            if (ImGui::Button("Write", ImVec2(100, 0))) {
                try {
                    int value = std::stoi(g_appState.valueInput);
                    if (g_appState.bridge->writeMemory(g_appState.addressInput, value, g_appState.lastResult)) {
                        g_appState.hasResult = true;
                        AddLog("[SUCCESS] Memory written successfully");
                    }
                    else {
                        AddLog("[ERROR] Write failed: " + g_appState.bridge->getLastError());
                    }
                }
                catch (...) {
                    AddLog("[ERROR] Invalid number format");
                }
            }

            RenderResultPanel();
            ImGui::EndTabItem();
        }

        // Pointer Chain Tab
        if (ImGui::BeginTabItem("Pointer Chain")) {
            ImGui::Spacing();

            // Multi-level Pointer Read
            ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "Multi-level Pointer Read");
            ImGui::InputText("Address##PointerBase", g_appState.baseAddressInput, sizeof(g_appState.baseAddressInput));
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Supported formats: game.exe+0x1234 or 0x10A3F200");
            }

            ImGui::SliderInt("Offset Layers", &g_appState.layerCount, 1, 10);

            for (int i = 0; i < g_appState.layerCount; i++) {
                char label[32];
                snprintf(label, sizeof(label), "Offset #%d", i + 1);
                ImGui::InputText(label, g_appState.offsetInputs[i].data(), g_appState.offsetInputs[i].size());
            }

            if (ImGui::Button("Read Pointer Chain", ImVec2(150, 0))) {
                std::vector<uint64_t> offsets;
                for (int i = 0; i < g_appState.layerCount; i++) {
                    offsets.push_back(CEBridge::Client::parseAddress(g_appState.offsetInputs[i].data()));
                }

                if (g_appState.bridge->readPointerChain(g_appState.baseAddressInput, offsets, g_appState.lastResult)) {
                    g_appState.hasResult = true;
                    AddLog("[SUCCESS] Multi-level pointer read successfully");
                }
                else {
                    AddLog("[ERROR] Read failed: " + g_appState.bridge->getLastError());
                }
            }

            RenderResultPanel();
            ImGui::EndTabItem();
        }

        // Module Operations Tab
        if (ImGui::BeginTabItem("Module Operations")) {
            ImGui::Spacing();

            // Get Module Address
            ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "Get Module Address");
            ImGui::InputText("Module Name##ModBase", g_appState.moduleNameInput, sizeof(g_appState.moduleNameInput));
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("e.g.: game.exe");
            }

            if (ImGui::Button("Get Address", ImVec2(120, 0))) {
                uint64_t baseAddr;
                if (g_appState.bridge->getModuleBase(g_appState.moduleNameInput, baseAddr)) {
                    AddLog("[SUCCESS] Module address: " + FormatAddress(baseAddr));
                }
                else {
                    AddLog("[ERROR] Get failed: " + g_appState.bridge->getLastError());
                }
            }

            ImGui::Separator();

            // Read Module Offset
            ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "Read Module Offset");
            ImGui::InputText("Module Name##ModRead", g_appState.moduleNameInput, sizeof(g_appState.moduleNameInput));
            ImGui::InputText("Offset", g_appState.offsetInput, sizeof(g_appState.offsetInput));

            if (ImGui::Button("Read", ImVec2(100, 0))) {
                uint64_t offset = CEBridge::Client::parseAddress(g_appState.offsetInput);
                if (g_appState.bridge->readModuleOffset(g_appState.moduleNameInput, offset, g_appState.lastResult)) {
                    g_appState.hasResult = true;
                    AddLog("[SUCCESS] Module offset read successfully");
                }
                else {
                    AddLog("[ERROR] Read failed: " + g_appState.bridge->getLastError());
                }
            }

            ImGui::SameLine();

            // Write Module Offset
            ImGui::InputText("New Value##ModWrite", g_appState.valueInput, sizeof(g_appState.valueInput));
            if (ImGui::Button("Write", ImVec2(100, 0))) {
                try {
                    int value = std::stoi(g_appState.valueInput);
                    uint64_t offset = CEBridge::Client::parseAddress(g_appState.offsetInput);
                    if (g_appState.bridge->writeModuleOffset(g_appState.moduleNameInput, offset, value, g_appState.lastResult)) {
                        g_appState.hasResult = true;
                        AddLog("[SUCCESS] Module offset written successfully");
                    }
                    else {
                        AddLog("[ERROR] Write failed: " + g_appState.bridge->getLastError());
                    }
                }
                catch (...) {
                    AddLog("[ERROR] Invalid number format");
                }
            }

            RenderResultPanel();
            ImGui::EndTabItem();
        }

        // Debugging Features Tab
        if (ImGui::BeginTabItem("Debugging Features")) {
            ImGui::Spacing();

            // Breakpoint Settings
            ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "Breakpoint Settings");
            ImGui::InputText("Breakpoint Address", g_appState.addressInput, sizeof(g_appState.addressInput));

            if (ImGui::Button("Set Breakpoint", ImVec2(120, 0))) {
                if (g_appState.bridge->setBreakpoint(g_appState.addressInput)) {
                    AddLog("[SUCCESS] Breakpoint set successfully");
                }
                else {
                    AddLog("[ERROR] Set failed: " + g_appState.bridge->getLastError());
                }
            }

            ImGui::SameLine();

            if (ImGui::Button("Remove Breakpoint", ImVec2(120, 0))) {
                if (g_appState.bridge->removeBreakpoint(g_appState.addressInput)) {
                    AddLog("[SUCCESS] Breakpoint removed");
                }
                else {
                    AddLog("[ERROR] Remove failed: " + g_appState.bridge->getLastError());
                }
            }

            ImGui::Separator();

            // Register View
            ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "Register Status");
            if (ImGui::Button("Refresh Registers", ImVec2(150, 0))) {
                if (g_appState.bridge->getRegisters(g_appState.registers)) {
                    AddLog("[SUCCESS] Register information updated");
                }
                else {
                    AddLog("[ERROR] Get failed: " + g_appState.bridge->getLastError());
                }
            }

            if (!g_appState.registers.empty()) {
                ImGui::BeginChild("RegisterView", ImVec2(0, 300), true);
                ImGui::Columns(2);
                ImGui::Text("Register"); ImGui::NextColumn();
                ImGui::Text("Value"); ImGui::NextColumn();
                ImGui::Separator();

                for (const auto& [regName, regValue] : g_appState.registers) {
                    ImGui::Text("%s", regName.c_str()); ImGui::NextColumn();
                    ImGui::Text("%s", FormatAddress(regValue).c_str()); ImGui::NextColumn();
                }

                ImGui::Columns(1);
                ImGui::EndChild();
            }
            else {
                ImGui::TextDisabled("No register data available");
                ImGui::TextDisabled("Hint: Registers can only be read when a breakpoint is triggered");
            }

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();

    // 关于对话框
    if (g_appState.showAbout) {
        ImGui::OpenPopup("关于");
        if (ImGui::BeginPopupModal("About CE Lua Bridge Client", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("CE Lua Bridge Client v2.0");
            ImGui::Separator();
            ImGui::Text("Developed by Lun");
            ImGui::Text("GitHub: Lun-OS");
            ImGui::Text("QQ: 1596534228");
            ImGui::Text("This tool is for learning and research purposes only.");
            ImGui::Text("Please do not use it for illegal activities.");
            ImGui::Separator();
            if (ImGui::Button("OK", ImVec2(120, 0))) { ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();
        }
    }
}

// Main function
    // 主函数
static HWND g_hParentWindow = nullptr;

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // Find parent window
    // 查找父窗口
    g_hParentWindow = ::FindWindowW(nullptr, L"Target Window");

    // Create window class
    // 创建窗口类
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"CEBridgeImGui", nullptr };
    ::RegisterClassExW(&wc);
    
    // Get screen dimensions
    // 获取屏幕尺寸
    // int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    // int screenHeight = GetSystemMetrics(SM_CYSCREEN);

    // Find testck.exe process window
    // 查找 testck.exe 进程的窗口
    DWORD testckPid = FindProcessId(L"testck.exe");
    HWND testckHwnd = nullptr;
    if (testckPid != 0) {
        testckHwnd = FindMainWindow(testckPid);
    }

    RECT testckRect = {0, 0, 0, 0};
    if (testckHwnd != nullptr) {
        GetWindowRect(testckHwnd, &testckRect);
    }

    int windowX = testckRect.left;
    int windowY = testckRect.top;
    int windowWidth = testckRect.right - testckRect.left;
    int windowHeight = testckRect.bottom - testckRect.top;

    if (windowWidth == 0 || windowHeight == 0) {
        // Fallback to fullscreen if testck.exe window not found or invalid
        // 如果未找到 testck.exe 窗口或其尺寸无效，则回退到全屏模式
        windowWidth = GetSystemMetrics(SM_CXSCREEN);
        windowHeight = GetSystemMetrics(SM_CYSCREEN);
        windowX = 0;
        windowY = 0;
    }
    
    // Create fullscreen transparent window
    // 创建全屏透明窗口
    HWND hwnd = ::CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST, 
        wc.lpszClassName, 
        L"CE Lua Bridge Client v2.0 (ImGui)", 
        WS_POPUP, 
        windowX, windowY, windowWidth, windowHeight, 
        nullptr, nullptr, wc.hInstance, nullptr
    );

    // Initialize Direct3D
    // 初始化 Direct3D
    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    // Show window
    // 显示窗口
    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    
    // Set window transparent - black background becomes transparent, other colors remain opaque
    // 设置窗口透明 - 黑色背景变透明，其他颜色保持不透明
    ::SetLayeredWindowAttributes(hwnd, RGB(0, 0, 0), 0, LWA_COLORKEY);
    
    ::UpdateWindow(hwnd);

    // Initialize ImGui
    // 初始化 ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Set ImGui style
    // 设置 ImGui 样式
    ImGui::StyleColorsDark();

    // Custom color theme - transparent window background, opaque menu
    // 自定义颜色主题 - 窗口背景透明，菜单不透明
    ImGuiStyle& style = ImGui::GetStyle();
    
    // Set ImGui window background to dark gray (opaque), main window background to black (transparent)
    // 设置ImGui窗口背景为深灰色（不透明），主窗口背景为黑色（透明）
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.06f, 0.06f, 0.94f);
    
    // Set menu and controls to opaque dark theme
    // 设置菜单和控件为不透明的深色主题
    style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.14f, 0.14f, 0.14f, 1.0f);
    style.Colors[ImGuiCol_PopupBg] = ImVec4(0.08f, 0.08f, 0.08f, 0.94f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.16f, 0.29f, 0.48f, 0.54f);
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.40f);
    style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.26f, 0.59f, 0.98f, 0.67f);
    style.Colors[ImGuiCol_TitleBg] = ImVec4(0.04f, 0.04f, 0.04f, 1.0f);
    style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.16f, 0.29f, 0.48f, 1.0f);
    style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.00f, 0.00f, 0.00f, 0.51f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.26f, 0.59f, 0.98f, 0.40f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.06f, 0.53f, 0.98f, 1.00f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.26f, 0.59f, 0.98f, 0.31f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.80f);
    style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);

    // Set ImGui platform/rendering backend
    // 设置 ImGui 平台/渲染后端
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    // Main loop
    // 主循环
    bool done = false;
    while (!done) {
        // Find testck.exe process window
        // 查找 testck.exe 进程的窗口
        DWORD testckPid = FindProcessId(L"testck.exe");
        HWND testckHwnd = nullptr;
        if (testckPid != 0) {
            testckHwnd = FindMainWindow(testckPid);
        }

        RECT testckRect = {0, 0, 0, 0};
        if (testckHwnd != nullptr) {
            GetWindowRect(testckHwnd, &testckRect);
        }

        int windowX = testckRect.left;
        int windowY = testckRect.top;
        int windowWidth = testckRect.right - testckRect.left;
        int windowHeight = testckRect.bottom - testckRect.top;

        if (windowWidth == 0 || windowHeight == 0) {
            // Fallback to fullscreen if testck.exe window not found or invalid
            // 如果未找到 testck.exe 窗口或其尺寸无效，则回退到全屏模式
            windowWidth = GetSystemMetrics(SM_CXSCREEN);
            windowHeight = GetSystemMetrics(SM_CYSCREEN);
            windowX = 0;
            windowY = 0;
        }

        // Update current window position and size
        // 更新当前窗口的位置和大小
        ::SetWindowPos(hwnd, nullptr, windowX, windowY, windowWidth, windowHeight, SWP_NOZORDER | SWP_NOACTIVATE);

        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done)
            break;

        // Start ImGui frame
        // 开始 ImGui 帧
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // Render interface
        // 渲染界面
        RenderMainWindow();
        RenderLogWindow();

        // Render
        // 渲染
        ImGui::Render();
        // Set clear color to black, with color key transparency setting
        // 设置清除颜色为黑色，配合颜色键透明度设置
        const float clear_color[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        g_pSwapChain->Present(1, 0);
    }

    // Cleanup
    // 清理
    if (g_appState.bridge) {
        delete g_appState.bridge;
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);

    return 0;
}

// Direct3D device creation
// Direct3D 设备创建
bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
    HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res == DXGI_ERROR_UNSUPPORTED)
        res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res != S_OK) {
        AddLog(std::string("[Error] D3D11CreateDeviceAndSwapChain failed, HRESULT: ") + std::to_string(res)); // [错误] D3D11CreateDeviceAndSwapChain 失败，HRESULT:
        return false;
    }

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

void CleanupRenderTarget() {
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg) {
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED)
            return 0;
        if (g_pd3dDevice != nullptr) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU)
            return 0;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}

// For learning and communication purposes only
// 仅供学习交流使用
// Author: Lun. github:Lun-OS QQ:1596534228
// 作者:Lun. github:Lun-OS  QQ:1596534228
