.intel_syntax noprefix
.global enter_critical
.global leave_critical
.global critical_section
.global shared_val
   
.section .data
lockword:      .long 0          # 0 = unlocked, 1 = locked
shared_val:    .quad 0          # shared 64-bit counter

.section .text

# ------------------------------------------------------------
# void enter_critical(void)
# Spin until lockword becomes 0, then set to 1.
# ------------------------------------------------------------
enter_critical:
   mov   eax, 1
1: xchg  eax, DWORD PTR lockword[rip]
   test  eax, eax
   jnz   1b
   ret

# ------------------------------------------------------------
# void leave_critical(void)
# Store 0 atomically to release lock.
# ------------------------------------------------------------
leave_critical:
   xor   eax, eax
   xchg  eax, DWORD PTR lockword[rip]
   ret

# ------------------------------------------------------------
# void critical_section(void)
# Atomically increments shared_val.
# ------------------------------------------------------------
critical_section:
   call  enter_critical

   mov   rax, QWORD PTR shared_val[rip]
   add   rax, 1
   mov   QWORD PTR shared_val[rip], rax

   call  leave_critical
   ret
