-- ============================================================
-- CE Lua to C++ File Communication Bridge Script (High-Performance Fix)
-- CE Lua 与 C++ 文件通讯桥接脚本（高性能修复版）
-- Version: 2.4 (Fixed multiple read issues)
-- 版本: 2.4 (修复多次读取问题)
-- Legal risks are borne by yourself
-- 法律风险由自己承担
-- Author: Lun. github:Lun-OS  QQ:1596534228
-- 作者：Lun. github:Lun-OS  QQ:1596534228
-- ============================================================

-- -------------------- Configuration Area --------------------
-- -------------------- 配置区 --------------------
local CONFIG = {
    basePath = (os.getenv("LOCALAPPDATA") or ".") .. "\\Temp\\QAQ\\",
    commandFile = "command.txt",
    resultFile = "result.txt",
    logFile = "bridge_log.txt",
    stopFlag = "stop.flag",

    pollInterval = 50,
    idleInterval = 1000,
    idleThreshold = 5,
    logFlushInterval = 10000,
    maxCacheSize = 100,
    batchReadSize = 64,  -- Batch read buffer size / 批量读取缓冲区大小

    -- New: Whether to enable logging (default off)
    -- 新增：是否启用日志（默认关闭）
    enableLogging = false,
}

-- -------------------- 全局缓存 --------------------
local cache = {
    lastCmdMTime = 0,
    results = {},
    lastWrittenResults = "",
    logBuffer = {},
    lastLogFlushTick = 0,
    idleCount = 0,
    isRunning = false,
    threadHandle = nil,
    moduleCache = {},
    breakpoints = {},
    -- 新增：字符串池，减少重复字符串分配
    stringPool = {},
    -- 新增：预分配的结果缓冲区
    resultBuffer = {},
    -- 新增：数字解析缓存
    numCache = {},
    -- 新增：命令处理状态
    processingCommands = false
}

-- -------------------- 优化的工具函数 --------------------

-- 字符串池：复用常用字符串
local function intern(str)
    if not str then return str end
    local cached = cache.stringPool[str]
    if cached then return cached end
    cache.stringPool[str] = str
    return str
end

-- 优化：预计算路径，避免重复字符串拼接
local fullPaths = {}
local function getFullPath(filename)
    local path = fullPaths[filename]
    if not path then
        path = CONFIG.basePath .. filename
        fullPaths[filename] = path
    end
    return path
end

-- 优化：使用预分配的时间戳格式
local function getTimestamp()
    return os.date("[%Y-%m-%d %H:%M:%S]")
end

-- 优化：日志函数，减少字符串拼接
local function log(level, message)
    if not CONFIG.enableLogging then return end
    -- 直接插入表，避免format开销
    local bufferSize = #cache.logBuffer
    cache.logBuffer[bufferSize + 1] = getTimestamp()
    cache.logBuffer[bufferSize + 2] = " "
    cache.logBuffer[bufferSize + 3] = level
    cache.logBuffer[bufferSize + 4] = " "
    cache.logBuffer[bufferSize + 5] = message
end

-- 优化：批量写入日志
local function flushLog()
    if not CONFIG.enableLogging then return end

    local bufferSize = #cache.logBuffer
    if bufferSize == 0 then return end

    local success, err = pcall(function()
        local file = io.open(getFullPath(CONFIG.logFile), "a")
        if file then
            -- 一次性拼接所有日志
            local logStr = table.concat(cache.logBuffer)
            file:write(logStr, "\n")
            file:close()
            -- 清空缓冲区：直接赋值新表比逐个删除快
            cache.logBuffer = {}
            cache.lastLogFlushTick = os.time()
        end
    end)

    if not success then
        cache.logBuffer = {getTimestamp(), " ERROR 日志刷新失败: ", tostring(err)}
    end
end

local function ensureDirectory()
    local path = CONFIG.basePath
    if path:sub(-1) == "\\" or path:sub(-1) == "/" then
        path = path:sub(1, -2)
    end
    os.execute('mkdir "' .. path .. '" 2>nul')
end

-- 优化：文件哈希计算 - 使用更快的算法
local function getFileMTime(filepath)
    local file = io.open(filepath, "rb")
    if not file then return 0 end

    -- 优化：只读取文件大小和首尾字节，不读取全部内容
    local size = file:seek("end")
    if size == 0 then
        file:close()
        return 0
    end

    file:seek("set", 0)
    local first = file:read(1)
    
    if size > 1 then
        file:seek("set", size - 1)
        local last = file:read(1)
        file:close()
        return size + string.byte(first) * 1000 + string.byte(last) * 100
    else
        file:close()
        return size + string.byte(first) * 1000
    end
end

-- 优化：数字解析 - 使用查找表加速
local hexChars = {
    ['0']=0, ['1']=1, ['2']=2, ['3']=3, ['4']=4, ['5']=5, ['6']=6, ['7']=7,
    ['8']=8, ['9']=9, ['a']=10, ['b']=11, ['c']=12, ['d']=13, ['e']=14, ['f']=15,
    ['A']=10, ['B']=11, ['C']=12, ['D']=13, ['E']=14, ['F']=15
}

local function parseNumber(str)
    if not str or str == "" then return nil end

    -- 检查缓存
    local cached = cache.numCache[str]
    if cached then return cached end

    -- 去除空格（优化：避免多次调用match）
    local trimmed = str:match("^%s*(.-)%s*$")
    
    -- 检查负数
    local isNegative = false
    local pos = 1
    if trimmed:byte(1) == 45 then  -- '-' 的ASCII码是45
        isNegative = true
        pos = 2
    end

    -- 解析数字
    local num
    local len = #trimmed
    
    -- 检查是否为十六进制（优化：直接比较字节码）
    if len >= pos + 1 and trimmed:byte(pos) == 48 then  -- '0'
        local next = trimmed:byte(pos + 1)
        if next == 120 or next == 88 then  -- 'x' or 'X'
            -- 十六进制：手动解析（比tonumber更快）
            num = 0
            for i = pos + 2, len do
                local c = trimmed:sub(i, i)
                local digit = hexChars[c]
                if not digit then break end
                num = num * 16 + digit
            end
        else
            num = tonumber(trimmed)
        end
    else
        -- 尝试十进制，失败则尝试十六进制
        num = tonumber(trimmed) or tonumber(trimmed, 16)
    end

    if num and isNegative then
        num = -num
    end

    -- 缓存结果（限制缓存大小）
    if num and #cache.numCache < 1000 then
        cache.numCache[str] = num
    end

    return num
end

-- 获取模块基址
function getModuleBase(moduleName)
    if not moduleName then return nil, 'no module name' end

    -- 先检查缓存
    local lname = moduleName:lower()
    local cached = cache.moduleCache[lname]
    if cached then
        return cached
    end

    -- 尝试使用 getAddress
    if getAddress then
        local ok, addr = pcall(getAddress, moduleName)
        if ok and addr and addr ~= 0 then
            cache.moduleCache[lname] = addr
            log("INFO", "模块 " .. moduleName .. " 基址: 0x" .. string.format("%X", addr))
            return addr
        end
    end

    -- 尝试枚举模块
    if enumModules then
        local ok, result = pcall(enumModules)
        if ok and result then
            for i = 0, result.Count - 1 do
                local modInfo = result[i]
                if modInfo and modInfo.Name then
                    if modInfo.Name:lower() == lname or modInfo.Name:lower():find(lname, 1, true) then
                        cache.moduleCache[lname] = modInfo.Address
                        log("INFO", "模块 " .. moduleName .. " 基址: 0x" .. string.format("%X", modInfo.Address))
                        return modInfo.Address
                    end
                end
            end
        end
    end

    return nil, 'module not found'
end

-- 优化：地址表达式解析 - 减少字符串操作
local function parseAddressExpression(expr)
    if not expr or expr == "" then return nil end

    expr = expr:match("^%s*(.-)%s*$")

    -- 如果整个表达式是十六进制或数字，直接返回
    local num = parseNumber(expr)
    if num then return num end

    -- 查找第一个运算符位置（优化：使用字节码比较）
    local firstOpIdx = nil
    for i = 1, #expr do
        local b = expr:byte(i)
        if b == 43 or b == 45 then  -- '+' or '-'
            firstOpIdx = i
            break
        end
    end

    local head = expr
    local tail = ""
    if firstOpIdx then
        head = expr:sub(1, firstOpIdx - 1)
        tail = expr:sub(firstOpIdx)
    end

    -- 尝试把 head 作为模块名或地址
    local baseAddr = parseNumber(head)
    if not baseAddr then
        local base, err = getModuleBase(head)
        if not base then
            log("ERROR", "无法获取模块 " .. tostring(head) .. ": " .. tostring(err))
            return nil
        end
        baseAddr = base
    end

    -- 优化：解析后续的偏移（减少正则匹配）
    if tail ~= "" then
        local i = 1
        local len = #tail
        while i <= len do
            local op = tail:byte(i)
            if op ~= 43 and op ~= 45 then break end  -- '+' or '-'
            
            i = i + 1
            local numStart = i
            
            -- 找到下一个运算符或结束
            while i <= len do
                local b = tail:byte(i)
                if b == 43 or b == 45 then break end
                i = i + 1
            end
            
            local offsetStr = tail:sub(numStart, i - 1)
            local offset = parseNumber(offsetStr)
            if not offset then
                log("ERROR", "无法解析偏移: " .. offsetStr)
                return nil
            end

            if op == 43 then  -- '+'
                baseAddr = baseAddr + offset
            else  -- '-'
                baseAddr = baseAddr - offset
            end
        end
    end

    return baseAddr
end

-- -------------------- 优化的命令处理函数 --------------------

-- 优化：使用表缓存减少分配
local function createResult(value, status, msg)
    return {value = tostring(value), status = status, msg = msg or ""}
end

-- 读取内存（支持多种数据类型）
local function cmdRead(addrStr, dataType)
    local addr = parseAddressExpression(addrStr)
    if not addr then
        return createResult(0, "ERR", "地址解析失败: " .. tostring(addrStr))
    end

    dataType = (dataType and dataType:lower()) or "int"

    local success, value = pcall(function()
        if dataType == "int" or dataType == "dword" then
            return readInteger(addr)
        elseif dataType == "qword" or dataType == "int64" or dataType == "i64" then
            return readQword(addr)
        elseif dataType == "ptr" then
            return readPointer(addr)
        elseif dataType == "float" then
            return readFloat(addr)
        elseif dataType == "double" then
            return readDouble(addr)
        elseif dataType == "byte" then
            local b = readBytes(addr, 1)
            if type(b) == "table" then return b[1] else return b end
        else
            return readInteger(addr)
        end
    end)

    if success and value ~= nil then
        return createResult(value, "OK", string.format("地址: 0x%X", addr))
    else
        return createResult(0, "ERR", string.format("读取失败 (地址: 0x%X)", addr))
    end
end

-- 写入内存
local function cmdWrite(addrStr, valueStr, dataType)
    local addr = parseAddressExpression(addrStr)
    if not addr then
        return createResult(0, "ERR", "地址解析失败: " .. tostring(addrStr))
    end

    local value = parseNumber(valueStr)
    if not value then
        return createResult(0, "ERR", "数值解析失败")
    end

    dataType = (dataType and dataType:lower()) or "int"

    local success, err = pcall(function()
        if dataType == "int" or dataType == "dword" then
            writeInteger(addr, value)
        elseif dataType == "qword" or dataType == "int64" or dataType == "i64" then
            writeQword(addr, value)
        elseif dataType == "ptr" then
            if targetIs64Bit and targetIs64Bit() then
                writeQword(addr, value)
            else
                writeInteger(addr, value)
            end
        elseif dataType == "float" then
            writeFloat(addr, value)
        elseif dataType == "double" then
            writeDouble(addr, value)
        elseif dataType == "byte" then
            writeByte(addr, value)
        else
            writeInteger(addr, value)
        end
    end)

    if success then
        return createResult(value, "OK", string.format("写入成功 (地址: 0x%X)", addr))
    else
        return createResult(0, "ERR", string.format("写入失败: %s", tostring(err)))
    end
end

-- 优化：多层指针解析 - 批量读取减少函数调用
local function cmdPointer(baseStr, ...)
    local args = {...}

    -- 解析基址
    local currentAddr = parseAddressExpression(baseStr)
    if not currentAddr then
        return createResult(0, "ERR", "基址解析失败: " .. tostring(baseStr))
    end

    -- 识别末尾类型标记（如 int/dword/qword/float/double/byte/ptr/address）
    local finalType = nil
    if #args >= 1 then
        local last = tostring(args[#args])
        local lname = last:lower()
        if lname == "int" or lname == "dword" or lname == "qword" or lname == "float" or lname == "double" or lname == "byte" or lname == "ptr" or lname == "address" or lname == "addr" then
            finalType = lname
            table.remove(args, #args)
        end
    end

    -- 批量解析偏移量
    local parsedOffsets = {}
    for i = 1, #args do
        local offset = parseNumber(args[i])
        if not offset then
            return createResult(0, "ERR", string.format("第%d层偏移解析失败: %s", i, tostring(args[i])))
        end
        parsedOffsets[i] = offset
    end

    -- 逐层解析指针（指针宽度自适应）
    for i = 1, #parsedOffsets do
        local ok, ptrValue = pcall(readPointer, currentAddr)
        if not ok or not ptrValue then
            return createResult(0, "ERR", string.format("第%d层指针读取失败 (地址: 0x%X)", i, currentAddr))
        end
        currentAddr = ptrValue + parsedOffsets[i]
    end

    -- 根据最终类型读取值；默认按 dword
    local ok, finalValue
    if finalType == "float" then
        ok, finalValue = pcall(readFloat, currentAddr)
    elseif finalType == "double" then
        ok, finalValue = pcall(readDouble, currentAddr)
    elseif finalType == "byte" then
        ok, finalValue = pcall(readBytes, currentAddr, 1)
        if ok and type(finalValue) == "table" then finalValue = finalValue[1] end
    elseif finalType == "qword" then
        ok, finalValue = pcall(readQword, currentAddr)
    elseif finalType == "ptr" then
        ok, finalValue = pcall(readPointer, currentAddr)
    elseif finalType == "address" or finalType == "addr" then
        return createResult(string.format("0x%X", currentAddr), "OK", string.format("最终地址: 0x%X", currentAddr))
    else
        ok, finalValue = pcall(readInteger, currentAddr)
    end

    if ok and finalValue ~= nil then
        return createResult(finalValue, "OK", string.format("最终地址: 0x%X", currentAddr))
    else
        return createResult(0, "ERR", string.format("读取最终值失败 (地址: 0x%X)", currentAddr))
    end
end

-- 获取模块基址
local function cmdModule(moduleName)
    if not moduleName or moduleName == "" then
        return createResult(0, "ERR", "模块名为空")
    end

    local base, err = getModuleBase(moduleName)
    if base then
        return createResult(string.format("0x%X", base), "OK", "")
    else
        return createResult(0, "ERR", tostring(err))
    end
end

-- 设置断点
local function cmdSetBreakpoint(addrStr)
    local addr = parseAddressExpression(addrStr)
    if not addr then 
        return createResult(0, "ERR", "地址解析失败") 
    end

    if debug_setBreakpoint then
        local ok, res = pcall(debug_setBreakpoint, addr)
        if ok then
            cache.breakpoints[addr] = true
            return createResult(1, "OK", string.format("断点已设置 (0x%X)", addr))
        end
    end

    return createResult(0, "ERR", "断点设置失败（可能不支持此API）")
end

-- 移除断点
local function cmdRemoveBreakpoint(addrStr)
    local addr = parseAddressExpression(addrStr)
    if not addr then 
        return createResult(0, "ERR", "地址解析失败") 
    end

    if debug_removeBreakpoint then
        local ok, res = pcall(debug_removeBreakpoint, addr)
        if ok then
            cache.breakpoints[addr] = nil
            return createResult(1, "OK", string.format("断点已移除 (0x%X)", addr))
        end
    end

    return createResult(0, "ERR", "断点移除失败")
end

-- 获取寄存器值
local function cmdGetRegs()
    local regs = {}

    -- 尝试读取常见寄存器
    local regNames = {'EAX','EBX','ECX','EDX','ESI','EDI','ESP','EBP','EIP',
                      'RAX','RBX','RCX','RDX','RSI','RDI','RSP','RBP','RIP'}

    for i = 1, #regNames do
        local regName = regNames[i]
        local value = _G[regName]
        if value and type(value) == "number" then
            regs[regName] = string.format("0x%X", value)
        end
    end

    -- 尝试调试上下文
    if debug_getContext then
        local ok, context = pcall(debug_getContext)
        if ok and context and type(context) == "table" then
            for k, v in pairs(context) do
                if type(v) == "number" then
                    regs[k] = string.format("0x%X", v)
                end
            end
        end
    end

    if next(regs) then
        return regs, "OK"
    else
        return nil, "NO_REG_API"
    end
end

-- 优化：命令解析 - 减少字符串匹配和分割
local function executeCommand(line)
    -- 快速跳过空行和注释
    if line == "" or line:byte(1) == 35 then return nil end  -- '#' = 35

    -- 优化：手动分割字符串，避免gmatch开销
    local parts = {}
    local partCount = 0
    local i = 1
    local len = #line
    
    while i <= len do
        -- 跳过空白
        while i <= len do
            local b = line:byte(i)
            if b ~= 32 and b ~= 9 then break end  -- ' ' = 32, '\t' = 9
            i = i + 1
        end
        
        if i > len then break end
        
        -- 提取单词
        local wordStart = i
        while i <= len do
            local b = line:byte(i)
            if b == 32 or b == 9 then break end
            i = i + 1
        end
        
        partCount = partCount + 1
        parts[partCount] = line:sub(wordStart, i - 1)
        i = i + 1
    end

    if partCount == 0 then return nil end

    local cmd = parts[1]:upper()

    if cmd == "READ" and partCount >= 2 then
        local dataType = parts[3] or "int"
        return parts[2], cmdRead(parts[2], dataType)

    elseif cmd == "WRITE" and partCount >= 3 then
        local dataType = parts[4] or "int"
        return parts[2], cmdWrite(parts[2], parts[3], dataType)

    elseif cmd == "POINTER" and partCount >= 3 then
        -- 识别末尾类型标记
        local known = { int=true, dword=true, qword=true, float=true, double=true, byte=true, ptr=true, address=true, addr=true }
        local finalType = nil
        if known[(parts[partCount] or ""):lower()] then
            finalType = parts[partCount]
            partCount = partCount - 1
        end
        -- 构造偏移数组
        local offsets = {}
        for i = 3, partCount do
            offsets[i - 2] = parts[i]
        end
        local key = parts[2] .. "+" .. table.concat(offsets, "+")
        if finalType then key = key .. ":" .. finalType end
        if finalType then
            return key, cmdPointer(parts[2], table.unpack(offsets), finalType)
        else
            return key, cmdPointer(parts[2], table.unpack(offsets))
        end

    elseif cmd == "MODULE" and partCount >= 2 then
        return parts[2], cmdModule(parts[2])

    elseif cmd == "BREAKPOINT" and partCount >= 2 then
        return "BP_" .. parts[2], cmdSetBreakpoint(parts[2])

    elseif cmd == "REMOVE_BREAKPOINT" and partCount >= 2 then
        return "RBP_" .. parts[2], cmdRemoveBreakpoint(parts[2])

    elseif cmd == "GETREGS" then
        local regs, status = cmdGetRegs()
        if regs and status == "OK" then
            for k, v in pairs(regs) do
                cache.results[k] = createResult(v, "OK", "")
            end
            return "REGS", createResult(1, "OK", "寄存器已读取")
        else
            return "REGS", createResult(0, "ERR", "无法读取寄存器（需要在断点状态）")
        end

    else
        return "UNKNOWN", createResult(0, "ERR", "未知命令: " .. line)
    end
end

-- -------------------- 主循环逻辑 --------------------

-- +++ 修复：强制写入结果，即使内容相同 +++
local function writeResults()
    -- 预分配足够大小的缓冲区
    local buffer = cache.resultBuffer
    local bufferIdx = 0
    
    for addr, result in pairs(cache.results) do
        bufferIdx = bufferIdx + 1
        buffer[bufferIdx] = addr
        bufferIdx = bufferIdx + 1
        buffer[bufferIdx] = " = "
        bufferIdx = bufferIdx + 1
        buffer[bufferIdx] = tostring(result.value)
        bufferIdx = bufferIdx + 1
        buffer[bufferIdx] = " ; status="
        bufferIdx = bufferIdx + 1
        buffer[bufferIdx] = result.status

        if result.msg and result.msg ~= "" then
            bufferIdx = bufferIdx + 1
            buffer[bufferIdx] = " ; msg="
            bufferIdx = bufferIdx + 1
            buffer[bufferIdx] = result.msg
        end

        bufferIdx = bufferIdx + 1
        buffer[bufferIdx] = " ; ts="
        bufferIdx = bufferIdx + 1
        buffer[bufferIdx] = tostring(os.time())
        bufferIdx = bufferIdx + 1
        buffer[bufferIdx] = "\n"
    end

    -- 一次性拼接所有结果
    local newContent = table.concat(buffer, "", 1, bufferIdx)
    
    -- 清空缓冲区
    for i = 1, bufferIdx do
        buffer[i] = nil
    end

    -- +++ 修复：使用原子写入，避免读写冲突 +++
    local success = pcall(function()
        -- 先写入临时文件
        local tempPath = getFullPath(CONFIG.resultFile .. ".tmp")
        local file = io.open(tempPath, "w")
        if file then
            file:write(newContent)
            file:close()
            
            -- 原子重命名
            os.remove(getFullPath(CONFIG.resultFile))
            os.rename(tempPath, getFullPath(CONFIG.resultFile))
            
            cache.lastWrittenResults = newContent
            return true
        end
        return false
    end)

    if not success then
        log("ERROR", "写入结果文件失败")
    end

    return success
end

-- 优化：批量处理命令
local function processCommands()
    -- +++ 修复：防止重入 +++
    if cache.processingCommands then
        log("WARN", "命令处理中，跳过重复调用")
        return false
    end
    
    cache.processingCommands = true
    
    local cmdPath = getFullPath(CONFIG.commandFile)
    local file = io.open(cmdPath, "r")

    if not file then 
        cache.processingCommands = false
        return false 
    end

    local content = file:read("*all")
    file:close()

    if not content or content == "" then 
        cache.processingCommands = false
        return false 
    end

    local executeCount = 0
    
    -- +++ 修复：每次处理新命令前清空旧结果 +++
    cache.results = {}
    
    -- 优化：一次性处理所有行，减少循环开销
    local lineStart = 1
    local contentLen = #content
    
    while lineStart <= contentLen do
        -- 查找行结束
        local lineEnd = content:find("[\r\n]", lineStart)
        if not lineEnd then lineEnd = contentLen + 1 end
        
        local line = content:sub(lineStart, lineEnd - 1)
        
        if line ~= "" then
            local addr, result = executeCommand(line)
            if addr and result then
                cache.results[addr] = result
                executeCount = executeCount + 1
            end
        end
        
        -- 跳过换行符
        lineStart = lineEnd + 1
        if lineStart <= contentLen and content:byte(lineStart) == 10 then
            lineStart = lineStart + 1  -- 跳过 \r\n 的 \n
        end
    end

    -- 删除命令文件
    pcall(os.remove, cmdPath)

    if executeCount > 0 then
        log("INFO", string.format("执行了 %d 条命令", executeCount))
        
        -- +++ 修复：强制写入结果 +++
        writeResults()
        
        -- 清理缓存避免内存泄漏
        if #cache.numCache > 500 then
            cache.numCache = {}
        end
    end

    cache.processingCommands = false
    return executeCount > 0
end

local function checkStopFlag()
    local file = io.open(getFullPath(CONFIG.stopFlag), "r")
    if file then
        file:close()
        return true
    end
    return false
end

local function garbageCollect()
    local count = 0
    for _ in pairs(cache.results) do count = count + 1 end

    if count > CONFIG.maxCacheSize then
        log("WARN", string.format("缓存超限(%d)，执行清理", count))
        cache.results = {}
        cache.lastWrittenResults = ""
        
        -- 清理数字缓存
        if #cache.numCache > 500 then
            cache.numCache = {}
        end
    end
end

local function gracefulStop()
    log("INFO", "收到停止信号，开始优雅关闭...")
    flushLog()

    cache.logBuffer = {getTimestamp(), " INFO Bridge stopped normally"}
    flushLog()

    cache.results = {}
    cache.isRunning = false

    pcall(os.remove, getFullPath(CONFIG.stopFlag))
end

local function bridgeLoop()
    cache.lastLogFlushTick = os.time()
    log("INFO", "========== 桥接服务启动（高性能修复版 v2.4）==========")
    log("INFO", string.format("基础路径: %s", CONFIG.basePath))
    log("INFO", "修复: 多次读取结果缓存问题")
    flushLog()

    while cache.isRunning do
        if checkStopFlag() then
            gracefulStop()
            break
        end

        local currentMTime = getFileMTime(getFullPath(CONFIG.commandFile))

        if currentMTime ~= cache.lastCmdMTime and currentMTime > 0 then
            cache.lastCmdMTime = currentMTime
            cache.idleCount = 0

            local hasCommands = processCommands()

            if hasCommands then
                garbageCollect()
            end

            cache.lastCmdMTime = 0
        else
            cache.idleCount = cache.idleCount + 1
        end

        if os.time() - cache.lastLogFlushTick >= math.floor(CONFIG.logFlushInterval / 1000) then
            flushLog()
        end

        local sleepTime = cache.idleCount >= CONFIG.idleThreshold 
            and CONFIG.idleInterval 
            or CONFIG.pollInterval

        sleep(sleepTime)
    end

    log("INFO", "========== 桥接服务已停止 ==========")
    flushLog()
end

-- -------------------- 公共接口 --------------------

function QAQ()
    if cache.isRunning then
        print("桥接服务已在运行中")
        return false
    end

    ensureDirectory()

    pcall(os.remove, getFullPath(CONFIG.stopFlag))
    pcall(function()
        local file = io.open(getFullPath(CONFIG.commandFile), "w")
        if file then file:close() end
    end)

    cache.isRunning = true
    cache.threadHandle = createThread(bridgeLoop)

    print("========================================")
    print("桥接服务已启动（v2.4 修复版）")
    print("缓存路径: " .. CONFIG.basePath)
    print("作者：Lun. github:Lun-OS  QQ:1596534228")
    print("stopQAQ()停止")
    print("========================================")
    return true
end

function stopQAQ()
    if not cache.isRunning then
        print("桥接服务未运行")
        return false
    end

    local file = io.open(getFullPath(CONFIG.stopFlag), "w")
    if file then
        file:write("STOP")
        file:close()
    end

    local timeout = 5000
    local start = os.time()
    while cache.isRunning and (os.time() - start) < math.floor(timeout/1000) do
        sleep(100)
    end

    if cache.isRunning then
        print("警告: 桥接服务未能在超时内停止")
        return false
    end

    print("桥接服务已停止")
    return true
end

print("========================================")
print("CE Lua桥接脚本已加载 v2.4（修复版）")
print("目标路径: " .. CONFIG.basePath)
print("使用 QAQ() 启动服务")
print("使用 stopQAQ() 停止服务")
print("法律风险由自己承担\n作者：Lun.  github:Lun-OS  QQ:1596534228")
print("========================================")