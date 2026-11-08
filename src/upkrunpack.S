.arm

.section .text

// Bit decoder: loads the probability in r8, returns the decoded bit in C
// (direct path, no refill occurs here)
.macro DECODE_PROB
    mvn r9, r2, lsl #24      // prov > (state & 255) can be also expressed as:
    cmn r9, r8, lsl #24      // 0xFFFFFFFF + ((prob - state) << 24)
    rsbcs r8, r8, #256
    sbc r9, r11, r2, lsr #8
    mla r2, r9, r8, r2       // mul order matters (can shave a cycle or two)
    add r9, r8, #8
    sub r8, r8, r9, lsr#4
    rsbcs r8, r8, #256
.endm

// Decode bit with refill path (will refill if needed)
.macro DECODE_BIT refill_fn
    cmp r2, #4096
    blt \refill_fn           // brach on refill (happens rarely, ~8%)
    ldrb r8, [sp, r5]
    DECODE_PROB
    strb r8, [sp, r5]
.endm

// Same as above, but uses and advances r7 as table base.
.macro DECODE_BIT_NEXT refill_fn
    cmp r2, #4096
    blt \refill_fn
    ldrb r8, [r7, #1]!
    DECODE_PROB
    strb r8, [r7]
.endm

// Raw refill path, placed right before \lbl so it falls into it.
.macro REFILL_FT lbl
\lbl\()_refill:
    ldrb r8, [r1], #1
    orr r2, r8, r2, lsl #8
.endm

// Refill path that can live anywhere.
.macro REFILL_BIT lbl
    REFILL_FT \lbl
    b \lbl
.endm

.global upkr_unpack
.type upkr_unpack, %function
// r0 .. out_ptr (returned)
// r1 .. in_ptr (returned)
// r2 .. state
// r3 .. offset
// r4 .. prev_was_literal / decode_length ret
// r5 .. context index (literal, flags)
// r6 .. decode_length temp
// r7 .. decode_length context pointer
// r8-r9 .. decode_bit temp
// r11 .. hardcoded to zero
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

    b .Lloop

    // Literal path (placed here so it falls through to main loop)
    // (enters with r5=1)
REFILL_FT .Ldata_loop
.Ldata_loop:
    DECODE_BIT .Ldata_loop_refill
    adc r5, r5, r5
    movs r4, r5, lsr #8
    beq .Ldata_loop
    strb r5, [r0], #1

.Lloop:
    mov r5, #0
    bl upkr_decode_bit
    movcc r5, #1
    bcc .Ldata_loop
.Lmatch:
    mov r5, #256
    rsbs r6, r4, #0
    blcc upkr_decode_bit
    bcc .Lskip_offset

    add r7, sp, #256
    bl upkr_decode_length
    adds r3, r4, #1
    beq .retfn
.Lskip_offset:

    add r7, sp, #256+64
    bl upkr_decode_length
.Lcopy_loop:
    ldrb r5, [r0, r3]
    strb r5, [r0], #1
    adds r4, r4, #1
    blt .Lcopy_loop
    b .Lloop

// decodes a length field, contexts start at r7 + 1
upkr_decode_length:
    mov r4, #0
    mvn r6, #0
.Lbit_loop_1:
    DECODE_BIT_NEXT .Lbit_loop_1_refill
    addcc r4, r4, r6
    bxcc lr

.Lbit_loop_2:
    DECODE_BIT_NEXT .Lbit_loop_2_refill
    addcs r4, r4, r6
    mov r6, r6, lsl #1
    b .Lbit_loop_1

REFILL_BIT .Lbit_loop_1
REFILL_BIT .Lbit_loop_2

// bit decoding function
REFILL_FT upkr_decode_bit
upkr_decode_bit:
    DECODE_BIT upkr_decode_bit_refill
    bx lr

.retfn:
    add sp, $384
    pop { r1, r3-r12, lr }

    sub r0, r0, r1     // Return number of emitted bytes
    bx lr

.pool

