.arm

.section .text

// Bit decoding function, inline it for speed (those extra cycles pile up)
.macro DECODE_BIT refill_fn
    cmp r2, #4096
    blt \refill_fn           // refill happens rarely (~8%), thus branch

    ldrb r8, [sp, r5]
    mvn r9, r2, lsl #24      // prov > (state & 255) can be also expressed as:
    cmn r9, r8, lsl #24      // 0xFFFFFFFF + ((prob - state) << 24)
    rsbcs r8, r8, #256
    sbc r9, r11, r2, lsr #8
    mla r2, r9, r8, r2       // mul order matters (can shave a cycle or two)
    add r9, r8, #8
    sub r8, r8, r9, lsr#4
    rsbcs r8, r8, #256
    strb r8, [sp, r5]
.endm

.macro REFILL_BIT lbl
\lbl\()_refill:
    ldrb r8, [r1], #1
    orr r2, r8, r2, lsl #8
    b \lbl
.endm

.global upkr_unpack
.type upkr_unpack, %function
// r0 .. out_ptr (returned)
// r1 .. in_ptr (returned)
// r2 .. state
// r3 .. offset
// r4 .. prev_was_literal / decode_length ret
// r5 .. context index
// r6 .. decode_length temp
// r8-r9 .. decode_bit temp
// r11 .. hardcoded to zero
// r12 .. decode_length return address
upkr_unpack:
    push { r0, r3-r12, lr }

    // Setup initial state including table on stack
    mov r11, $0
    mov r2, $96
    ldr r3, =0x80808080
.Lclear:
    push {r3}
    subs r2, r2, #1
    bne .Lclear

.Lloop:
    mov r5, #0
    bl upkr_decode_bit
    bcc .Ldata
.Lmatch:
    mov r5, #256
    rsbs r6, r4, #0
    blcc upkr_decode_bit
    bcc .Lskip_offset

    bl upkr_decode_length
    adds r3, r4, #1
    beq .retfn
.Lskip_offset:

    mov r5, #256+64
    bl upkr_decode_length
.Lcopy_loop:
    ldrb r5, [r0, r3]
.Lstore:
    strb r5, [r0], #1
    adds r4, r4, #1
    blt .Lcopy_loop
    b .Lloop

.Ldata:
    mov r5, #1
.Ldata_loop:
    DECODE_BIT .Ldata_loop_refill
    adc r5, r5, r5
    movs r4, r5, lsr #8
    beq .Ldata_loop
    b .Lstore

REFILL_BIT .Ldata_loop

// decodes a length field
upkr_decode_length:
    mov r4, #0
    mvn r6, #0
.Lbit_loop:
    add r5, r5, #1
.Lbit_loop_1:
    DECODE_BIT .Lbit_loop_1_refill
    addcc r4, r4, r6
    bxcc lr

    add r5, r5, #1
.Lbit_loop_2:
    DECODE_BIT .Lbit_loop_2_refill
    addcs r4, r4, r6
    mov r6, r6, lsl #1
    b .Lbit_loop

REFILL_BIT .Lbit_loop_1
REFILL_BIT .Lbit_loop_2

// bit decoding function
.refill:
    ldrb r8, [r1], #1
    orr r2, r8, r2, lsl#8
upkr_decode_bit:
    DECODE_BIT .refill
    bx lr

.retfn:
    add sp, $384
    pop { r1, r3-r12, lr }

    sub r0, r0, r1     // Return number of emitted bytes
    bx lr

.pool

