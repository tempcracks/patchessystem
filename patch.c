#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <dirent.h>
#include <libgen.h>

// ============================================================================
// LOGGING SYSTEM
// ============================================================================
typedef enum {
    LOG_DEBUG,
    LOG_INFO, 
    LOG_WARNING,
    LOG_ERROR
} log_level_t;

typedef struct {
    FILE* output;
    log_level_t min_level;
} logger_t;

void logger_init(logger_t* logger, FILE* output, log_level_t min_level) {
    logger->output = output;
    logger->min_level = min_level;
}

void logger_log(logger_t* logger, log_level_t level, const char* format, ...) {
    if (level < logger->min_level) return;
    
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    char timestamp[20];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);
    
    const char* level_str = "UNKNOWN";
    switch(level) {
        case LOG_DEBUG: level_str = "DEBUG"; break;
        case LOG_INFO: level_str = "INFO"; break;
        case LOG_WARNING: level_str = "WARNING"; break;
        case LOG_ERROR: level_str = "ERROR"; break;
    }
    
    fprintf(logger->output, "[%s] %s: ", timestamp, level_str);
    
    va_list args;
    va_start(args, format);
    vfprintf(logger->output, format, args);
    va_end(args);
    
    fprintf(logger->output, "\n");
    fflush(logger->output);
}

// ============================================================================
// COMMAND EXECUTION
// ============================================================================
typedef struct {
    int status;
    char* output;
} command_result_t;

void command_result_free(command_result_t* result) {
    if (result && result->output) {
        free(result->output);
        result->output = NULL;
    }
}

int command_execute(const char* command, command_result_t* result, logger_t* logger) {
    if (logger) {
        logger_log(logger, LOG_DEBUG, "Executing: %s", command);
    }
    
    FILE* pipe = popen(command, "r");
    if (!pipe) {
        return -1;
    }
    
    char buffer[128];
    size_t output_size = 0;
    char* output = NULL;
    
    while (fgets(buffer, sizeof(buffer), pipe) != NULL) {
        size_t chunk_len = strlen(buffer);
        char* new_output = realloc(output, output_size + chunk_len + 1);
        if (!new_output) {
            free(output);
            pclose(pipe);
            return -1;
        }
        output = new_output;
        memcpy(output + output_size, buffer, chunk_len);
        output_size += chunk_len;
        output[output_size] = '\0';
    }
    
    int status = pclose(pipe);
    
    if (result) {
        result->status = status;
        result->output = output;
    } else {
        free(output);
    }
    
    if (logger && output) {
        logger_log(logger, LOG_DEBUG, "Command output:\n%s", output);
    }
    
    return 0;
}

char* command_execute_with_output(const char* command, logger_t* logger) {
    command_result_t result = {0};
    
    if (command_execute(command, &result, logger) != 0) {
        return NULL;
    }
    
    if (result.status != 0) {
        free(result.output);
        return NULL;
    }
    
    // Remove trailing newlines
    if (result.output) {
        size_t len = strlen(result.output);
        while (len > 0 && (result.output[len-1] == '\n' || result.output[len-1] == '\r')) {
            result.output[--len] = '\0';
        }
    }
    
    return result.output;
}

// ============================================================================
// FILE SYSTEM UTILITIES
// ============================================================================
int directory_exists(const char* path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

int file_exists(const char* path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

int create_directory_recursive(const char* path) {
    char* path_copy = strdup(path);
    if (!path_copy) return -1;
    
    char* p = path_copy;
    
    // Skip leading slash
    if (*p == '/') p++;
    
    while (*p != '\0') {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(path_copy, 0755) != 0 && errno != EEXIST) {
                free(path_copy);
                return -1;
            }
            *p = '/';
        }
        p++;
    }
    
    if (mkdir(path_copy, 0755) != 0 && errno != EEXIST) {
        free(path_copy);
        return -1;
    }
    
    free(path_copy);
    return 0;
}

int copy_file(const char* src, const char* dst) {
    FILE* src_file = fopen(src, "rb");
    if (!src_file) return -1;
    
    FILE* dst_file = fopen(dst, "wb");
    if (!dst_file) {
        fclose(src_file);
        return -1;
    }
    
    char buffer[8192];
    size_t bytes;
    int success = 0;
    
    while ((bytes = fread(buffer, 1, sizeof(buffer), src_file)) > 0) {
        if (fwrite(buffer, 1, bytes, dst_file) != bytes) {
            success = -1;
            break;
        }
    }
    
    fclose(src_file);
    fclose(dst_file);
    return success;
}

// ============================================================================
// PORT PATCHER
// ============================================================================
typedef struct {
    char* port_name;
    char* patch_file;
    char* backup_dir;
    char* ports_dir;
    int dry_run;
} patcher_config_t;

void patcher_config_init(patcher_config_t* config) {
    memset(config, 0, sizeof(*config));
    config->ports_dir = strdup("/usr/ports");
}

void patcher_config_free(patcher_config_t* config) {
    free(config->port_name);
    free(config->patch_file);
    free(config->backup_dir);
    free(config->ports_dir);
}

typedef struct {
    patcher_config_t config;
    logger_t* logger;
} port_patcher_t;

void port_patcher_init(port_patcher_t* patcher, const patcher_config_t* config, logger_t* logger) {
    patcher->config = *config;
    patcher->config.port_name = config->port_name ? strdup(config->port_name) : NULL;
    patcher->config.patch_file = config->patch_file ? strdup(config->patch_file) : NULL;
    patcher->config.backup_dir = config->backup_dir ? strdup(config->backup_dir) : NULL;
    patcher->config.ports_dir = config->ports_dir ? strdup(config->ports_dir) : NULL;
    patcher->logger = logger;
}

void port_patcher_free(port_patcher_t* patcher) {
    patcher_config_free(&patcher->config);
}

// Fixed version of verify_prerequisites
int verify_prerequisites(const port_patcher_t* patcher) {
    // Build port directory path
    char port_dir[1024];
    snprintf(port_dir, sizeof(port_dir), "%s/x11/%s", 
             patcher->config.ports_dir, patcher->config.port_name);
    
    if (!directory_exists(port_dir)) {
        logger_log(patcher->logger, LOG_ERROR, "Port directory not found: %s", port_dir);
        return -1;
    }
    
    if (!file_exists(patcher->config.patch_file)) {
        logger_log(patcher->logger, LOG_ERROR, "Patch file not found: %s", patcher->config.patch_file);
        return -1;
    }
    
    logger_log(patcher->logger, LOG_DEBUG, "Prerequisites verified successfully");
    return 0;
}

// Fixed version of create_backup_dir
int create_backup_dir(const port_patcher_t* patcher) {
    if (create_directory_recursive(patcher->config.backup_dir) != 0) {
        logger_log(patcher->logger, LOG_ERROR, "Failed to create backup directory: %s", 
                   strerror(errno));
        return -1;
    }
    
    logger_log(patcher->logger, LOG_DEBUG, "Backup directory ready: %s", 
               patcher->config.backup_dir);
    return 0;
}

// Fixed version of backup_original
char* backup_original(port_patcher_t* patcher) {
    logger_log(patcher->logger, LOG_INFO, "Backing up original source files...");
    
    // Build port directory path
    char port_dir[1024];
    snprintf(port_dir, sizeof(port_dir), "%s/x11/%s", 
             patcher->config.ports_dir, patcher->config.port_name);
    
    // Execute make extract
    char command[2048];
    snprintf(command, sizeof(command), "cd %s && make extract", port_dir);
    
    command_result_t result = {0};
    if (command_execute(command, &result, patcher->logger) != 0 || result.status != 0) {
        logger_log(patcher->logger, LOG_ERROR, "make extract failed");
        command_result_free(&result);
        return NULL;
    }
    command_result_free(&result);
    
    // Get WRKSRC directory (fixed the -v to -V)
    snprintf(command, sizeof(command), "cd %s && make -V WRKSRC", port_dir);
    char* wrksrc = command_execute_with_output(command, patcher->logger);
    if (!wrksrc) {
        logger_log(patcher->logger, LOG_ERROR, "Failed to get WRKSRC directory");
        return NULL;
    }
    
    // Create backup path with timestamp
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    char timestamp[20];
    strftime(timestamp, sizeof(timestamp), "%Y%m%d-%H%M%S", tm_info);
    
    char backup_path[1024];
    snprintf(backup_path, sizeof(backup_path), "%s/%s-original-%s",
             patcher->config.backup_dir, patcher->config.port_name, timestamp);
    
    // Build source directory path
    char source_dir[1024];
    snprintf(source_dir, sizeof(source_dir), "%s/%s", port_dir, wrksrc);
    
    // Use system cp command for reliable directory copying
    snprintf(command, sizeof(command), "cp -r %s %s", source_dir, backup_path);
    if (command_execute(command, NULL, patcher->logger) != 0) {
        logger_log(patcher->logger, LOG_ERROR, "Backup copy failed");
        free(wrksrc);
        return NULL;
    }
    
    logger_log(patcher->logger, LOG_INFO, "Backup created at: %s", backup_path);
    return wrksrc;
}

// ============================================================================
// USAGE EXAMPLE
// ============================================================================
int main(int argc, char* argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <port-name> <patch-file> [backup-dir]\n", argv[0]);
        return EXIT_FAILURE;
    }
    
    // Initialize logging
    logger_t file_logger, console_logger;
    FILE* log_file = fopen("/var/log/port_patcher.log", "a");
    if (log_file) {
        logger_init(&file_logger, log_file, LOG_DEBUG);
    }
    logger_init(&console_logger, stdout, LOG_INFO);
    
    // Initialize configuration
    patcher_config_t config;
    patcher_config_init(&config);
    config.port_name = strdup(argv[1]);
    config.patch_file = strdup(argv[2]);
    config.backup_dir = argc > 3 ? strdup(argv[3]) : strdup("/usr/local/etc/patches");
    
    // Initialize patcher
    port_patcher_t patcher;
    port_patcher_init(&patcher, &config, &file_logger);
    
    int success = 0;
    
    // Run operations
    if (verify_prerequisites(&patcher) == 0 &&
        create_backup_dir(&patcher) == 0) {
        
        char* wrksrc = backup_original(&patcher);
        if (wrksrc) {
            logger_log(&console_logger, LOG_INFO, "Backup completed successfully");
            free(wrksrc);
            success = 1;
        }
    }
    
    // Cleanup
    port_patcher_free(&patcher);
    patcher_config_free(&config);
    
    if (log_file) {
        fclose(log_file);
    }
    
    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
