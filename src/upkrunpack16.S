.arm

// Runs from IWRAM: ARM code fetched from EWRAM is several times slower.
.section .iwram.text, "ax", %progbits

.global upkr_unpack16
.type upkr_unpack16, %function
// 16 bit bus version: only strh (half-word) writes are performed on the
// output buffer, which must be 16 bit aligned. One garbage byte may be
// written past the end of the output (same deal as the aplib depacker).
//
// r0 .. out_ptr (returns number of emitted bytes)
// r1 .. in_ptr
// r2 .. state
// r3 .. -offset
// r4 .. prev_was_literal / decode_length ret (negated)
// r5 .. context index / byte temp
// r6 .. decode_length temp
// r7 .. last emitted byte (only valid/needed while out_ptr is odd)
// r8-r9 .. decode_bit temp
// r10 .. store address temp
// r12 .. decode_length return address
upkr_unpack16:
    push { r0, r3-r12, lr }

    sub sp, $384
    mov r2, $384
    mov r3, $128
    orr r3, r3, r3, lsl #8
    orr r3, r3, r3, lsl #16
.Lclear:
    subs r2, r2, #4
    str r3, [sp, r2]
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

    @ Copy -r4 bytes from [r0 + r3]. The loop is unrolled x2 and split in
    @ two halves so every store is a half-word store on an even address.
    @ Entered on either half depending on the current out_ptr alignment.
    tst r0, #1
    bne 2f
1:  ldrb r7, [r0, r3]             @ Aligned: byte goes into the LSB
    strh r7, [r0], #1             @ (one garbage byte written at r0+1)
    adds r4, r4, #1
    beq .Lloop                    @ Done, out_ptr odd and r7 is live
2:  ldrb r5, [r0, r3]             @ Unaligned: pack with the previous byte
    orr r5, r7, r5, lsl #8
    strh r5, [r0, #-1]            @ Fixes up the garbage byte above
    add r0, r0, #1
    adds r4, r4, #1
    blt 1b
    b .Lloop                      @ Done, out_ptr aligned

.Ldata:
    mov r5, #1

.Ldata_loop:
    bl upkr_decode_bit
    adc r5, r5, r5
    movs r4, r5, lsr #8           @ r4 != 0 marks "previous was a literal"
    beq .Ldata_loop

    @ Emit one literal byte (r5 holds 0x100 | byte). Branchless, since the
    @ store is noise next to the ~8 arithmetic-decoder calls above.
    and r5, r5, #255
    tst r0, #1
    orrne r5, r7, r5, lsl #8      @ Unaligned: prepend the previous byte
    moveq r7, r5                  @ Aligned: remember it for the next store
    bic r10, r0, #1
    strh r5, [r10]
    add r0, r0, #1
    b .Lloop

.type upkr_decode_length, %function
upkr_decode_length:
    mov r12, lr

    mov r4, #0
    mvn r6, #0
.Lbit_loop:
    bl upkr_decode_bit_inc
    addcc r4, r4, r6
    bxcc r12

    bl upkr_decode_bit_inc
    addcs r4, r4, r6
    mov r6, r6, lsl #1
    b .Lbit_loop

.type upkr_decode_bit, %function
upkr_decode_bit_inc:
    add r5, r5, #1
upkr_decode_bit:
    cmp r2, #4096
    ldrltb r8, [r1], #1
    orrlt r2, r8, r2, lsl#8
    blt upkr_decode_bit

    ldrb r8, [sp, r5]
    and r9, r2, #255
    add r9, r9, #1
    cmp r8, r9
    rsbcs r8, r8, #256
    mvn r9, r2, lsr#8
    addcs r9, r9, #1
    mla r2, r8, r9, r2
    add r9, r8, #8
    sub r8, r8, r9, lsr#4
    rsbcs r8, r8, #256
    strb r8, [sp, r5]
    bx lr

.retfn:
    add sp, $384
    pop { r1, r3-r12, lr }

    sub r0, r0, r1     // Return number of emitted bytes
    bx lr

.pool

