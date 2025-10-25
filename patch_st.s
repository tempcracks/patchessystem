
.section .rodata
.align 8

# Log level strings
level_debug:    .asciz "DEBUG"
level_info:     .asciz "INFO" 
level_warning:  .asciz "WARNING"
level_error:    .asciz "ERROR"
level_unknown:  .asciz "UNKNOWN"

# Format strings
timestamp_fmt:  .asciz "[%Y-%m-%d %H:%M:%S]"
log_format:     .asciz "{}: {} [{}:{}:{}]\n"
newline:        .asciz "\n"

# File paths
stdout_path:    .asciz "/dev/stdout"
stderr_path:    .asciz "/dev/stderr"

# Error messages
open_error:     .asciz "Failed to open log file\n"

.equ LOG_DEBUG,   0
.equ LOG_INFO,    1
.equ LOG_WARNING, 2
.equ LOG_ERROR,   3

#struct logger{
#  int fd;
#  uint8_t min_level;
#  uint8_t padding[7];
#};

.equ LOGGER_SIZE,  16
.equ LOGGER_FD_OFFSET, 0
.equ LOGGER_LEVEL_OFFSET, 8

.section .text
.globl logger_init
.type logger_init, @funtion

#void logger_init(logger_t* logger, int fd, uint8_t min_level)
# %rdi = logger pointer, %rsi = fd, %rdx = min_level
logger_init:
  pushq %rbp
  movq %rsp, %rbp
  movl %esi,


