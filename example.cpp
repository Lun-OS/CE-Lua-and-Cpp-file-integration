#include "CEBridge.h"
#include <iostream>
#include <iomanip>
#include <limits>

void printSeparator() {
    std::cout << "\n========================================\n";
}

void printResult(const CEBridge::CommandResult& result) {
    std::cout << "��ַ: " << result.address << std::endl;
    std::cout << "ֵ: " << result.value << std::endl;
    std::cout << "״̬: " << result.status << std::endl;
    if (!result.message.empty()) {
        std::cout << "��Ϣ: " << result.message << std::endl;
    }
}

int main() {
    printSeparator();
    std::cout << "   CE Lua �Žӿͻ��� v2.0����ǿ�棩" << std::endl;
    printSeparator();

    // ��������
    CEBridge::BridgeConfig config;
    config.verbose = true;

    // �����ͻ���
    CEBridge::Client bridge(config);

    // ������־�ص�
    bridge.setLogCallback([](const std::string& level, const std::string& message) {
        std::cout << "[" << level << "] " << message << std::endl;
        });

    // ��ʼ��
    if (!bridge.initialize()) {
        std::cerr << "��ʼ��ʧ��: " << bridge.getLastError() << std::endl;
        std::cout << "\n���س����˳�...";
        std::cin.get();
        return 1;
    }

    std::cout << "\n��Ҫ��ʾ:" << std::endl;
    std::cout << "1. ��ȷ������ CE �м�����ǿ�� Lua �ű�" << std::endl;
    std::cout << "2. �� CE Lua ����ִ̨��: QAQ()" << std::endl;
    std::cout << "3. ȷ��Ŀ������Ѹ��ӵ� CE" << std::endl;
    std::cout << "\n���س�������..." << std::endl;
    std::cin.get();

    // ���˵�ѭ��
    while (true) {
        printSeparator();
        std::cout << "��ѡ�����:" << std::endl;
        std::cout << "1. ��ȡ�ڴ��ַ" << std::endl;
        std::cout << "2. д���ڴ��ַ" << std::endl;
        std::cout << "3. ����ָ���ȡ" << std::endl;
        std::cout << "4. ���ָ���ȡ" << std::endl;
        std::cout << "5. ��ȡģ���ַ" << std::endl;
        std::cout << "6. ��ȡģ��ƫ��" << std::endl;
        std::cout << "7. д��ģ��ƫ��" << std::endl;
        std::cout << "8. ���öϵ�" << std::endl;
        std::cout << "9. �Ƴ��ϵ�" << std::endl;
        std::cout << "10. ��ȡ�Ĵ���ֵ" << std::endl;
        std::cout << "11. �ۺϲ��ԣ�ģ��+���ָ�룩" << std::endl;
        std::cout << "0. �˳�����" << std::endl;
        printSeparator();
        std::cout << "������ѡ��: ";

        int choice;
        if (!(std::cin >> choice)) {
            std::cin.clear();
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            std::cout << "������Ч��������\n";
            continue;
        }
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

        if (choice == 0) {
            std::cout << "�����˳�..." << std::endl;
            break;
        }

        CEBridge::CommandResult result;

        switch (choice) {
        case 1: { // ��ȡ�ڴ棨֧��ģ����+ƫ�ƣ�
            std::cout << "\n�����ַ��֧�ָ�ʽ��0x1234 �� game.exe+0x1234��: ";
            std::string addr;
            std::getline(std::cin, addr);

            if (bridge.readMemory(addr, result)) {
                printSeparator();
                std::cout << "��ȡ�ɹ�:" << std::endl;
                printResult(result);
            }
            else {
                std::cout << "��ȡʧ��: " << bridge.getLastError() << std::endl;
            }
            break;
        }

        case 2: { // д���ڴ�
            std::cout << "\n�����ַ��֧�ָ�ʽ��0x1234 �� game.exe+0x1234��: ";
            std::string addr;
            std::getline(std::cin, addr);

            std::cout << "����Ҫд�����ֵ: ";
            std::string value;
            std::getline(std::cin, value);

            if (bridge.writeMemory(addr, value, result)) {
                printSeparator();
                std::cout << "д��ɹ�:" << std::endl;
                printResult(result);
            }
            else {
                std::cout << "д��ʧ��: " << bridge.getLastError() << std::endl;
            }
            break;
        }

        case 3: { // ����ָ��
            std::cout << "\n�����ַ��ʮ�����ƣ��� 10A3F200 �� 0x10A3F200��: ";
            std::string baseStr;
            std::getline(std::cin, baseStr);

            std::cout << "����ƫ�ƣ�ʮ�����ƣ��� 18 �� 0x18��: ";
            std::string offsetStr;
            std::getline(std::cin, offsetStr);

            std::vector<std::string> offsets = { offsetStr };
            if (bridge.readPointer(baseStr, offsets, result)) {
                printSeparator();
                std::cout << "ָ���ȡ�ɹ�:" << std::endl;
                printResult(result);
            }
            else {
                std::cout << "��ȡʧ��: " << bridge.getLastError() << std::endl;
            }
            break;
        }

        case 4: { // ���ָ��
            std::cout << "\n�����ַ��֧��ģ�������� game.exe+0x1234 �� 0x10A3F200��: ";
            std::string baseStr;
            std::getline(std::cin, baseStr);

            std::cout << "����ƫ�Ʋ���: ";
            int layers;
            if (!(std::cin >> layers)) {
                std::cin.clear();
                std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                std::cout << "����������Ч\n";
                break;
            }
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

            std::vector<std::string> offsets;
            for (int i = 0; i < layers; ++i) {
                std::cout << "�����" << (i + 1) << "��ƫ�ƣ�ʮ�����ƣ�: 0x";
                std::string offsetStr;
                std::cin >> offsetStr;
                offsets.push_back(offsetStr);
            }
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

            if (bridge.readPointer(baseStr, offsets, result)) {
                printSeparator();
                std::cout << "���ָ���ȡ�ɹ�:" << std::endl;
                printResult(result);
            }
            else {
                std::cout << "��ȡʧ��: " << bridge.getLastError() << std::endl;
            }
            break;
        }

        case 5: { // ��ȡģ���ַ
            std::cout << "\n����ģ�������� game.exe��: ";
            std::string moduleName;
            std::getline(std::cin, moduleName);

            if (bridge.getModuleBase(moduleName, result)) {
                printSeparator();
                std::cout << "ģ���ַ��ȡ�ɹ�:" << std::endl;
                std::cout << moduleName << " ��ַ: " << result.value << std::endl;
            }
            else {
                std::cout << "��ȡʧ��: " << bridge.getLastError() << std::endl;
            }
            break;
        }

        case 6: { // ��ȡģ��ƫ��
            std::cout << "\n����ģ�������� game.exe��: ";
            std::string moduleName;
            std::getline(std::cin, moduleName);

            std::cout << "����ƫ�ƣ�ʮ�����ƣ�: 0x";
            std::string offsetStr;
            std::cin >> offsetStr;
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

            std::string address = moduleName + "+0x" + offsetStr;
            
            if (bridge.readMemory(address, result)) {
                printSeparator();
                std::cout << "��ȡ�ɹ�:" << std::endl;
                printResult(result);
            }
            else {
                std::cout << "��ȡʧ��: " << bridge.getLastError() << std::endl;
            }
            break;
        }

        case 7: { // д��ģ��ƫ��
            std::cout << "\n����ģ�������� game.exe��: ";
            std::string moduleName;
            std::getline(std::cin, moduleName);

            std::cout << "����ƫ�ƣ�ʮ�����ƣ�: 0x";
            std::string offsetStr;
            std::cin >> offsetStr;

            std::cout << "����Ҫд�����ֵ: ";
            std::string value;
            std::cin >> value;
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

            std::string address = moduleName + "+0x" + offsetStr;

            if (bridge.writeMemory(address, value, result)) {
                printSeparator();
                std::cout << "д��ɹ�:" << std::endl;
                printResult(result);
            }
            else {
                std::cout << "д��ʧ��: " << bridge.getLastError() << std::endl;
            }
            break;
        }

        case 8: // ���öϵ�
        case 9: // �Ƴ��ϵ�
        case 10: { // ��ȡ�Ĵ���ֵ
            std::cout << "\n�˹������°汾���ݲ�֧��" << std::endl;
            break;
        }

        case 11: { // �ۺϲ���
            std::cout << "\n=== �ۺϲ��ԣ�ģ���ַ + ���ָ�� ===" << std::endl;
            std::cout << "ʾ������ȡ [[game.exe+0x12AB40]+0x18]+0x20 ��ֵ\n" << std::endl;

            std::cout << "����ģ����: ";
            std::string moduleName;
            std::getline(std::cin, moduleName);

            std::cout << "����ģ��ƫ�ƣ�ʮ�����ƣ�: 0x";
            std::string moduleOffsetStr;
            std::cin >> moduleOffsetStr;
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

            std::string baseStr = moduleName + "+0x" + moduleOffsetStr;

            std::vector<std::string> offsets;
            std::cout << "����ƫ�Ʋ���: ";
            int layers;
            if (!(std::cin >> layers)) {
                std::cin.clear();
                std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                std::cout << "����������Ч\n";
                break;
            }
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

            for (int i = 0; i < layers; ++i) {
                std::cout << "��" << (i + 1) << "��ƫ�ƣ�ʮ�����ƣ�: 0x";
                std::string offsetStr;
                std::cin >> offsetStr;
                offsets.push_back(offsetStr);
            }
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

            std::cout << "\n����ִ�ж��ָ���ȡ..." << std::endl;
            if (bridge.readPointer(baseStr, offsets, result)) {
                printSeparator();
                std::cout << "�ۺϲ��Գɹ�!" << std::endl;
                std::cout << "����·��: [[" << baseStr << "]";
                for (const auto& offset : offsets) {
                    std::cout << "+0x" << offset;
                }
                std::cout << "]" << std::endl;
                printResult(result);
            }
            else {
                std::cout << "����ʧ��: " << bridge.getLastError() << std::endl;
            }
            break;
        }

        default:
            std::cout << "��Чѡ�������ѡ��" << std::endl;
            break;
        }

        std::cout << "\n���س�������...";
        std::cin.get();
    }

    // ֹͣ�Žӷ���
    std::cout << "\n�Ƿ�ֹͣ Lua �Žӷ���(y/n): ";
    char stopChoice;
    std::cin >> stopChoice;

    if (stopChoice == 'y' || stopChoice == 'Y') {
        if (bridge.sendStopSignal()) {
            std::cout << "ֹͣ�ź��ѷ���" << std::endl;
        }
        else {
            std::cout << "����ֹͣ�ź�ʧ��" << std::endl;
        }
    }

    std::cout << "\n�������˳�" << std::endl;
    return 0;
}

// ���ɷ������Լ��е�
// ���ߣ�Lun. github:Lun-OS  QQ:1596534228
