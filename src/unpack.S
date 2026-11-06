@
@ Copyright 2024 David Guillen Fandos <david@davidgf.net>
@
@ Redistribution and use in source and binary forms, with or without
@ modification, are permitted provided that the following conditions are met:
@
@ 1. Redistributions of source code must retain the above copyright notice,
@    this list of conditions and the following disclaimer.
@
@ 2. Redistributions in binary form must reproduce the above copyright notice,
@    this list of conditions and the following disclaimer in the documentation
@    and/or other materials provided with the distribution.
@
@ 3. Neither the name of the copyright holder nor the names of its contributors
@    may be used to endorse or promote products derived from this software
@    without specific prior written permission.
@
@ THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS “AS IS”
@ AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
@ IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
@ ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
@ LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
@ CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
@ SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
@ INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
@ CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
@ ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
@ POSSIBILITY OF SUCH DAMAGE.


@ aplib-derived unpacker for ARM7TDMI
@ Version based on different depackers and some "new" ideas.
@ This is a format similar to aplib but not compatible anymore.
@ Use tools/apultra.cc to generate streams.
@ There are two versions of the depacking routine: regular byte-access routine
@ that works at a byte level, and a 16 bit bus friendly version that uses
@ 16 bit accesses and takes care of alignment problems, so it can be used with
@ VRAM/SDRAM (only performs strh half-word accesses on the output buffer).

@ The routines are optimized for speed (and size).

.align 4
.arm

@ r0 is source buffer
@ r1 is destination buffer

@ r2-r4 temp registers
@ r5 last long offset (set by 110, used by 10)
@ r6 one if last code was literal ("0") or single byte ("111"), aka pair
@ r10 current working byte (to extract bits from)
@ r11 mask register (signals when we run out of bytes)

@ Extracts next bit into Z flag (MSB goes first)
#define nextbitzf                                                             \
  rors r11, $1;              /* Rotate mask */                                \
  ldrcsb r10, [r0], #1;      /* Load next byte if we consumed the byte */     \
  tst r10, r11;              /* Check current bit */

@ Reads a far match offset into r5, r2 is the gamma value and r6 the pair flag.
#define readfaroffset                                                         \
  sub r3, r2, $2;            /* high bits = value - 2 - pair */               \
  sub r3, r3, r6;                                                             \
  lsl r3, r3, $1;            /* Two extra raw offset bits */                  \
  nextbitzf; orrne r3, r3, $1;                                                \
  lsl r3, r3, $1;                                                             \
  nextbitzf; orrne r3, r3, $1;                                                \
  ldrb r5, [r0], #1;         /* read one byte to use as LSB */                \
  orr r5, r5, r3, lsl #8;    /* join bytes  */                                \
  lsl r5, r5, $1;            /* offset is encoded in halfwords */

#ifdef _APPACK_BYTE_VERSION

.global apunpack8
apunpack8:
  push {r4-r6, r10, r11, lr}

  mov r4, r1             @ Save original dst ptr for later
  ldrb r2, [r0], #1      @ First byte is unconditionly copied
  strb r2, [r1], #1
  mov r6, $1             @ First byte is literal

  ldr r11, =0x01010101   @ So we do not have to reload every time (use ror)

_main_loop:              @ Decode prefix code and process each case
  nextbitzf
  beq _token_0           @ Prefix "0"
  nextbitzf
  beq _token_10          @ Prefix "10"
  nextbitzf
  beq _token_110         @ Prefix "110"


  @ Prefix "111": Copies a previous byte with 4 bit offset.
  mov r2, $0
  nextbitzf; orrne r2, $0x8   @ Extract 4 bits (MSB first)
  nextbitzf; orrne r2, $0x4
  nextbitzf; orrne r2, $0x2
  nextbitzf; orrne r2, $0x1
  @ If the 4 bit field is zero, emit a zero byte instead
  cmp r2, $0
  ldrneb r2, [r1, -r2]      @ Load byte from output buffer.
  strb r2, [r1], #1         @ Emit new byte
  mov r6, $1                @ Literal mark
  b _main_loop

  @ Prefix "110", parse offset and emit 2-3 byte seq copy (also EOS mark)
_token_110:
  ldrb r2, [r0], #1             @ Consume one input byte
  lsrs r5, r2, $1               @ Extract offset (7 MSB)
  beq 2f                        @ Offset == 0 means EOS
  ldrb r2, [r1, -r5]            @ Copy two bytes at offset
  strb r2, [r1], #1
  ldrb r2, [r1, -r5]
  strb r2, [r1], #1
  ldrcsb r2, [r1, -r5]          @ Optionally a third byte if LSB was set
  strcsb r2, [r1], #1

  mov r6, $0   @ Not a literal
  b _main_loop

  @ Prefix "0", simply emit next byte
_token_0:
  ldrb r2, [r0], #1
  strb r2, [r1], #1
  mov r6, $1   @ Literal mark
  b _main_loop

  @ Prefix "10", parse offset and length, emit a previous byte seq
_token_10:
  bl _decode_gamma                     @ Read gamma coded value
  cmp r6, $1                           @ if not pair
  cmpeq r2, $2                         @ or value != 2
  bne _long_off                        @ Assume a long offset

  @ Pick last offset, and read a length to copy
  bl _decode_gamma
  b 1f                                 @ Jump to copying loop

_long_off:
  readfaroffset

  bl _decode_gamma                     @ read length to copy
  cmp r5, $0x80                        @ Add some +1 or +2 depending on offset
  addlt r2, $2
  cmp r5, $0x500
  addge r2, $1
  cmp r5, $0x7D00
  addge r2, $1

  1:                                   @ Copy r2 bytes from -r5 offset
    ldrb r3, [r1, -r5]
    strb r3, [r1], #1
    subs r2, r2, $1
    bne 1b
  
  mov r6, $0   @ Not a literal
  b _main_loop

2:
  sub r0, r1, r4                       @ Return the number of output bytes
  pop {r4-r6, r10, r11, lr}
  bx lr

#endif // _APPACK_BYTE_VERSION



@ 16bit version: output buffer must be 16 bit aligned
@ Uses two decompression bodies that handle aligned/unaligned out ptr.
@ Branches across the two bodies are placed so that the alignment assumptions
@ are maintained (since the number of emitted bytes is very well defined).

@ r0 is source buffer
@ r1 is destination buffer

@ r2-r4 temp registers
@ r5 last long offset (set by 110, used by 10)
@ r6 one if last code was literal ("0") or single byte ("111"), aka pair
@ r7 previous written byte (for faster 16 bit writes)
@ r10 current working byte (to extract bits from)
@ r11 mask register (signals when we run out of bytes)

#ifdef _APPACK_HWORD_VERSION

.global apunpack16
apunpack16:
  push {r4-r7, r10, r11, lr}

  mov r4, r1             @ Save original dst ptr for later
  ldrb r7, [r0], #1      @ First byte is unconditionly copied
  strh r7, [r1], #1
  mov r6, $1             @ First byte is literal

  ldr r11, =0x01010101   @ So we do not have to reload every time (use ror)

_main_loop_unal:         @ Decode prefix code and process each case
  nextbitzf
  beq _token_0_unal      @ Prefix "0"
  nextbitzf
  beq _token_10_unal     @ Prefix "10"
  nextbitzf
  beq _token_110_unal    @ Prefix "110"


  @ Prefix "111": Copies a previous byte with 4 bit offset.
  mov r2, $0
  nextbitzf; orrne r2, $0x8   @ Extract 4 bits (MSB first)
  nextbitzf; orrne r2, $0x4
  nextbitzf; orrne r2, $0x2
  nextbitzf; orrne r2, $0x1
  @ If the 4 bit field is zero, emit a zero byte instead
  cmp r2, $0
  ldrneb r2, [r1, -r2]      @ Load byte from output buffer.
  orr r2, r7, r2, lsl #8    @ Place previous byte in the LSB
  strh r2, [r1, #-1]        @ Emit byte (write halfword)
  add r1, $1
  mov r6, $1   @ Literal mark
  b _main_loop_al           @ Buffer is now aligned (+1 byte!)

  @ Prefix "110", parse offset and emit 2-3 byte seq copy (also EOS mark)
_token_110_unal:
  mov r6, $0                    @ Not a literal
  ldrb r2, [r0], #1             @ Consume one input byte
  lsrs r5, r2, $1               @ Extract offset (7 MSB)
  beq 2f                        @ Offset == 0 means EOS

  ldrb r2, [r1, -r5]            @ Copy one byte (use previous too)
  orr r2, r7, r2, lsl #8        @ Place previous byte in the LSB
  strh r2, [r1, #-1]            @ Emit byte (write halfword)
  add r1, $1
  ldrb r7, [r1, -r5]            @ Copy another byte, place it in r7
  strh r7, [r1], #1

  bcc _main_loop_unal           @ if two bytes only, branch now

  ldrb r2, [r1, -r5]
  orr r2, r7, r2, lsl #8        @ Place previous byte in the LSB
  strh r2, [r1, #-1]
  add r1, $1
  b _main_loop_al

  @ Prefix "0", simply emit next byte
_token_0_unal:
  ldrb r2, [r0], #1
  orr r2, r7, r2, lsl #8        @ Place previous byte in the LSB
  strh r2, [r1, #-1]            @ Emit byte (write halfword)
  add r1, $1
  mov r6, $1   @ Literal mark
  b _main_loop_al               @ Buffer is now aligned (+1 byte!)

  @ Prefix "10", parse offset and length, emit a previous byte seq
_token_10_unal:
  bl _decode_gamma                     @ Read gamma coded value
  cmp r6, $1                           @ if not pair
  cmpeq r2, $2                         @ or value != 2
  bne _long_off_unal                   @ Assume a long offset

  @ Pick last offset, and read a length to copy
  mov r6, $0                           @ set: Not a literal
  bl _decode_gamma
  b 1f                                 @ Jump to copying loop

_long_off_unal:
  readfaroffset
  mov r6, $0                           @ set: Not a literal

  bl _decode_gamma                     @ read length to copy
  cmp r5, $0x80                        @ Add some +1 or +2 depending on offset
  addlt r2, $2
  cmp r5, $0x500
  addge r2, $1
  cmp r5, $0x7D00
  addge r2, $1

  @ Loop over N bytes, unaligned then aligned
  1:                                   @ Copy r2 bytes from -r5 offset
    ldrb r3, [r1, -r5]
    orr r3, r7, r3, lsl #8             @ Place previous byte in the LSB
    strh r3, [r1, #-1]
    add r1, $1
    subs r2, r2, $1
    beq _main_loop_al                  @ Done, output is aligned!

    ldrb r7, [r1, -r5]
    strh r7, [r1], #1
    subs r2, r2, $1
    bne 1b                             @ Not done yet.
  
  b _main_loop_unal


_main_loop_al:           @ Decode prefix code and process each case
  nextbitzf
  beq _token_0_al        @ Prefix "0"
  nextbitzf
  beq _token_10_al       @ Prefix "10"
  nextbitzf
  beq _token_110_al      @ Prefix "110"


  @ Prefix "111": Copies a previous byte with 4 bit offset.
  mov r7, $0
  nextbitzf; orrne r7, $0x8   @ Extract 4 bits (MSB first)
  nextbitzf; orrne r7, $0x4
  nextbitzf; orrne r7, $0x2
  nextbitzf; orrne r7, $0x1
  @ If the 4 bit field is zero, emit a zero byte instead
  cmp r7, $0
  ldrneb r7, [r1, -r7]      @ Load byte from output buffer.
  strh r7, [r1], #1         @ Emit byte (write halfword)
  mov r6, $1                @ Literal mark
  b _main_loop_unal         @ Buffer is now unaligned

  @ Prefix "110", parse offset and emit 2-3 byte seq copy (also EOS mark)
_token_110_al:
  mov r6, $0                    @ Not a literal
  ldrb r2, [r0], #1             @ Consume one input byte
  lsrs r5, r2, $1               @ Extract offset (7 MSB)
  beq 2f                        @ Offset == 0 means EOS

  ldrb r7, [r1, -r5]            @ Emit two bytes
  strh r7, [r1], #1             @ Out buf is aligned
  ldrb r2, [r1, -r5]
  orr r2, r7, r2, lsl #8        @ Place previous byte in the LSB
  strh r2, [r1, #-1]
  add r1, $1

  bcc _main_loop_al             @ If two bytes, buffer remains aligned

  ldrb r7, [r1, -r5]            @ Copy one more byte
  strh r7, [r1], #1             @ Emit byte (write halfword)
  b _main_loop_unal


  @ Prefix "0", simply emit next byte
_token_0_al:
  ldrb r7, [r0], #1
  strh r7, [r1], #1             @ Emit byte (write halfword)
  mov r6, $1                    @ Literal mark
  b _main_loop_unal             @ Buffer is now unaligned

  @ Prefix "10", parse offset and length, emit a previous byte seq
_token_10_al:
  bl _decode_gamma                     @ Read gamma coded value
  cmp r6, $1                           @ if not pair
  cmpeq r2, $2                         @ or value != 2
  bne _long_off_al                     @ Assume a long offset

  @ Pick last offset, and read a length to copy
  mov r6, $0                           @ set: Not a literal
  bl _decode_gamma
  b 1f                                 @ Jump to copying loop

_long_off_al:
  readfaroffset
  mov r6, $0                           @ set: Not a literal

  bl _decode_gamma                     @ read length to copy
  cmp r5, $0x80                        @ Add some +1 or +2 depending on offset
  addlt r2, $2
  cmp r5, $0x500
  addge r2, $1
  cmp r5, $0x7D00
  addge r2, $1

  @ Loop over N bytes, aligned then unaligned
  1:                                   @ Copy r2 bytes from -r5 offset
    ldrb r7, [r1, -r5]
    strh r7, [r1], #1
    subs r2, r2, $1
    beq _main_loop_unal                @ Done, output is unaligned!

    ldrb r3, [r1, -r5]
    orr r3, r7, r3, lsl #8             @ Place previous byte in the LSB
    strh r3, [r1, #-1]
    add r1, $1
    subs r2, r2, $1

    bne 1b                             @ Not done yet.
  
  b _main_loop_al

2:
  sub r0, r1, r4                       @ Return the number of output bytes
  pop {r4-r7, r10, r11, lr}
  bx lr

#endif // #ifdef _APPACK_HWORD_VERSION

#if defined(_APPACK_BYTE_VERSION) || defined(_APPACK_HWORD_VERSION)

@ Gamma encoded numbers (of arbitrary length). They use twice as many bits
@ to encode (but smaller numbers use less bits).
@ Sequence "abcd" would be encoded as "a1b1c2d0".
_decode_gamma:
  mov r2, $1
1:
  lsl r2, r2, $1            @ Multiply current number by two
  nextbitzf                 @ Extract one bit
  orrne r2, r2, $1          @ If bit is set, then set the LSB as well.

  @ Go check next bit, if it's zero, we are done.
  nextbitzf                 @ Extract one bit
  bne 1b

  bx lr

#endif

