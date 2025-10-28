
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
  movl %esi, LOGGER_FD_OFFSET(%rdi)
  movb %dl, LOGGER_LEVEL_OFFSET(%rdi)
  movq $0, LOGGER_LEVEL_OFFSET+1(%rdi)
  popq %rbp
  retq
.size logger_init, .-logger_init

#logger creation function
#logger_t* logger_create_stdout(uint8_t min_level)
.globl logger_create_stdout
.type logger_create_stdout, @function
logger_create_stdout:
  pushq %rbp
  movq  %rsp, %rbp
  pushq %rbx

  #allocate logger structure
  movq $LOGGER_SIZE, %rdi
  call malloc
  testq %rax, %rax
  jz  .create_strout_error

  movq %rax,%rbxlogger_init:
  pushq %rbp
  movq %rsp, %rbp
  movl %esi, LOGGER_FD_OFFSET(%rdi)
  movb %dl, LOGGER_LEVEL_OFFSET(%rdi)
  movq $0, LOGGER_LEVEL_OFFSET+1(%rdi)
  popq %rbp
  retq
.size logger_init, .-logger_init

#logger creation function
#logger_t* logger_create_stdout(uint8_t min_level)
.globl logger_create_stdout
.type logger_create_stdout, @function
logger_create_stdout:
  pushq %rbp
  movq  %rsp, %rbp
  pushq %rbx

  #allocate logger structure
  movq $LOGGER_SIZE, %rdi
  call malloc
  testq %rax, %rax
  jz  .create_strout_error

  movq %rax,%rbx

  #initialize with stdout (fd=1)
  movq %rbx,%rdi
  movq $1, %rsi  #stdout fd
  movzbl 16(%rbp), %edx  #min_level from stack
  call logger_init


  #initialize with stdout (fd=1)
  movq %rbx,%rdi
  movq $1, %rsi  #stdout fd
  movzbl 16(%rbp), %edx  #min_level from stack
  call logger_init

  movq %rbx,%rax
  jmp .create_stdout_done

.create_stdout_error:
  xorq %rax, %rax

.create_stdout_done:
  popq %rbx
  popq %rbp
  retq
.size logger_create_stdout, .-logger_create_stdout

# Level to String Conversion
# const char* level_to_string(uint8_t level)
.globl level_to_string
.type level_to_string, @function
level_to_string:
  cmpb $LOG_DEBUG, %dil
  je .debug_str
  cmpb $LOG_INFO, %dil
  je .info_str
  cmpb $LOG_WARNING, %dil
  je .warning_str
  cmpb $LOG_ERROR, %dil
  je .error_str

  #default
  leaq level_unknown(%rip), %rax
  retq

.debug_str:
  leaq level_debug(%rip), %rax
  retq

.info_str:
  leaq level_info(%rip), %rax
  retq

.warning_str:
  leaq level_warning(%rip), %rax
  retq

.error_str:
  leaq level_error(%rip), %rax
  retq
.size level_to_string .-level_to_string

# Timestamp Generation
# void get_timestamp(char* buffer, size_t buffer_size)

.globl get_timestamp
.type get_timestamp, @function
get_timestamp:
  pushq %rbp
  movq %rsp, %rbp
  subq $64, %rsp

  leaq -48(%rbp), %rdi #struct tm buffer
  xorq %rsi, %rsi #time_t = NULL  (current time)
  call localtime_r

  #format timestamp
  movq %rax, %rdi
  movq 16(%rbp), %rsi
  movq 24(%rbp), %rdx
  leaq  timestamp_fmt(%rip), %rcx
  call strftime

  addq $64, %rsp
  popq %rbp
  retq
.size get_timestamp, .-get_timestamp





  



