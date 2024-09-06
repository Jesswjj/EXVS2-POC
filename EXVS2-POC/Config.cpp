#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "Configs.h"
#include "INIReader.h"
#include "log.h"
#include "Version.h"

using namespace std::chrono_literals;

StartupConfig globalConfig;

static std::vector<std::string> Split(const std::string& string, char delimiter)
{
    std::string_view str = string;
    std::vector<std::string> result;

    while (true)
    {
        auto it = std::find(str.begin(), str.end(), delimiter);
        result.emplace_back(str.begin(), it);

        if (it == str.end())
            break;

        str = str.substr(result.back().size() + 1);
    }

    return result;
}

static void ReadStartupConfig(StartupConfig* config, INIReader& reader)
{
    // config reading
    std::string logLevel = reader.Get("config", "log", "none");
    if (_stricmp("trace", logLevel.c_str()) == 0)
    {
        g_logLevel = LogLevel::TRACE;
    }
    else if (_stricmp("debug", logLevel.c_str()) == 0)
    {
        g_logLevel = LogLevel::DEBUG;
    }
    else if (_stricmp("info", logLevel.c_str()) == 0)
    {
        g_logLevel = LogLevel::INFO;
    }
    else if (_stricmp("warn", logLevel.c_str()) == 0)
    {
        g_logLevel = LogLevel::WARN;
    }
    else if (_stricmp("error", logLevel.c_str()) == 0)
    {
        g_logLevel = LogLevel::ERR;
    }
    else if (_stricmp("none", logLevel.c_str()) == 0)
    {
        g_logLevel = LogLevel::NONE;
    }
    else
    {
        fatal("Unknown log level '%s'", logLevel.c_str());
    }
    if (g_logLevel != LogLevel::NONE)
    {
        AllocConsole();

        FILE* dummy;
        freopen_s(&dummy, "CONIN$", "r", stdin);
        freopen_s(&dummy, "CONOUT$", "w", stderr);
        freopen_s(&dummy, "CONOUT$", "w", stdout);
    }

    config->Windowed = reader.GetBoolean("config", "windowed", false);
    config->BorderlessWindow = reader.GetBoolean("config", "borderlesswindow", false);
    config->EnableInGamePerformanceMeter = reader.GetBoolean("config", "enableingameperformancemeter", false);
    config->DisableSocketHook = reader.GetBoolean("config", "disablesockethook", false);

    std::string modeString = reader.Get("config", "mode", "LM");
    if (modeString == "1" || _stricmp("client", modeString.c_str()) == 0)
    {
        config->Mode = 1;
    }
    else if (modeString == "2" || _stricmp("lm", modeString.c_str()) == 0)
    {
        config->Mode = 2;
    }
    else
    {
        fatal("invalid mode: %s", modeString.c_str());
    }

    config->Serial = reader.Get("config", "serial", "0001");
    if (config->Serial.size() != 4 && config->Serial.size() != 12)
    {
        fatal("invalid serial: expected 4 or 12 digit serial number");
    }
    
    const char* serialPrefixLM = nullptr;
    const char* serialPrefixClient = nullptr;
    const char* serialPrefixClientAlter = nullptr;
    const char* pcbIdPrefix = nullptr;

    switch (GetGameVersion()) {
        case VS2_400:
            serialPrefixLM = "28111101";
            serialPrefixClient = "28111401";
            serialPrefixClientAlter = "28111301";
            pcbIdPrefix = "ABLN";
            break;
        case XBoost_450:
            serialPrefixLM = "28431111";
            serialPrefixClient = "28431411";
            serialPrefixClientAlter = "28431311";
            pcbIdPrefix = "ABLN";
            break;
        default:
            fatal("Unknown game version: %d", GetGameVersion());
    }

    if (config->Serial.size() == 4)
    {
        config->Serial = (config->Mode == 1 ? serialPrefixClient : serialPrefixLM) + config->Serial;
    }

    bool validLength = config->Serial.size() == 12;
    bool validClientPrefix =
        config->Serial.starts_with(serialPrefixClient) || config->Serial.starts_with(serialPrefixClientAlter);
    bool validLMPrefix = config->Serial.starts_with(serialPrefixLM);
    if (config->Mode == 1 && (!validLength || !validClientPrefix))
    {
        fatal("invalid serial: expected serial of format %sXXXX/%sXXXX for client", serialPrefixClient,
              serialPrefixClientAlter);
    }
    else if (config->Mode == 2 && (!validLength || !validLMPrefix))
    {
        fatal("invalid serial: expected serial of format %sXXXX for LM", serialPrefixLM);
    }

    config->PcbId = reader.GetOptional("config", "PcbId").value_or(pcbIdPrefix + config->Serial.substr(5));

    // These will get filled in by InitializeSocketHooks.
    config->InterfaceName = reader.GetOptional("config", "InterfaceName");
    config->IpAddress = reader.GetOptional("config", "IpAddress");
    config->Gateway = reader.GetOptional("config", "Gateway");
    config->TenpoRouter = reader.GetOptional("config", "TenpoRouter");
    config->SubnetMask = reader.GetOptional("config", "SubnetMask");
    config->PrimaryDNS = reader.GetOptional("config", "DNS");

    config->AuthServerIp = reader.Get("config", "AuthIP", "127.0.0.1");
    config->ServerAddress = reader.Get("config", "Server", "127.0.0.1");
    config->RegionCode = reader.Get("config", "Region", "1");

    if(config->RegionCode.empty())
    {
        config->RegionCode = "1"; 
    }

    try
    {
        auto regionCode = std::stod(config->RegionCode);

        if(regionCode > 47 || regionCode <= 0)
        {
            config->RegionCode = "1";
        }
    }
    catch(std::invalid_argument &e) {
        config->RegionCode = "1";
    }
    
    config->UseRealCardReader = reader.GetBoolean("config", "userealcardreader", false);
    config->CardReaderComPort = reader.Get("config", "cardreadercomport", "COM4");
    
    if(config->UseRealCardReader == true && config->CardReaderComPort == "COM3")
    {
        fatal("COM3 is reserved for Controller and cannot be used as Card Reader COM Port");
    }

    config->Audio.DisableHook = reader.GetBoolean("audio", "DisableHook", false);

    config->Audio.DeviceId = reader.GetOptional("audio", "DeviceId");
    if (!config->Audio.DeviceId)
    {
        config->Audio.DeviceId = reader.GetOptional("audio", "Device");
    }

    config->Audio.DeviceName = reader.GetOptional("audio", "DeviceName");

    config->Display.Resolution = reader.Get("display", "resolution", "1080p");

    if(config->Display.Resolution != "144p" && config->Display.Resolution != "240p"
        && config->Display.Resolution != "480p" && config->Display.Resolution != "720p"
        && config->Display.Resolution != "1080p" && config->Display.Resolution != "2k"
        && config->Display.Resolution != "4k" && config->Display.Resolution != "8k")
    {
        fatal("Unsupported Resolution Setting %s", config->Display.Resolution.c_str());
    }

    auto isBorderless = config->Windowed == true && config->BorderlessWindow == true;

    if(config->Display.Resolution == "8k" && isBorderless == false)
    {
        fatal("%s is supported in Borderless Window mode only!", config->Display.Resolution.c_str());
    }

    config->OpeningScreenSkip = reader.Get("config", "OpeningScreenSkip", "SkipReminder");

    if(config->OpeningScreenSkip != "None" && config->OpeningScreenSkip != "SkipReminder" &&
        config->OpeningScreenSkip != "SkipBrand" && config->OpeningScreenSkip != "SkipAll")
    {
        fatal("%s is an unsupported Opening Screen Skip Mode", config->OpeningScreenSkip.c_str());
    }
}

static void ReadInputConfig(InputConfig* config, INIReader& reader)
{
    // key bind config reading
    KeyBinds keyboard;
#define KEYBIND(name, kb_default, dinput_default)                                                                      \
    {                                                                                                                  \
        std::vector<std::string> keys = Split(reader.Get("keyboard", #name, kb_default), ',');                         \
        for (const auto& key : keys)                                                                                   \
        {                                                                                                              \
            int val = findKeyByValue(key);                                                                             \
            if (val != -1)                                                                                             \
                keyboard.name.push_back(val);                                                                          \
            else                                                                                                       \
                fatal("failed to interpret key '%s'", key.c_str());                                                    \
        }                                                                                                              \
    }
    KEYBINDS()
#undef KEYBIND

    KeyBinds dinput;
#define KEYBIND(name, kb_default, dinput_default)                                                                      \
    {                                                                                                                  \
        std::vector<std::string> keys = Split(reader.Get("controller", #name, dinput_default), ',');                   \
        for (const auto& key : keys)                                                                                   \
        {                                                                                                              \
            dinput.name.push_back(atoi(key.c_str()));                                                                  \
        }                                                                                                              \
    }
    KEYBINDS()
#undef KEYBIND

    config->KeyboardEnabled = reader.GetBoolean("keyboard", "Enabled", true);
    config->KeyboardBindings = keyboard;

    config->ControllerEnabled = reader.GetBoolean("controller", "Enabled", true);
    // TODO: This should take a GUID instead of an index.
    config->ControllerDeviceId = reader.GetInteger("controller", "DeviceId", 16);
    config->ControllerPath = reader.GetOptional("controller", "Path");
    config->ControllerBindings = dinput;
}

static std::string Join(const std::string& delimiter, const std::vector<int>& vec)
{
    std::string result;
    for (size_t i = 0; i < vec.size(); ++i)
    {
        if (i != 0)
            result += ", ";
        result += std::to_string(vec[i]);
    }
    return result;
}

std::string KeyBinds::Dump(const std::string& prefix)
{
    std::string result;
#define KEYBIND(name, keyboard, dinput)                                                                                \
    if (!name.empty())                                                                                                 \
    {                                                                                                                  \
        result += prefix;                                                                                              \
        result += #name;                                                                                               \
        result += " = ";                                                                                               \
        result += Join(", ", name);                                                                                    \
        result += "\n";                                                                                                \
    }
    KEYBINDS();
    return result;
}

std::string InputConfig::Dump(const std::string& prefix)
{
    std::string result;
    result += prefix + "[keyboard]\n";

    result += prefix + "Enabled = ";
    result += KeyboardEnabled ? "true" : "false";
    result += "\n";

    result += KeyboardBindings.Dump(prefix);
    result += "\n";

    result += "\n";

    result += prefix;
    result += "[controller]\n";

    result += prefix + "Enabled = ";
    result += ControllerEnabled ? "true" : "false";
    result += "\n";

    if (ControllerPath)
    {
        result += prefix;
        result += "Path = ";
        result += *ControllerPath;
        result += "\n";
    }

    result += prefix;
    result += "DeviceId = ";
    result += std::to_string(ControllerDeviceId);
    result += "\n";

    result += ControllerBindings.Dump(prefix);
    return result;
}

static void ConfigMonitorThread()
{
    std::filesystem::path basePath = GetBasePath();
    std::string configPath = (basePath / "config.ini").string();

    HANDLE dir = CreateFileW(basePath.wstring().c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (dir == INVALID_HANDLE_VALUE)
        fatal("failed to open handle to %s", basePath.string().c_str());

    while (true)
    {
        // Sleep for a bit, to give the writer a chance to finish.
        std::this_thread::sleep_for(100ms);
        INIReader reader(configPath);

        InputConfig inputConfig;

        ReadInputConfig(&inputConfig, reader);
        UpdateInputConfig(std::move(inputConfig));

        union {
            char bytes[4096];
            DWORD align;
        } buf;
        DWORD bytesReturned = 0;

        bool configChanged = false;
        while (!configChanged)
        {
            if (!ReadDirectoryChangesW(dir, buf.bytes, sizeof(buf), false, FILE_NOTIFY_CHANGE_LAST_WRITE,
                                       &bytesReturned, nullptr, nullptr))
            {
                err("ReadDirectoryChangesW failed, stopping config monitor thread");
                return;
            }

            if (bytesReturned == 0)
                continue;

            char* p = buf.bytes;
            auto* info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(p);
            while (true)
            {
                if (info->Action == FILE_ACTION_ADDED || info->Action == FILE_ACTION_MODIFIED ||
                    info->Action == FILE_ACTION_RENAMED_NEW_NAME)
                {
                    std::wstring filename(info->FileName, info->FileNameLength / 2);
                    if (_wcsicmp(L"config.ini", filename.c_str()) == 0)
                    {
                        info("Detected change to config.ini");
                        configChanged = true;
                        break;
                    }
                    else
                    {
                        info("Ignoring changed file: '%S'", filename.c_str());
                    }
                }

                if (info->NextEntryOffset == 0)
                    break;
                p += info->NextEntryOffset;
                info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(p);
            }
        }
    }
}

void InitializeConfig()
{
    // FIXME: INIReader doesn't handle Unicode paths.
    std::filesystem::path basePath = GetBasePath();
    INIReader reader((basePath / "config.ini").string());

    int rc = reader.ParseError();
    if (rc == -1)
    {
        fatal("Failed to open config.ini");
    }
    else if (rc > 0)
    {
        fatal("Failed to parse config.ini: error on line %d", reader.ParseError());
    }

    ReadStartupConfig(&globalConfig, reader);

    std::thread(ConfigMonitorThread).detach();
}
