#include <iostream>
#include <filesystem>
#include <cstdlib>
#include <string>
#include <system_error>

namespace fs = std::filesystem;

class PatchApplier {
    std::string port_name;
    fs::path patch_file;
    fs::path backup_dir;
    fs::path port_dir;

public:
    PatchApplier(const std::string& name, const fs::path& patch, const fs::path& backup)
        : port_name(name), patch_file(patch), backup_dir(backup),
          port_dir("/usr/ports/x11/" + name) {}

    void run() {
        try {
            create_backup_dir();
            backup_original();
            apply_patch();
            rebuild_port();
            std::cout << "[+] Done! " << port_name << " has been patched.\n";
        } catch (const std::exception& e) {
            std::cerr << "[-] Error: " << e.what() << "\n";
            std::exit(EXIT_FAILURE);
        }
    }

private:
    void create_backup_dir() {
        std::error_code ec;
        fs::create_directories(backup_dir, ec);
        if (ec) {
            throw std::runtime_error("Failed to create backup directory: " + ec.message());
        }
    }

    void backup_original() {
        std::cout << "[+] Backing up original source files...\n";
        
        // Execute make extract
        if (std::system(("cd " + port_dir.string() + " && make extract").c_str()) {
            throw std::runtime_error("make extract failed");
        }

        // Get WRKSRC directory
        auto wrksrc = get_make_var("WRKSRC");
        fs::path source_dir = port_dir / wrksrc;
        fs::path backup_path = backup_dir / (port_name + "-original");

        // Copy directory
        std::error_code ec;
        fs::remove_all(backup_path, ec);
        fs::copy(source_dir, backup_path, fs::copy_options::recursive, ec);
        if (ec) {
            throw std::runtime_error("Backup failed: " + ec.message());
        }
    }

    void apply_patch() {
        std::cout << "[+] Applying patch: " << patch_file << "...\n";
        
        auto wrksrc = get_make_var("WRKSRC");
        fs::path source_dir = port_dir / wrksrc;

        // Apply patch
        if (std::system(("cd " + source_dir.string() + " && patch -p1 < " + patch_file.string()).c_str())) {
            std::cout << "[-] Patch failed! Restoring original files...\n";
            
            // Restore backup
            fs::path backup_path = backup_dir / (port_name + "-original");
            std::error_code ec;
            for (const auto& entry : fs::recursive_directory_iterator(backup_path)) {
                auto relative = fs::relative(entry.path(), backup_path);
                auto dest = source_dir / relative;
                
                if (fs::is_directory(entry.status())) {
                    fs::create_directories(dest, ec);
                } else {
                    fs::copy_file(entry.path(), dest, fs::copy_options::overwrite_existing, ec);
                }
                
                if (ec) {
                    throw std::runtime_error("Restore failed: " + ec.message());
                }
            }
            
            throw std::runtime_error("Patch application failed");
        }
    }

    void rebuild_port() {
        std::cout << "[+] Rebuilding port with patch...\n";
        if (std::system(("cd " + port_dir.string() + " && make clean install").c_str())) {
            throw std::runtime_error("make clean install failed");
        }
    }

    std::string get_make_var(const std::string& var) {
        std::string cmd = "cd " + port_dir.string() + " && make -V " + var;
        std::array<char, 128> buffer;
        std::string result;
        
        std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
        if (!pipe) {
            throw std::runtime_error("popen() failed!");
        }
        
        while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
            result += buffer.data();
        }
        
        // Remove trailing newline
        if (!result.empty() && result.back() == '\n') {
            result.pop_back();
        }
        
        return result;
    }
};

int main() {
    // Configuration
    std::string port_name = "st";
    fs::path patch_file = "/path/to/your/patch.diff";
    fs::path backup_dir = "/usr/local/etc/patches";

    PatchApplier applier(port_name, patch_file, backup_dir);
    applier.run();

    return EXIT_SUCCESS;
}
