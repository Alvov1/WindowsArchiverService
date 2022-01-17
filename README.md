# WindowsArchiverService
Windows Service implementation program. Creates `.zip` archive of the directory listed in configuration file with given wildcard masks. After installation service appears in the Windows Services manager.

Uses linux `libzip` library for archiving, `std::filesystem` for going through all files in directory and `std::regex` to check wildcard masks.

All actions are listed in the log file.

