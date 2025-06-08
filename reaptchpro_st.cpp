#include <iostream>
#include <filesystem>
#include <string>
#include <vector>
#include <memory>
#include <system_error>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <algorithm>

namespace fs = std::filesystem;

class Logger {
public:
    enum class Level { DEBUG, INFO, WARNING, ERROR };

    Logger(std::ostream& out = std::cout, Level min_level = Level::INFO)
        : out_(out), min_level_(min_level) {}

    void log(Level level, const std::string& message) {
        if (level >= min_level_) {
            auto now = std::time(nullptr);
            auto tm = *std::localtime(&now);
            
            out_ << "[" << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << "] "
                 << level_to_string(level) << ": " << message << std::endl;
        }
    }

private:
    std::string level_to_string(Level level) {
        switch(level) {
            case Level::DEBUG: return "DEBUG";
            case Level::INFO: return "INFO";
            case Level::WARNING: return "WARNING";
            case Level::ERROR: return "ERROR";
            default: return "UNKNOWN";
        }
    }

    std::ostream& out_;
    Level min_level_;
};

class CommandExecutor {
public:
    static int execute(const std::string& command, Logger& logger) {
        logger.log(Logger::Level::DEBUG, "Executing: " + command);
        
        std::array<char, 128> buffer;
        std::string result;
        
        auto pipe = popen(command.c_str(), "r");
        if (!pipe) {
            throw std::runtime_error("popen() failed!");
        }
        
        while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
            result += buffer.data();
        }
        
        int status = pclose(pipe);
        
        if (!result.empty()) {
            logger.log(Logger::Level::DEBUG, "Command output:\n" + result);
        }
        
        return status;
    }

    static std::string execute_with_output(const std::string& command, Logger& logger) {
        logger.log(Logger::Level::DEBUG, "Executing with output: " + command);
        
        std::array<char, 128> buffer;
        std::string result;
        
        auto pipe = popen(command.c_str(), "r");
        if (!pipe) {
            throw std::runtime_error("popen() failed!");
        }
        
        while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
            result += buffer.data();
        }
        
        int status = pclose(pipe);
        
        if (status != 0) {
            throw std::runtime_error("Command failed with status: " + std::to_string(status));
        }
        
        // Remove trailing newline if present
        if (!result.empty() && result.back() == '\n') {
            result.pop_back();
        }
        
        return result;
    }
};

class PortPatcher {
public:
    PortPatcher(const std::string& port_name, 
                const fs::path& patch_file,
                const fs::path& backup_dir,
                Logger& logger)
        : port_name_(port_name),
          patch_file_(patch_file),
          backup_dir_(backup_dir),
          port_dir_("/usr/ports/x11/" + port_name),
          logger_(logger) {}

    void run(bool dry_run = false) {
        try {
            logger_.log(Logger::Level::INFO, "Starting port patching for " + port_name_);
            
            verify_prerequisites();
            create_backup_dir();
            
            auto wrksrc = backup_original();
            apply_patch(wrksrc, dry_run);
            
            if (!dry_run) {
                rebuild_port();
            }
            
            logger_.log(Logger::Level::INFO, "Successfully patched " + port_name_);
        } catch (const std::exception& e) {
            logger_.log(Logger::Level::ERROR, std::string("Operation failed: ") + e.what());
            throw;
        }
    }

private:
    void verify_prerequisites() {
        if (!fs::exists(port_dir_)) {
            throw std::runtime_error("Port directory not found: " + port_dir_.string());
        }
        
        if (!fs::exists(patch_file_)) {
            throw std::runtime_error("Patch file not found: " + patch_file_.string());
        }
    }

    void create_backup_dir() {
        std::error_code ec;
        fs::create_directories(backup_dir_, ec);
        
        if (ec) {
            throw std::runtime_error("Failed to create backup directory: " + ec.message());
        }
        
        logger_.log(Logger::Level::DEBUG, "Backup directory ready: " + backup_dir_.string());
    }

    std::string backup_original() {
        logger_.log(Logger::Level::INFO, "Backing up original source files...");
        
        // Execute make extract
        if (CommandExecutor::execute("cd " + port_dir_.string() + " && make extract", logger_) != 0) {
            throw std::runtime_error("make extract failed");
        }
        
        // Get WRKSRC directory
        auto wrksrc = CommandExecutor::execute_with_output(
            "cd " + port_dir_.string() + " && make -V WRKSRC", logger_);
        
        fs::path source_dir = port_dir_ / wrksrc;
        fs::path backup_path = backup_dir_ / (port_name_ + "-original-" + get_timestamp());
        
        // Copy directory
        std::error_code ec;
        fs::copy(source_dir, backup_path, fs::copy_options::recursive, ec);
        
        if (ec) {
            throw std::runtime_error("Backup failed: " + ec.message());
        }
        
        logger_.log(Logger::Level::INFO, "Backup created at: " + backup_path.string());
        return wrksrc;
    }

    void apply_patch(const std::string& wrksrc, bool dry_run) {
        logger_.log(Logger::Level::INFO, "Applying patch: " + patch_file_.string());
        
        fs::path source_dir = port_dir_ / wrksrc;
        std::string patch_cmd = "cd " + source_dir.string() + " && patch -p1 < " + patch_file_.string();
        
        if (dry_run) {
            logger_.log(Logger::Level::INFO, "[DRY RUN] Would execute: " + patch_cmd);
            return;
        }
        
        if (CommandExecutor::execute(patch_cmd, logger_) != 0) {
            logger_.log(Logger::Level::ERROR, "Patch failed! Attempting restore...");
            restore_from_backup(source_dir);
            throw std::runtime_error("Patch application failed");
        }
    }

    void restore_from_backup(const fs::path& target_dir) {
        // Find the most recent backup
        std::vector<fs::path> backups;
        for (const auto& entry : fs::directory_iterator(backup_dir_)) {
            if (entry.path().string().find(port_name_ + "-original-") != std::string::npos) {
                backups.push_back(entry.path());
            }
        }
        
        if (backups.empty()) {
            throw std::runtime_error("No backup found to restore from");
        }
        
        std::sort(backups.begin(), backups.end());
        fs::path latest_backup = backups.back();
        
        logger_.log(Logger::Level::INFO, "Restoring from backup: " + latest_backup.string());
        
        // Clear target directory first
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(target_dir)) {
            fs::remove_all(entry.path(), ec);
            if (ec) {
                logger_.log(Logger::Level::WARNING, 
                          "Failed to remove " + entry.path().string() + ": " + ec.message());
            }
        }
        
        // Copy backup contents
        for (const auto& entry : fs::recursive_directory_iterator(latest_backup)) {
            auto relative = fs::relative(entry.path(), latest_backup);
            auto dest = target_dir / relative;
            
            if (fs::is_directory(entry.status())) {
                fs::create_directories(dest, ec);
            } else {
                fs::copy_file(entry.path(), dest, fs::copy_options::overwrite_existing, ec);
            }
            
            if (ec) {
                throw std::runtime_error("Restore failed during copy: " + ec.message());
            }
        }
    }

    void rebuild_port() {
        logger_.log(Logger::Level::INFO, "Rebuilding port with patch...");
        
        std::string rebuild_cmd = "cd " + port_dir_.string() + " && make clean install";
        if (CommandExecutor::execute(rebuild_cmd, logger_) != 0) {
            throw std::runtime_error("make clean install failed");
        }
    }

    std::string get_timestamp() {
        auto now = std::time(nullptr);
        auto tm = *std::localtime(&now);
        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y%m%d-%H%M%S");
        return oss.str();
    }

    const std::string port_name_;
    const fs::path patch_file_;
    const fs::path backup_dir_;
    const fs::path port_dir_;
    Logger& logger_;
};

int main(int argc, char* argv[]) {
    try {
        // Initialize logging
        std::ofstream log_file("/var/log/port_patcher.log", std::ios::app);
        Logger file_logger(log_file, Logger::Level::DEBUG);
        Logger console_logger(std::cout, Logger::Level::INFO);
        
        // Configuration - could be enhanced with command-line parsing
        std::string port_name = "st";
        fs::path patch_file = "/path/to/your/patch.diff";
        fs::path backup_dir = "/usr/local/etc/patches";
        
        bool dry_run = false;
        if (argc > 1 && std::string(argv[1]) == "--dry-run") {
            dry_run = true;
            console_logger.log(Logger::Level::INFO, "Running in dry-run mode");
        }
        
        PortPatcher patcher(port_name, patch_file, backup_dir, file_logger);
        patcher.run(dry_run);
        
        return EXIT_SUCCESS;
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
}
