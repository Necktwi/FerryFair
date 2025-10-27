; ======================================================
; critical_section.asm
; Demonstrates using coherent memory for a critical section.
; ======================================================

    bits 64
    default rel

    section .data
lockword:    dd 0                ; 0 = unlocked, 1 = locked
shared_val:  dq 0                ; shared 64-bit counter

    section .text
    global enter_critical
    global leave_critical
    global critical_section

; --------------------------------------------
; void enter_critical(void)
;   Spin until lockword becomes 0, then set to 1.
; --------------------------------------------
enter_critical:
   push rbp
   mov  rbp, rsp
.spin:
   mov  eax, 1
   xchg eax, dword [rel lockword]   ; atomically swap
   test eax, eax
   jnz  .spin                       ; if previous != 0, keep spinning
   pop  rbp
   ret

; --------------------------------------------
; void leave_critical(void)
;   Store 0 atomically (release)
; --------------------------------------------
leave_critical:
   push rbp
   mov  rbp, rsp
   xor  eax, eax                    ; eax = 0
   xchg eax, dword [rel lockword]
   pop  rbp
   ret

; --------------------------------------------
; void critical_section(void)
;   Atomically increments shared_val
; --------------------------------------------
critical_section:
   push rbp
   mov  rbp, rsp

   call enter_critical              ; acquire lock

   mov  rax, [rel shared_val]
   add  rax, 1
   mov  [rel shared_val], rax

   call leave_critical              ; release lock

   pop  rbp
   ret
