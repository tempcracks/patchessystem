
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


# Core Logging Function
# void logger_log_impl(logger_t* logger, uint8_t level, 
#                     const char* message, const char* file,
#                     const char* function, int line)

.globl logger_log_impl
.type logger_log_impl, @function
logger_log_impl:

  pushq %rbp
  movq %rsp, %rbp
  subq $256, %rsp #reserve stack space for buffers

  movq  %rdi, -8(%rbp) #logger
  movb %rsi, -9(%rbp) #level
  movq %rdx, -24(%rbp) #message
  movq %rcx, -32(%rbp) #file
  movq %r8, -40(%rbp) #function
  movl %r9d, -44(%rbp) #line

  #check log level
  movq -8(%rbp), %rax
  movb LOGGER_LEVEL_OFFSET(%rax), %al
  cmpb %al, -9(%rbp)
  jb  .log_exit #skip if level too low

  #thread safety:simple spinlock impl

  movq -8(%rbp), %rdi
  call logger_acquire_lock

  #generate timestamp
  leaq -128(%rbp), %rdi #timestamp buffer
  movq $32, %rsi
  call get_timestamp

  #get level string
  movzbl  -9(%rbp), %edi
  call level_to_string
  movq %rax, -56(%rbp) #level_str

  #format the log message

  #write timestamp
  movq -8(%rbp), %rax
  movl LOGGER_FD_OFFSET(%rax), %edi
  leaq -128(%rbp), %rsi #timestamp
  call write_string

  #write level
  movq -8(%rbp), %rax
  movl LOGGER_FD_OFFSET(%rax), %edi
  movq -56(%rbp), %rsi
  call write_string

  #write message
  movq -8(%rbp), %rax
  movl LOGGER_FD_OFFSET(%rax), %edi
  movq -56(%rbp), %rsi #level string
  call write_string

  #write location info
  movq -8(%rbp), %rax
  movl LOGGER_FD_OFFSET(%rax), %edi
  movq -32(%rbp), %rsi #file
  call write_string

  movq -8(%rbp), %rax
  movl LOGGER_FD_OFFSET(%rax), %edi
  movq -40(%rbp), %rsi #function
  call write_string

  #write line number(convert to string first)
  leaq -192(%rbp), %rdi #line buffer
  movl  -44(%rbp), %esi #line number
  call int_to_string

  movq -8(%rbp), %rax
  movl LOGGER_FD_OFFSET(%rax), %edi
  leaq -192(%rbp), %rsi #line string
  call write_string

  #newline
  movq -8(%rbp), %rax
  movl LOGGER_FD_OFFSET(%rax), %edi
  leaq newline(%rip), %rsi
  call write_string

  #release lock
  movq -8(%rbp), %rdi
  call logger_release_lock

.log_exit:
  addq $256, %rsp
  popq %rbp
  retq
.size logger_log_impl, .-logger_log_impl

#thread safety: simple spinlock impl
#void logger_acquire_lock(logger_t* logger)
.globl logger_acquire_lock
.type logger_acquire_lock, @function
logger_acquire_lock:
  movq $1, %rax
.spin_loop:
  xchgb %al, LOGGER_LEVEL_OFFSET+7(%rdi) #use padding byte as lock
  testb %al, %al
  jnz .spin_loop
  retq
.size logger_acquire_lock, .-logger_acquire_lock

#void logger_release_lock(logger_t* logger)
.globl logger_release_lock
.type logger_release_lock, @function
logger_release_lock:
  movb $0, LOGGER_LEVEL_OFFSET+7(%rdi)
  retq
.size logger_release_lock, .-logger_release_lock

#utility functions
#void write_string(int fd,const char* str)
.globl write_string
.type write_string, @functions
write_string:
  pushq %rbp
  movq %rsp, %rbp

  #calculate string length
  movq %rsi, %rdx
  call strlen
  movq %rax, %rdx #length

  #write system call
  movl %edi, %ebx #fd
  movq %rsi, %rcx #buffer
  movq $SYS_WRITE, %rax
  int $0x80

  popq %rbp
  retq
.size write_string, .-write_string

  #void int_to_string(char* buffer, int value)
  .globl int_to_string
  .type int_to_string, @functions
  int_to_string:
    pushq %rbp
    movq %rsp, %rbp

    #simple integer to string conversion
    movl %esi, %eax
    movq %rdi, %r8
    movq $10, %rcx
    movq $0, %r9 #digit count

  .convert_loop:
    xorq %rdx, %rdx
    divq %rcx
    addb $'0', %dl
    movb %dl, (%r8,%r9)
    incq %r9
    testq  %rax,%rax
    jnz .convert_loop

    #null terminate
    movb $0, (%r8,%r9)

    #reverse the string
    movq %r8, %rdi
    call reverse_string

     popq %rbp
     retq
    .size int_to_string, .-int_to_string

  #convenience macros for different  log levels

.macro LOG_DEBUG logger, message, file, function, line
  movq \logger, %rdi
  movb $LOG_DEBUG, %sil
  leaq \message(%rip), %rdx
  leaq \file(%rip), %rcx
  movl \line, %r9d
  call logger_log_impl
.endm

.macro LOG_INFO logger, message, file, function, line
  movq \logger, %rdi
  movb $LOG_INFO, %sil
  leaq \message(%rip), %rdx
  leaq \file(%rip), %rcx
  leaq \function(%rip), %r8
  movl \line, %r9d
  call logger_log_impl
.endm

  .section rodata
  example_file: .asciz "main.c"
  example_func: .asciz "main"
  example_mgs: .asciz "Application started"

  .text
  .globl _start
  .type _start, @function
  _start:

    #create logger
    movb $LOG_DEBUG, %dil
    call logger_create_stdout
    movq %rax, %r12 #save logger

    #log a message
    LOG_INFO %r12, example_msg, example_file, example_func, 42

    #exit
    movq $SYS_EXIT, %rax
    xorq %rdi, %rdi
    int $0x80
  .size _start, .-_start

  
      
  

  




  



