#include <algorithm>
#include <chrono>
#include <cstdio>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <print>
#include <ranges>
#include <source_location>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef __linux__
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using namespace std::chrono;
using namespace std::string_literals;
using namespace std::string_view_literals;

// logger

class Logger{
public:
	enum class Level : uint8_t { DEBUG, INFO, WARNING, ERROR };
	
	constexpr Logger(std::ostream& out = std::cout, Level min_level = Level::INFO) noexcept :out_(out), min_level_(min_level) {}
	template <typename... Args>
	void log(Level level, std::format_string<Args...> fmt, Args&&... args,
			const std::source_location& loc = std::source_location::current()){
		if (level < min_level_) return;
		std::scoped_lock lock(mutex_);
		auto now = zoned_time{current_zone(), system_clock::now()};
		std::print(out_, "[{:%Y-%m-%d %H:%M:%S}] {}: {} [{}:{}:{}]\n",
			               now,
				            level_to_string(level),
				            std::format(fmt, std::forward<Args>(args)...),
				            loc.file_name(),
				            loc.function_name(),
				            loc.line()
			   	);
    }
		/* method with compile-time filtering */
	     template <Level Level, typename... Args>
	     void log(std::format_string<Args...> fmt, Args&&... args,
	     const std::source_location& loc = std::source_location::current()) {
	     if constexpr (Level >= Level::INFO) {
	     log(Level, fmt, std::forward<Args>(args)..., loc);
		 }
		 } 
	     
	 	 void debug(auto&&... args) { log<Level::DEBUG>(std::forward<decltype(args)>(args)...); }
	 	 void info(auto&&... args) { log<Level::INFO>(std::forward<decltype(args)>(args)...); }	
		 void warning(auto&&... args) { log<Level::WARNING>(std::forward<decltype(args)>(args)...); }	
	 	 void error(auto&&... args) { log<Level::ERROR>(std::forward<decltype(args)>(args)...); }	
	 	
		static consteval std::string_view level_to_string(Level level) noexcept{
	   		using enum Level;
			switch (level){
				case DEBUG: return "DEBUG"sv;
				case INFO: return "INFO"sv;
				case WARNING: return "WARNING"sv;
				case ERROR: return "ERROR"sv;
				default: return "UNKNOWN"sv;
			}
		}

		void set_min_level(Level level) noexcept { min_level_ = level; }
		[[nodiscard]] Level get_min_level() const noexcept { return min_level_;}

private:
	std::reference_wrapper<std::ostream> out_;
	Level min_level_;
	std::mutex mutex_;
};


class CommandExecutor {
public:
	struct Result {
		int status;
		std::string output;
	};

	[[nodiscard]] static std::expected<Result, std::string>
		execute(std::string_view command, Logger& logger) {
			logger.debug("Executing: {}", command);

			std::array<char, 128> buffer;
			std::string result;

			#ifdef __unix__
			auto pipe = popen(command.data(), "r");
			if (!pipe) {
				return std::unexpected("popen() failed");
			}
		
		while(fgets(buffer.data(), buffer.size(), pipe)) != nullptr){
			result += buffer.data();
		}
		
		int status  = pclose(pipe);
		
		if (!result.empty()){
			logger.debug("command output:\n{}", result);
		}
		
		return Result{status,std::move(result)};
		#else
		return std::unexpected("Unsupported platform");
		#endif
	}
	
	[[nodiscard]] static std::expected<std::string, std::string>
		execute_with_output(std::string_view command, Logger& logger) {
		auto result = execute(command, logger);
		if (!result) return std::unexpected(result.error());
		
		if (result->status != 0){
			return std::unexpected(std::format("command failed with status: {}", result->status));
		}
		
		// remove trailing newlines
		auto output = result->output;
		while (!output.empty() && (output.back() == '\n' || output.back() == '\r')) {
			output.pop_back();
		}
		return output;
	}
	
};

class PortPatcher {
public:
	struct Config {
		std::string port_name;
		fs::path patch_file;
		fs::path backup_dir;
		fs::path ports_dir{"/usr/ports"};
		bool dry_run{false};
		bool force{false};
	};
	
	PortPatcher(Config config, Logger& logger):config_(std::move(config)),logger_(logger){}
	
	[[nodiscard]] std::expected<void, std::string> run(){
		try{
			logger_.info("starting  port patching for {}", config_.port_name);
			verify_prerequisites();
			create_backup_dir();
			
			auto wrksrc = backup_original();
			apply_patch(wrksrc);
			
			if (!config_.dry_run) {
				rebuild_port();
			}
			
			logger_.info("successfully patched {}", config_.port_name);
			return {};
			
		} catch (const std::exception& e){
			return std::unexpected(std::format("Operation failed: {}", e.what()));
			}
		}
	}
private:

	void verify_prerequisites() const {
		const auto port_dir = config_.ports_dir / "x11" / config_.port_name;
		if(!fs::exists(port_dir)){
			throw std::runtime_error(std::format("Port directory not found {}:, port_dir.string()));
			}
		if (!fs::exists(config_.patch_file)){
			throw std::runtime_error(std::format("patch file not found: {}", config_.patch_file.string()));
			}
		logger_.debug("prerequisistes verified succeessfuly");
	}
	void create_backup_dir() const {
		std::error_code ec;
		fs::create_directories(config_.backup_dir, ec);
		
		if(ec){
			throw std::runtime_error(std::format("failed to create backup directory: {}", ec.message()));
			}
		logger_.debug("Backup directory ready: {}", config_.backup_dir.string());
	}
	std::string backup_original(){
		logger_.info("Backing up original source files...");
		const auto port_dir= config_.ports_dir / "x11" / config_.port_name;
		
		// execute make extract
		auto result = CommandExecutor::execute(
			std::format("cd {} && make extract", port_dir.string()), logger_);
		
		if (!result || result->status != 0 ) {
			throw std::runtime_error("make extract failed");
		}
		
		// get wrksrc directory
		auto wrksrc = CommandExecutor::execute_with_output(
			std::format("cd {} && make -v WRKSRC", port_dir.string()), logger_);
		
		if (!wrksrc){
			throw std::runtime_error(std::format("failed to get WRKSRC: {}", wrksrc.error()));
			}
		
		fs::path source_dir = port_dir / *wrksrc;
		fs::path backup_path = config_.backup_dir / 
			std::format("{}-original-{}", config_.port_name, get_timestamp());
		
		// copy directory  using modern filesystem operations
		std::error_code ec;
		fs::copy(source_dir, backup_path, fs::copy_options::recursive | fs::copy_options::copy_symlinks, ec);
		
		if (ec){
			throw std::runtime_error(std::format("backup failed: {}", ec.message()));
		}
		
		logger_.info("backup created at: {}", backup_path.string());
		return *wrksrc;
		}
		void apply_patch(const std::string& wrksrc){
			logger_.info("applying patch {}", config_.patch_file.string());
			const auto port_dir = config_.ports_dir / "x11" / config_.port_name;
			const auto source_dir = port_dir / wrksrc;
			const auto patch_cmd = std::format("cd {} && patch -p1 < {}", source_dir.strin(), config_.patch_file.string());
		
		if (config_.dry_run){
			logger_.info("[DRY RUN] would execute: {}", patch_cmd);
			return;
		}
		
		auto result = CommandExecutor::execute(patch_cmd, logger_);
		if (!result || result->status != 0) { 
			logger_.error("patch failed! attempting restore...");
			restore_from_backup(source_dir);
			throw std::runtime_error("patch application failed");
			}
		}
		void restore_from_backup(const fs::path& target_dir) {
        // Find backups using modern ranges
        auto backups = fs::directory_iterator(config_.backup_dir)
            | std::views::filter([this](const auto& entry) {
                return entry.path().string().contains(config_.port_name + "-original-");
            })
            | std::ranges::to<std::vector>();
        
        if (backups.empty()) {
            throw std::runtime_error("No backup found to restore from");
        }
        
        // Sort by modification time
        std::ranges::sort(backups, [](const auto& a, const auto& b) {
            return fs::last_write_time(a) > fs::last_write_time(b);
        });
        
        const auto& latest_backup = backups.front();
        logger_.info("Restoring from backup: {}", latest_backup.string());
        
        // Clear target directory using modern filesystem operations
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(target_dir)) {
            fs::remove_all(entry.path(), ec);
            if (ec) {
                logger_.warning("Failed to remove {}: {}", entry.path().string(), ec.message());
            }
        }
        
        // Copy with modern recursive iterator
        for (const auto& entry : fs::recursive_directory_iterator(latest_backup)) {
            const auto relative = fs::relative(entry.path(), latest_backup);
            const auto dest = target_dir / relative;
            
            if (fs::is_directory(entry.status())) {
                fs::create_directories(dest, ec);
            } else {
                fs::copy_file(entry.path(), dest, fs::copy_options::overwrite_existing, ec);
            }
            
            if (ec) {
                throw std::runtime_error(std::format("Restore failed during copy: {}", ec.message()));
            }
        }
    }

    void rebuild_port() {
        logger_.info("Rebuilding port with patch...");
        
        const auto port_dir = config_.ports_dir / "x11" / config_.port_name;
        const auto rebuild_cmd = std::format("cd {} && make clean install", port_dir.string());
        
        auto result = CommandExecutor::execute(rebuild_cmd, logger_);
        if (!result || result->status != 0) {
            throw std::runtime_error("make clean install failed");
        }
    }

    [[nodiscard]] static std::string get_timestamp() {
        auto now = zoned_time{current_zone(), system_clock::now()};
        return std::format("{:%Y%m%d-%H%M%S}", now);
    }

    Config config_;
    Logger& logger_;
};

struct CLIArgs {
    std::string port_name;
    fs::path patch_file;
    fs::path backup_dir{"/usr/local/etc/patches"};
    bool dry_run{false};
    bool verbose{false};
    bool help{false};
};

[[nodiscard]] std::expected<CLIArgs, std::string> parse_args(std::span<const char*> args) {
    CLIArgs cli_args;
    
    for (size_t i = 1; i < args.size(); ++i) {
        std::string_view arg = args[i];
        
        if (arg == "--help" || arg == "-h") {
            cli_args.help = true;
        } else if (arg == "--dry-run" || arg == "-n") {
            cli_args.dry_run = true;
        } else if (arg == "--verbose" || arg == "-v") {
            cli_args.verbose = true;
        } else if (arg == "--backup-dir" || arg == "-b") {
            if (++i >= args.size()) return std::unexpected("Missing backup directory");
            cli_args.backup_dir = args[i];
        } else if (!arg.starts_with('-')) {
            if (cli_args.port_name.empty()) {
                cli_args.port_name = arg;
            } else if (cli_args.patch_file.empty()) {
                cli_args.patch_file = arg;
            }
        } else {
            return std::unexpected(std::format("Unknown argument: {}", arg));
        }
    }
    
    if (cli_args.help) return cli_args;
    if (cli_args.port_name.empty()) return std::unexpected("Port name required");
    if (cli_args.patch_file.empty()) return std::unexpected("Patch file required");
    
    return cli_args;
}

void print_usage(std::string_view program_name) {
    std::print("Usage: {} <port-name> <patch-file> [options]\n", program_name);
    std::print("Options:\n");
    std::print("  -h, --help           Show this help message\n");
    std::print("  -n, --dry-run        Don't actually apply changes\n");
    std::print("  -v, --verbose        Enable verbose output\n");
    std::print("  -b, --backup-dir DIR Specify backup directory\n");
}

int main(int argc, char* argv[]) {
    try {
        // Parse command line arguments
        auto args = parse_args({argv, static_cast<size_t>(argc)});
        if (!args) {
            std::println(stderr, "Error: {}", args.error());
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
        
        if (args->help) {
            print_usage(argv[0]);
            return EXIT_SUCCESS;
        }
        
        // Initialize logging
        std::ofstream log_file("/var/log/port_patcher.log", std::ios::app);
        Logger file_logger(log_file, args->verbose ? Logger::Level::DEBUG : Logger::Level::INFO);
        Logger console_logger(std::cout, args->verbose ? Logger::Level::DEBUG : Logger::Level::INFO);
        
        // Create and run patcher
        PortPatcher::Config config{
            .port_name = args->port_name,
            .patch_file = args->patch_file,
            .backup_dir = args->backup_dir,
            .dry_run = args->dry_run
        };
        
        PortPatcher patcher(config, file_logger);
        
        if (args->dry_run) {
            console_logger.info("Running in dry-run mode");
        }
        
        auto result = patcher.run();
        if (!result) {
            console_logger.error("{}", result.error());
            return EXIT_FAILURE;
        }
        
        console_logger.info("Operation completed successfully");
        return EXIT_SUCCESS;
        
    } catch (const std::exception& e) {
        std::println(stderr, "Fatal error: {}", e.what());
        return EXIT_FAILURE;
    }
}
		

