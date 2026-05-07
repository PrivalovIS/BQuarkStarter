#include <windows.h>
#include <commctrl.h>
#include <cwctype>
#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include "resource.h"

#pragma comment(lib, "Comctl32.lib")

namespace
{
    const wchar_t kWindowClassName[] = L"BQuarkStarterWindow";
    const int kMaxConcentrators = 15;

    enum ControlId
    {
        IdIp = 1001,
        IdPort,
        IdConcentratorSuffix,
        IdAdd,
        IdRemove,
        IdConcentrators,
        IdPlcFrom,
        IdPlcTo,
        IdStart,
        IdStatus
    };

    HFONT g_font = NULL;
    HFONT g_headerFont = NULL;
    HBRUSH g_backgroundBrush = NULL;

    struct StartSettings
    {
        std::wstring ip;
        int port;
        int plcFrom;
        int plcTo;
        std::vector<std::wstring> concentrators;
    };

    typedef std::map<std::wstring, std::wstring> MeterLineMap;

    HWND CreateChild(HWND parent, const wchar_t* className, const wchar_t* text, DWORD style,
        int x, int y, int width, int height, int id)
    {
        HWND hwnd = CreateWindowExW(
            0,
            className,
            text,
            WS_CHILD | WS_VISIBLE | style,
            x,
            y,
            width,
            height,
            parent,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
            GetModuleHandleW(NULL),
            NULL);

        if (hwnd && g_font)
        {
            SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
        }

        return hwnd;
    }

    HWND CreateGroup(HWND parent, const wchar_t* text, int x, int y, int width, int height)
    {
        return CreateChild(parent, L"BUTTON", text, BS_GROUPBOX, x, y, width, height, 0);
    }

    HWND CreateLabel(HWND parent, const wchar_t* text, int x, int y, int width, int height)
    {
        return CreateChild(parent, L"STATIC", text, SS_LEFT, x, y, width, height, 0);
    }

    HWND CreateCenterLabel(HWND parent, const wchar_t* text, int x, int y, int width, int height)
    {
        return CreateChild(parent, L"STATIC", text, SS_CENTER, x, y, width, height, 0);
    }

    HWND CreateEdit(HWND parent, const wchar_t* text, int x, int y, int width, int height, int id, DWORD extraStyle = 0)
    {
        return CreateChild(parent, L"EDIT", text,
            WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL | extraStyle,
            x, y, width, height, id);
    }

    HWND CreateButton(HWND parent, const wchar_t* text, int x, int y, int width, int height, int id)
    {
        return CreateChild(parent, L"BUTTON", text,
            WS_TABSTOP | BS_PUSHBUTTON,
            x, y, width, height, id);
    }

    void SetStatus(HWND hwnd, const wchar_t* text)
    {
        HWND status = GetDlgItem(hwnd, IdStatus);
        if (status)
        {
            SendMessageW(status, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(text));
        }
    }

    std::wstring GetWindowTextString(HWND hwnd, int id)
    {
        HWND control = GetDlgItem(hwnd, id);
        int length = GetWindowTextLengthW(control);
        std::wstring text(static_cast<size_t>(length + 1), L'\0');
        if (length > 0)
        {
            GetWindowTextW(control, &text[0], length + 1);
        }

        text.resize(static_cast<size_t>(length));
        return text;
    }

    bool ParseNumber(const std::wstring& text, int minValue, int maxValue, int* value)
    {
        if (text.empty())
        {
            return false;
        }

        int result = 0;
        for (size_t i = 0; i < text.size(); ++i)
        {
            if (text[i] < L'0' || text[i] > L'9')
            {
                return false;
            }

            result = result * 10 + (text[i] - L'0');
            if (result > maxValue)
            {
                return false;
            }
        }

        if (result < minValue || result > maxValue)
        {
            return false;
        }

        *value = result;
        return true;
    }

    bool IsDigitOrLatin(wchar_t ch)
    {
        return (ch >= L'0' && ch <= L'9') ||
            (ch >= L'A' && ch <= L'Z') ||
            (ch >= L'a' && ch <= L'z');
    }

    wchar_t ToUpperLatin(wchar_t ch)
    {
        if (ch >= L'a' && ch <= L'z')
        {
            return static_cast<wchar_t>(ch - L'a' + L'A');
        }

        return ch;
    }

    bool ReadConcentrator(HWND hwnd, wchar_t* value, int valueLength)
    {
        if (valueLength < 5)
        {
            return false;
        }

        wchar_t suffix[4] = {};
        GetDlgItemTextW(hwnd, IdConcentratorSuffix, suffix, 4);

        if (wcslen(suffix) != 3)
        {
            SetStatus(hwnd, L"Введите 3 символа после первой цифры 2");
            SetFocus(GetDlgItem(hwnd, IdConcentratorSuffix));
            return false;
        }

        value[0] = L'2';
        for (int i = 0; i < 3; ++i)
        {
            if (!IsDigitOrLatin(suffix[i]))
            {
                SetStatus(hwnd, L"В адресе концентратора разрешены только цифры и латинские буквы");
                SetFocus(GetDlgItem(hwnd, IdConcentratorSuffix));
                return false;
            }

            value[i + 1] = ToUpperLatin(suffix[i]);
        }

        value[4] = L'\0';
        return true;
    }

    bool ListContains(HWND listBox, const wchar_t* value)
    {
        int count = static_cast<int>(SendMessageW(listBox, LB_GETCOUNT, 0, 0));
        for (int i = 0; i < count; ++i)
        {
            wchar_t item[16] = {};
            SendMessageW(listBox, LB_GETTEXT, i, reinterpret_cast<LPARAM>(item));
            if (lstrcmpiW(item, value) == 0)
            {
                return true;
            }
        }

        return false;
    }

    void ClearConcentratorInput(HWND hwnd)
    {
        HWND edit = GetDlgItem(hwnd, IdConcentratorSuffix);
        SetWindowTextW(edit, L"");
        SetFocus(edit);
    }

    void AddConcentrator(HWND hwnd)
    {
        HWND listBox = GetDlgItem(hwnd, IdConcentrators);
        int count = static_cast<int>(SendMessageW(listBox, LB_GETCOUNT, 0, 0));
        if (count >= kMaxConcentrators)
        {
            SetStatus(hwnd, L"Достигнут максимум: 15 концентраторов");
            SetFocus(GetDlgItem(hwnd, IdConcentratorSuffix));
            return;
        }

        wchar_t value[5] = {};
        if (!ReadConcentrator(hwnd, value, 5))
        {
            return;
        }

        if (ListContains(listBox, value))
        {
            SetStatus(hwnd, L"Дубликат концентратора");
            SetFocus(GetDlgItem(hwnd, IdConcentratorSuffix));
            return;
        }

        SendMessageW(listBox, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value));
        ClearConcentratorInput(hwnd);

        wchar_t statusText[80] = {};
        wsprintfW(statusText, L"Концентратор %s добавлен", value);
        SetStatus(hwnd, statusText);
    }

    void RemoveSelectedConcentrator(HWND hwnd)
    {
        HWND listBox = GetDlgItem(hwnd, IdConcentrators);
        int selected = static_cast<int>(SendMessageW(listBox, LB_GETCURSEL, 0, 0));
        if (selected == LB_ERR)
        {
            SetStatus(hwnd, L"Концентратор не выбран");
            return;
        }

        wchar_t value[16] = {};
        SendMessageW(listBox, LB_GETTEXT, selected, reinterpret_cast<LPARAM>(value));
        SendMessageW(listBox, LB_DELETESTRING, selected, 0);

        int count = static_cast<int>(SendMessageW(listBox, LB_GETCOUNT, 0, 0));
        if (count > 0)
        {
            int next = selected < count ? selected : count - 1;
            SendMessageW(listBox, LB_SETCURSEL, next, 0);
        }

        wchar_t statusText[80] = {};
        wsprintfW(statusText, L"Концентратор %s удален", value);
        SetStatus(hwnd, statusText);
    }

    void AddDefaultConcentrators(HWND listBox)
    {
        SendMessageW(listBox, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"2101"));
        SendMessageW(listBox, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"2102"));
        SendMessageW(listBox, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"2103"));
    }

    bool IsValidConcentratorAddress(const std::wstring& value)
    {
        if (value.size() != 4 || value[0] != L'2')
        {
            return false;
        }

        for (size_t i = 1; i < value.size(); ++i)
        {
            if (!IsDigitOrLatin(value[i]))
            {
                return false;
            }
        }

        return true;
    }

    std::wstring MakeMeterKey(const std::wstring& concentrator, int address)
    {
        wchar_t buffer[32] = {};
        wsprintfW(buffer, L"%s|%d", concentrator.c_str(), address);
        return buffer;
    }

    std::wstring GetExeDirectory()
    {
        wchar_t path[MAX_PATH] = {};
        GetModuleFileNameW(NULL, path, MAX_PATH);
        wchar_t* slash = wcsrchr(path, L'\\');
        if (slash)
        {
            *slash = L'\0';
        }

        return path;
    }

    std::wstring CombinePath(const std::wstring& directory, const std::wstring& name)
    {
        if (directory.empty())
        {
            return name;
        }

        if (directory[directory.size() - 1] == L'\\')
        {
            return directory + name;
        }

        return directory + L"\\" + name;
    }

    std::wstring GetDirectoryName(const std::wstring& path)
    {
        size_t slash = path.find_last_of(L"\\/");
        if (slash == std::wstring::npos)
        {
            return L".";
        }

        return path.substr(0, slash);
    }

    bool FileExists(const std::wstring& path)
    {
        DWORD attributes = GetFileAttributesW(path.c_str());
        return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
    }

    std::wstring ToWideAnsi(const std::string& text)
    {
        if (text.empty())
        {
            return L"";
        }

        int length = MultiByteToWideChar(CP_ACP, 0, text.c_str(), static_cast<int>(text.size()), NULL, 0);
        std::wstring result(static_cast<size_t>(length), L'\0');
        MultiByteToWideChar(CP_ACP, 0, text.c_str(), static_cast<int>(text.size()), &result[0], length);
        return result;
    }

    std::string ToAnsi(const std::wstring& text)
    {
        if (text.empty())
        {
            return "";
        }

        int length = WideCharToMultiByte(CP_ACP, 0, text.c_str(), static_cast<int>(text.size()), NULL, 0, NULL, NULL);
        std::string result(static_cast<size_t>(length), '\0');
        WideCharToMultiByte(CP_ACP, 0, text.c_str(), static_cast<int>(text.size()), &result[0], length, NULL, NULL);
        return result;
    }

    std::wstring GetParameter(const std::wstring& line, const std::wstring& name)
    {
        std::wstring marker = name + L"=";
        size_t start = line.find(marker);
        if (start == std::wstring::npos)
        {
            return L"";
        }

        start += marker.size();
        size_t end = line.find(L';', start);
        if (end == std::wstring::npos)
        {
            end = line.size();
        }

        return line.substr(start, end - start);
    }

    void LoadExistingMeterLines(const std::wstring& startPath, MeterLineMap* meters)
    {
        meters->clear();

        std::ifstream file(startPath.c_str(), std::ios::binary);
        if (!file)
        {
            return;
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        std::wstring content = ToWideAnsi(buffer.str());
        std::wistringstream stream(content);

        std::wstring currentConcentrator;
        std::wstring line;
        while (std::getline(stream, line))
        {
            if (!line.empty() && line[line.size() - 1] == L'\r')
            {
                line.erase(line.size() - 1);
            }

            if (line.find(L"TYPE=PLC_I_CONCENTRATOR") != std::wstring::npos)
            {
                currentConcentrator = GetParameter(line, L"ADDR");
                std::transform(currentConcentrator.begin(), currentConcentrator.end(), currentConcentrator.begin(), ToUpperLatin);
                continue;
            }

            if (line.find(L"TYPE=PLC_I_METER") != std::wstring::npos && !currentConcentrator.empty())
            {
                std::wstring addressText = GetParameter(line, L"ADDR");
                int address = 0;
                if (ParseNumber(addressText, 1, 1024, &address))
                {
                    (*meters)[MakeMeterKey(currentConcentrator, address)] = line;
                }
            }
        }
    }

    bool LoadSettingsFromStartDat(HWND hwnd)
    {
        std::wstring startPath = CombinePath(GetExeDirectory(), L"START.dat");
        std::ifstream file(startPath.c_str(), std::ios::binary);
        if (!file)
        {
            SetStatus(hwnd, L"START.dat не найден, используются настройки по умолчанию");
            return false;
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        std::wstring content = ToWideAnsi(buffer.str());
        std::wistringstream stream(content);

        StartSettings settings;
        settings.port = 0;
        settings.plcFrom = 1025;
        settings.plcTo = 0;

        std::wstring line;
        while (std::getline(stream, line))
        {
            if (!line.empty() && line[line.size() - 1] == L'\r')
            {
                line.erase(line.size() - 1);
            }

            if (line.find(L"TYPE=GPRS/TCP_MODEM") != std::wstring::npos)
            {
                settings.ip = GetParameter(line, L"IP");
                std::wstring portText = GetParameter(line, L"PORT");
                ParseNumber(portText, 1, 65535, &settings.port);
                continue;
            }

            if (line.find(L"TYPE=PLC_I_CONCENTRATOR") != std::wstring::npos)
            {
                std::wstring address = GetParameter(line, L"ADDR");
                std::transform(address.begin(), address.end(), address.begin(), ToUpperLatin);

                if (IsValidConcentratorAddress(address) &&
                    settings.concentrators.size() < kMaxConcentrators &&
                    std::find(settings.concentrators.begin(), settings.concentrators.end(), address) == settings.concentrators.end())
                {
                    settings.concentrators.push_back(address);
                }
                continue;
            }

            if (line.find(L"TYPE=PLC_I_METER") != std::wstring::npos)
            {
                int address = 0;
                if (ParseNumber(GetParameter(line, L"ADDR"), 1, 1024, &address))
                {
                    if (address < settings.plcFrom)
                    {
                        settings.plcFrom = address;
                    }

                    if (address > settings.plcTo)
                    {
                        settings.plcTo = address;
                    }
                }
            }
        }

        if (settings.ip.empty() ||
            settings.port < 1 ||
            settings.concentrators.empty() ||
            settings.plcFrom < 1 ||
            settings.plcTo > 1024 ||
            settings.plcFrom > settings.plcTo)
        {
            SetStatus(hwnd, L"START.dat поврежден, используются настройки по умолчанию");
            return false;
        }

        SetWindowTextW(GetDlgItem(hwnd, IdIp), settings.ip.c_str());

        wchar_t numberText[16] = {};
        wsprintfW(numberText, L"%d", settings.port);
        SetWindowTextW(GetDlgItem(hwnd, IdPort), numberText);

        HWND listBox = GetDlgItem(hwnd, IdConcentrators);
        SendMessageW(listBox, LB_RESETCONTENT, 0, 0);
        for (size_t i = 0; i < settings.concentrators.size(); ++i)
        {
            SendMessageW(listBox, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(settings.concentrators[i].c_str()));
        }

        wsprintfW(numberText, L"%d", settings.plcFrom);
        SetWindowTextW(GetDlgItem(hwnd, IdPlcFrom), numberText);

        wsprintfW(numberText, L"%d", settings.plcTo);
        SetWindowTextW(GetDlgItem(hwnd, IdPlcTo), numberText);

        SetStatus(hwnd, L"Настройки загружены из START.dat");
        return true;
    }

    bool CollectSettings(HWND hwnd, StartSettings* settings)
    {
        settings->ip = GetWindowTextString(hwnd, IdIp);
        if (settings->ip.empty())
        {
            SetStatus(hwnd, L"Введите IP адрес");
            SetFocus(GetDlgItem(hwnd, IdIp));
            return false;
        }

        std::wstring portText = GetWindowTextString(hwnd, IdPort);
        if (!ParseNumber(portText, 1, 65535, &settings->port))
        {
            SetStatus(hwnd, L"Порт должен быть числом от 1 до 65535");
            SetFocus(GetDlgItem(hwnd, IdPort));
            return false;
        }

        if (!ParseNumber(GetWindowTextString(hwnd, IdPlcFrom), 1, 1024, &settings->plcFrom))
        {
            SetStatus(hwnd, L"PLC От должен быть числом от 1 до 1024");
            SetFocus(GetDlgItem(hwnd, IdPlcFrom));
            return false;
        }

        if (!ParseNumber(GetWindowTextString(hwnd, IdPlcTo), 1, 1024, &settings->plcTo))
        {
            SetStatus(hwnd, L"PLC До должен быть числом от 1 до 1024");
            SetFocus(GetDlgItem(hwnd, IdPlcTo));
            return false;
        }

        if (settings->plcFrom > settings->plcTo)
        {
            SetStatus(hwnd, L"PLC От не может быть больше PLC До");
            SetFocus(GetDlgItem(hwnd, IdPlcFrom));
            return false;
        }

        HWND listBox = GetDlgItem(hwnd, IdConcentrators);
        int count = static_cast<int>(SendMessageW(listBox, LB_GETCOUNT, 0, 0));
        if (count < 1 || count > kMaxConcentrators)
        {
            SetStatus(hwnd, L"Количество концентраторов должно быть от 1 до 15");
            return false;
        }

        settings->concentrators.clear();
        for (int i = 0; i < count; ++i)
        {
            wchar_t item[16] = {};
            SendMessageW(listBox, LB_GETTEXT, i, reinterpret_cast<LPARAM>(item));
            settings->concentrators.push_back(item);
        }

        return true;
    }

    std::wstring BuildStartDat(const StartSettings& settings, const MeterLineMap& existingMeters)
    {
        std::wostringstream output;
        SYSTEMTIME now;
        GetLocalTime(&now);
        wchar_t dateText[32] = {};
        wsprintfW(dateText, L"%02d.%02d.%04d %02d:%02d:%02d",
            now.wDay, now.wMonth, now.wYear, now.wHour, now.wMinute, now.wSecond);

        output << L"// Файл 'START.dat' от " << dateText << L"\r\n";
        output << L"// Создано программой BQuarkStarter\r\n";
        output << L"// ВНИМАНИЕ! При просмотре и редактировании этого файла перенос по словам должен быть ВЫКЛЮЧЕН!\r\n\r\n";
        output << L"// СЕКЦИЯ ОПИСАНИЯ ОБЪЕКТОВ СИСТЕМЫ СБОРА ДАННЫХ.\r\n";
        output << L"// Уровни иерархии отделяются друг от друга символами табуляции.\r\n\r\n";
        output << L"OBJECTS\r\n";
        output << L"\tTYPE=GPRS/TCP_MODEM; IP=" << settings.ip << L"; PORT=" << settings.port << L"\r\n";

        for (size_t i = 0; i < settings.concentrators.size(); ++i)
        {
            const std::wstring& concentrator = settings.concentrators[i];
            output << L"\t\tTYPE=PLC_I_CONCENTRATOR; ADDR=" << concentrator
                << L"; BAUDRATE=9600; CHILD.HOST=" << concentrator << L"; MAXIDLE=3000\r\n";

            for (int address = settings.plcFrom; address <= settings.plcTo; ++address)
            {
                MeterLineMap::const_iterator existing = existingMeters.find(MakeMeterKey(concentrator, address));
                if (existing != existingMeters.end())
                {
                    output << existing->second << L"\r\n";
                }
                else
                {
                    output << L"\t\t\tTYPE=PLC_I_METER; ADDR=" << address << L"; BASEONLY=YES;\r\n";
                }
            }
        }

        output << L"\r\n";
        output << L"// СЕКЦИЯ ОПИСАНИЯ ИНТЕРФЕЙСА ПРОГРАММЫ.\r\n\r\n";
        output << L"INTERFACE\r\n";
        output << L"\tTYPE=OPTIONS\r\n";
        output << L"\tTYPE=TABLE\r\n";
        output << L"\t\tTYPE=COLUMN; WIDTH=30; TITLE=##; VALUE=NUM\r\n";
        output << L"\t\tTYPE=COLUMN; WIDTH=40; TITLE=Конц.; VALUE=PROPERTY; FILTER=HOST\r\n";
        output << L"\t\tTYPE=COLUMN; WIDTH=40; TITLE=PLC; VALUE=PROPERTY; FILTER=ADDR\r\n";
        output << L"\t\tTYPE=COLUMN; WIDTH=200; TITLE=Сумма. кВт*ч; VALUE=BINDATA; FILTER=SUM/00:00\r\n\r\n";
        output << L"\t\t\r\n";
        output << L"// СЕКЦИЯ ПРОТОКОЛА\r\n\r\n";
        output << L"LOG\r\n";
        output << L"\t\t\r\n";
        return output.str();
    }

    bool WriteTextFileAnsi(const std::wstring& path, const std::wstring& content)
    {
        std::ofstream file(path.c_str(), std::ios::binary | std::ios::trunc);
        if (!file)
        {
            return false;
        }

        std::string ansi = ToAnsi(content);
        file.write(ansi.c_str(), static_cast<std::streamsize>(ansi.size()));
        return file.good();
    }

    std::wstring FindBQuarkExe(const std::wstring& baseDirectory)
    {
        std::wstring direct = CombinePath(baseDirectory, L"BQuark.Rev.3.3.exe");
        if (FileExists(direct))
        {
            return direct;
        }

        std::wstring nested = CombinePath(CombinePath(baseDirectory, L"BQuark.Rev.3.3"), L"BQuark.Rev.3.3.exe");
        if (FileExists(nested))
        {
            return nested;
        }

        return L"";
    }

    void StartBQuark(HWND hwnd)
    {
        StartSettings settings;
        if (!CollectSettings(hwnd, &settings))
        {
            return;
        }

        std::wstring baseDirectory = GetExeDirectory();
        std::wstring startPath = CombinePath(baseDirectory, L"START.dat");
        MeterLineMap existingMeters;
        LoadExistingMeterLines(startPath, &existingMeters);

        std::wstring content = BuildStartDat(settings, existingMeters);
        if (!WriteTextFileAnsi(startPath, content))
        {
            SetStatus(hwnd, L"Не удалось записать START.dat");
            return;
        }

        std::wstring bquarkPath = FindBQuarkExe(baseDirectory);
        if (bquarkPath.empty())
        {
            SetStatus(hwnd, L"Не найден BQuark.Rev.3.3.exe");
            return;
        }

        STARTUPINFOW startupInfo;
        PROCESS_INFORMATION processInfo;
        ZeroMemory(&startupInfo, sizeof(startupInfo));
        ZeroMemory(&processInfo, sizeof(processInfo));
        startupInfo.cb = sizeof(startupInfo);

        std::wstring commandLine = L"\"" + bquarkPath + L"\"";
        if (!CreateProcessW(
            bquarkPath.c_str(),
            &commandLine[0],
            NULL,
            NULL,
            FALSE,
            0,
            NULL,
            baseDirectory.c_str(),
            &startupInfo,
            &processInfo))
        {
            SetStatus(hwnd, L"Не удалось запустить BQuark.Rev.3.3.exe");
            return;
        }

        CloseHandle(processInfo.hThread);
        CloseHandle(processInfo.hProcess);
        PostQuitMessage(0);
    }

    void CreateMainControls(HWND hwnd)
    {
        HWND header = CreateLabel(hwnd, L"Настройки запуска BQuark", 20, 12, 350, 24);
        if (header && g_headerFont)
        {
            SendMessageW(header, WM_SETFONT, reinterpret_cast<WPARAM>(g_headerFont), TRUE);
        }

        CreateGroup(hwnd, L"Подключение", 20, 42, 444, 64);
        CreateLabel(hwnd, L"IP:", 38, 68, 32, 20);
        CreateEdit(hwnd, L"192.168.1.75", 72, 64, 156, 23, IdIp);
        CreateLabel(hwnd, L"Порт:", 264, 68, 46, 20);
        CreateEdit(hwnd, L"30000", 312, 64, 104, 23, IdPort);

        CreateGroup(hwnd, L"Концентраторы", 20, 114, 226, 342);
        CreateLabel(hwnd, L"2", 40, 146, 12, 20);
        HWND concentratorEdit = CreateEdit(hwnd, L"", 54, 142, 86, 23, IdConcentratorSuffix, ES_UPPERCASE);
        SendMessageW(concentratorEdit, EM_LIMITTEXT, 3, 0);
        CreateButton(hwnd, L"Добавить", 150, 140, 78, 27, IdAdd);
        HWND listBox = CreateChild(hwnd, L"LISTBOX", L"",
            WS_TABSTOP | WS_BORDER | LBS_NOTIFY,
            40, 176, 188, 249, IdConcentrators);
        AddDefaultConcentrators(listBox);
        CreateButton(hwnd, L"Удалить", 150, 424, 78, 27, IdRemove);

        CreateGroup(hwnd, L"Диапазон PLC адресов", 258, 114, 206, 90);
        CreateLabel(hwnd, L"От:", 278, 145, 32, 20);
        CreateEdit(hwnd, L"1", 316, 141, 90, 23, IdPlcFrom);
        CreateLabel(hwnd, L"До:", 278, 176, 32, 20);
        CreateEdit(hwnd, L"16", 316, 172, 90, 23, IdPlcTo);

        HWND start = CreateButton(hwnd, L"Старт", 293, 286, 136, 40, IdStart);
        if (start && g_headerFont)
        {
            SendMessageW(start, WM_SETFONT, reinterpret_cast<WPARAM>(g_headerFont), TRUE);
        }

        CreateCenterLabel(hwnd, L"Иван Привалов\r\nБирск 2026", 275, 342, 172, 40);

        CreateChild(hwnd, STATUSCLASSNAMEW, L"Готово", SBARS_SIZEGRIP, 0, 0, 0, 0, IdStatus);
    }

    LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
        case WM_CREATE:
            CreateMainControls(hwnd);
            LoadSettingsFromStartDat(hwnd);
            return 0;

        case WM_COMMAND:
            switch (LOWORD(wParam))
            {
            case IdAdd:
                if (HIWORD(wParam) == BN_CLICKED)
                {
                    AddConcentrator(hwnd);
                    return 0;
                }
                break;

            case IdRemove:
                if (HIWORD(wParam) == BN_CLICKED)
                {
                    RemoveSelectedConcentrator(hwnd);
                    return 0;
                }
                break;

            case IdConcentratorSuffix:
                if (HIWORD(wParam) == EN_MAXTEXT)
                {
                    SetStatus(hwnd, L"Адрес концентратора: 2 + 3 символа");
                    return 0;
                }
                break;

            case IdStart:
                if (HIWORD(wParam) == BN_CLICKED)
                {
                    StartBQuark(hwnd);
                    return 0;
                }
                break;
            }
            return 0;

        case WM_CTLCOLORDLG:
            return reinterpret_cast<LRESULT>(g_backgroundBrush);

        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN:
        {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetBkColor(hdc, RGB(236, 247, 238));
            SetTextColor(hdc, RGB(28, 51, 35));
            return reinterpret_cast<LRESULT>(g_backgroundBrush);
        }

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        default:
            return DefWindowProcW(hwnd, message, wParam, lParam);
        }
    }

    void CreateFonts()
    {
        g_font = CreateFontW(
            -13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        g_headerFont = CreateFontW(
            -18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    }

    bool HandleKeyDown(HWND hwnd, WPARAM key)
    {
        HWND focused = GetFocus();

        if (key == VK_RETURN && focused == GetDlgItem(hwnd, IdConcentratorSuffix))
        {
            AddConcentrator(hwnd);
            return true;
        }

        if (key == VK_DELETE && focused == GetDlgItem(hwnd, IdConcentrators))
        {
            RemoveSelectedConcentrator(hwnd);
            return true;
        }

        return false;
    }
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
    INITCOMMONCONTROLSEX commonControls;
    commonControls.dwSize = sizeof(commonControls);
    commonControls.dwICC = ICC_BAR_CLASSES;
    InitCommonControlsEx(&commonControls);

    CreateFonts();
    g_backgroundBrush = CreateSolidBrush(RGB(236, 247, 238));

    WNDCLASSEXW windowClass;
    ZeroMemory(&windowClass, sizeof(windowClass));
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_START_ICON));
    windowClass.hCursor = LoadCursorW(NULL, IDC_ARROW);
    windowClass.hbrBackground = g_backgroundBrush;
    windowClass.lpszClassName = kWindowClassName;
    windowClass.hIconSm = LoadIconW(instance, MAKEINTRESOURCEW(IDI_START_ICON));

    if (!RegisterClassExW(&windowClass))
    {
        return 1;
    }

    HWND hwnd = CreateWindowExW(
        0,
        kWindowClassName,
        L"BQuark Starter",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        492,
        520,
        NULL,
        NULL,
        instance,
        NULL);

    if (!hwnd)
    {
        return 1;
    }

    ShowWindow(hwnd, showCommand);
    UpdateWindow(hwnd);

    MSG message;
    while (GetMessageW(&message, NULL, 0, 0) > 0)
    {
        if (message.message == WM_KEYDOWN && HandleKeyDown(hwnd, message.wParam))
        {
            continue;
        }

        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    if (g_font)
    {
        DeleteObject(g_font);
    }

    if (g_headerFont)
    {
        DeleteObject(g_headerFont);
    }

    if (g_backgroundBrush)
    {
        DeleteObject(g_backgroundBrush);
    }

    return static_cast<int>(message.wParam);
}
