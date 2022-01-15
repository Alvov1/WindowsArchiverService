#define _CRT_SECURE_NO_WARNINGS
#include <iostream>
#include <fstream>
#include <string>
#include <thread>
#include <cstdio>
#include <filesystem>
#include <regex>

#include <Windows.h>
#include <TCHAR.H>
#include <winsvc.h>
#include <errhandlingapi.h>
#include <zip.h>

SERVICE_STATUS serviceStatus;
SERVICE_STATUS_HANDLE hStatus;

constexpr LPWSTR serviceName = (LPWSTR) TEXT("Archivator Service BSIT");
constexpr LPWSTR servicePath = (LPWSTR) TEXT("C:/Users/allvo/Desktop/VsProject.exe");

constexpr auto logPath = "C:/Users/allvo/Desktop/log.txt";
constexpr auto configPath = "C:/Users/allvo/Desktop/config.txt";

constexpr auto archiveName = "arch.zip";

int addLogMessage(const std::string& str) {
    static auto count = 0;

    std::ofstream output(logPath, std::ios_base::app);
    if (output.fail())
        return -1;

    output << "# " << ++count << ": " << str << std::endl;

    output.close();
    return 1;
}

int InstallService() {
    SC_HANDLE hSCManager = OpenSCManager(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
    if (!hSCManager)
        return addLogMessage("Unable to open the Service Control Manager.");

    SC_HANDLE hService = CreateService(
        hSCManager,
        serviceName,
        serviceName,
        SERVICE_ALL_ACCESS,
        SERVICE_WIN32_OWN_PROCESS,
        SERVICE_DEMAND_START,
        SERVICE_ERROR_NORMAL,
        servicePath,
        nullptr, nullptr, nullptr, nullptr, nullptr
    );
    if (!hService) {
        switch (GetLastError()) {
        case ERROR_ACCESS_DENIED:
            addLogMessage("Error: ERROR_ACCESS_DENIED");
            break;
        case ERROR_CIRCULAR_DEPENDENCY:
            addLogMessage("Error: ERROR_CIRCULAR_DEPENDENCY");
            break;
        case ERROR_DUPLICATE_SERVICE_NAME:
            addLogMessage("Error: ERROR_DUPLICATE_SERVICE_NAME");
            break;
        case ERROR_INVALID_HANDLE:
            addLogMessage("Error: ERROR_INVALID_HANDLE");
            break;
        case ERROR_INVALID_NAME:
            addLogMessage("Error: ERROR_INVALID_NAME");
            break;
        case ERROR_INVALID_PARAMETER:
            addLogMessage("Error: ERROR_INVALID_PARAMETER");
            break;
        case ERROR_INVALID_SERVICE_ACCOUNT:
            addLogMessage("Error: ERROR_INVALID_SERVICE_ACCOUNT");
            break;
        case ERROR_SERVICE_EXISTS:
            addLogMessage("Error: ERROR_SERVICE_EXISTS");
            break;
        default:
            addLogMessage("Error: Undefined");
        }
        CloseServiceHandle(hSCManager);
        return -1;
    }
    CloseServiceHandle(hService);

    CloseServiceHandle(hSCManager);
    addLogMessage("Service installed successfully.");
    std::cout << "Service installed." << std::endl;
    return 0;
}

int RemoveService() {
    SC_HANDLE hSCManager = OpenSCManager(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
    if (!hSCManager)
        return addLogMessage("Unable to open the Service Control Manager.");

    SC_HANDLE hService = OpenService(hSCManager, serviceName, SERVICE_STOP | DELETE);
    if (!hService) {
        addLogMessage("Error: Can't remove service");
        CloseServiceHandle(hSCManager);
        return -1;
    }

    DeleteService(hService);
    CloseServiceHandle(hService);
    CloseServiceHandle(hSCManager);
    addLogMessage("Service removed successfully.");
    std::cout << "Service removed." << std::endl;
    return 0;
}

int StartService() {
    std::cout << "Log file location: '" << logPath << "'. ";
    SC_HANDLE hSCManager = OpenSCManager(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
    SC_HANDLE hService = OpenService(hSCManager, serviceName, SERVICE_START);
    if (!StartService(hService, 0, nullptr)) {
        CloseServiceHandle(hSCManager);

        std::cout << "Error :" << GetLastError() << std::endl;

        return addLogMessage("Unable to start the service.");
    }

    CloseServiceHandle(hService);
    CloseServiceHandle(hSCManager);
    std::cout << "Service started." << std::endl;
    return 0;
}

void ControlHandler(DWORD request) {
    switch (request) {
    case SERVICE_CONTROL_STOP:
        addLogMessage("Stopping request.");
        serviceStatus.dwWin32ExitCode = 0;
        serviceStatus.dwCurrentState = SERVICE_STOPPED;
        SetServiceStatus(hStatus, &serviceStatus);
        return;
    case SERVICE_CONTROL_SHUTDOWN:
        addLogMessage("Shutdown request.");
        serviceStatus.dwWin32ExitCode = 0;
        serviceStatus.dwCurrentState = SERVICE_STOPPED;
        SetServiceStatus(hStatus, &serviceStatus);
        return;

    default:
        break;
    }
    SetServiceStatus(hStatus, &serviceStatus);
}

/* Adapt the mask to the std::regex API. */
std::string applyForRegex(std::string mask) {
    if (mask.empty())
        throw std::runtime_error("Mask is empty. Enter a propper mask (APPLY).");
    for (unsigned long ind = mask.find('?'); ind != std::string::npos; ind = mask.find('?', ++ind))
        mask.replace(ind, 1, ".");
    for (unsigned long ind = mask.find('*'); ind != std::string::npos; ind = mask.find('*', ++ind))
        mask.replace(ind++, 1, ".*");
    return mask;
}

/* Get path without the root.
 * E.g: E:/Directory -> /Directory. */
std::filesystem::path strip_root(const std::filesystem::path& p) {
    auto rel = p.relative_path();
    if (rel.empty()) return {};
    return rel.lexically_relative(*rel.begin());
}

int Archive(const std::filesystem::path& Directory, const std::filesystem::path& Archive, const std::vector<std::string>& masks) {
    try {
        int error = 0;
        zip* arch = zip_open(Archive.string().c_str(), ZIP_CREATE, &error);
        if (arch == nullptr)
            return addLogMessage("Unable to open the archive.");

        std::vector<std::regex> filters;
        filters.reserve(masks.size());
        for (const auto& mask : masks)
            filters.push_back(std::regex(mask));

        for (const auto& item : std::filesystem::recursive_directory_iterator(Directory)) {
            const auto pathInArchive = std::filesystem::relative(strip_root(item.path()), strip_root(Directory));
            if (item.is_directory()) {
                auto result = zip_dir_add(arch, pathInArchive.string().c_str(), ZIP_FL_ENC_GUESS);
                continue;
            }

            if (item.is_regular_file()) {
                const std::string filename = item.path().filename().string();

                bool flag = true;
                for (const auto& filter : filters) {
                    std::smatch what;
                    if (!std::regex_match(filename, what, filter)) flag = false;
                }
                if (flag == false) continue;

                auto* source = zip_source_file(arch, item.path().string().c_str(), 0, 0);
                if (source == nullptr)
                    return addLogMessage("Source filename creating error.");

                auto result = zip_file_add(arch, pathInArchive.string().c_str(), source, ZIP_FL_OVERWRITE);
                if (result < 0)
                    return addLogMessage("Unable to add item '" + pathInArchive.string() + "' to the archive.");
            }
        }

        zip_close(arch);
    }
    catch (std::exception& e) {
        addLogMessage("Unknown exception while archiving: " + std::string(e.what()) + ".");
    }
    return 0;
}

void ServiceMain(int argc, char** argv) {
    serviceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    serviceStatus.dwCurrentState = SERVICE_START_PENDING;
    serviceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP |
        SERVICE_ACCEPT_SHUTDOWN;
    serviceStatus.dwWin32ExitCode = 0;
    serviceStatus.dwServiceSpecificExitCode = 0;
    serviceStatus.dwCheckPoint = 0;
    serviceStatus.dwWaitHint = 0;
    hStatus = RegisterServiceCtrlHandler(serviceName,
        (LPHANDLER_FUNCTION)ControlHandler);
    if (hStatus == (SERVICE_STATUS_HANDLE) nullptr)
        return;

    serviceStatus.dwCurrentState = SERVICE_RUNNING;
    SetServiceStatus(hStatus, &serviceStatus);
    while (serviceStatus.dwCurrentState == SERVICE_RUNNING) {
        std::ifstream config(configPath);
        if (config.fail()) {
            addLogMessage("Unable to find the configuration file.");

            serviceStatus.dwCurrentState = SERVICE_STOPPED;
            serviceStatus.dwWin32ExitCode = -1;
            SetServiceStatus(hStatus, &serviceStatus);
            return;
        }

        std::string backupDirectoryStr;
        std::string archiveLocationStr;

        std::getline(config, backupDirectoryStr);
        std::getline(config, archiveLocationStr);

        std::filesystem::path Directory;
        std::filesystem::path ArchiveLocation;

        try {
            Directory = std::filesystem::path(backupDirectoryStr);
        }
        catch (std::invalid_argument& e) {
            const std::string what = "Error: Invalid backup directory " + backupDirectoryStr + ".";
            addLogMessage(what.c_str());
            continue;
        }

        try {
            ArchiveLocation = std::filesystem::path(archiveLocationStr + "/" + archiveName);
        }
        catch (std::invalid_argument& e) {
            const std::string what = "Error: Invalid archive location " + archiveLocationStr + ".";
            addLogMessage(what.c_str());
            continue;
        }

        std::string masksCountStr;
        std::getline(config, masksCountStr);
        size_t masksCount = 0;
        try {
            masksCount = std::stoi(masksCountStr);
        }
        catch (const std::invalid_argument& e) {
            addLogMessage("Incorrect masks number in config file.");
            continue;
        }
        std::vector<std::string> masks;
        masks.reserve(masksCount);

        std::string temp;
        for (auto i = 0; i < masksCount; ++i) {
            std::getline(config, temp);
            if (temp.empty())
                addLogMessage("Got empty mask.");
            else
                masks.push_back(applyForRegex(temp));
        }

        addLogMessage("Backup directory: '" + backupDirectoryStr + "'.");
        addLogMessage("Archive location: '" + archiveLocationStr + "/" + archiveName + "'.");
        addLogMessage(std::to_string(masksCount) + " masks.");
        Archive(Directory, ArchiveLocation, masks);

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

int StopService() {
    SC_HANDLE hSCManager = OpenSCManager(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
    SC_HANDLE hService = OpenService(hSCManager, serviceName, SERVICE_QUERY_STATUS | SERVICE_STOP);

    if (QueryServiceStatus(hService, &serviceStatus)) {
        if (serviceStatus.dwCurrentState == SERVICE_RUNNING)
            ControlService(hService, SERVICE_CONTROL_STOP, &serviceStatus);
        else
            return addLogMessage("Unable to stop the service.");
    }

    addLogMessage("Service stopped successfully.");
    CloseServiceHandle(hService);
    CloseServiceHandle(hSCManager);
    std::cout << "Service stopped." << std::endl;
    return 0;
}

int _tmain(int argc, _TCHAR* argv[]) {
    SERVICE_TABLE_ENTRY ServiceTable[1];
    ServiceTable[0].lpServiceName = serviceName;
    ServiceTable[0].lpServiceProc = (LPSERVICE_MAIN_FUNCTION)ServiceMain;
    StartServiceCtrlDispatcher(ServiceTable);

    if (argc != 1) {
        if (wcscmp(argv[argc - 1], _T("install")) == 0)
            InstallService();
        if (wcscmp(argv[argc - 1], _T("remove")) == 0)
            RemoveService();
        if (wcscmp(argv[argc - 1], _T("start")) == 0)
            StartService();
        if (wcscmp(argv[argc - 1], _T("stop")) == 0)
            StopService();

    }
}