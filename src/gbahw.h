/*
 * Copyright (C) 2024 David Guillen Fandos <david@davidgf.net>
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see
 * <http://www.gnu.org/licenses/>.
 */

#include <stdint.h>

#define SCREEN_WIDTH        240
#define SCREEN_HEIGHT       160

#define NDS_SCREEN_WIDTH    256
#define NDS_SCREEN_HEIGHT   192

#define KEY_BUTTA        0x0001
#define KEY_BUTTB        0x0002
#define KEY_BUTTSEL      0x0004
#define KEY_BUTTSTA      0x0008
#define KEY_BUTTRIGHT    0x0010
#define KEY_BUTTLEFT     0x0020
#define KEY_BUTTUP       0x0040
#define KEY_BUTTDOWN     0x0080
#define KEY_BUTTR        0x0100
#define KEY_BUTTL        0x0200

#define DISPSTAT_VBLANK      0x0001
#define DISPSTAT_HBLANK      0x0002
#define DISPSTAT_VBLANK_IRQ  0x0008

#define DMA_ENABLE       0x8000
#define DMA_TRANSFER32   0x0400
#define DMA_DST_INC      0x0000
#define DMA_DST_DEC      0x0020
#define DMA_DST_FIXED    0x0040
#define DMA_SRC_INC      0x0000
#define DMA_SRC_DEC      0x0080
#define DMA_SRC_FIXED    0x0100

#define REG_IE           (*((volatile uint16_t *) 0x04000200))
#define REG_IF           (*((volatile uint16_t *) 0x04000202))
#define REG_IME          (*((volatile uint16_t *) 0x04000208))
#define REG_WAITCNT      (*((volatile uint16_t *) 0x04000204))
#define REG_MEMCTRL      (*((volatile uint16_t *) 0x04000800))

#define REG_DISPCNT      (*((volatile uint16_t *) 0x04000000))
#define REG_DISPSTAT     (*((volatile uint16_t *) 0x04000004))
#define REG_VCOUNT       (*((volatile uint16_t *) 0x04000006))
#define REG_BGxCNT(n)    (*((volatile uint16_t *)(0x04000008 + 2*(n))))
#define REG_BLDCNT       (*((volatile uint16_t *) 0x04000050))
#define REG_BLDALPHA     (*((volatile uint16_t *) 0x04000052))
#define REG_BLDY         (*((volatile uint16_t *) 0x04000054))
#define REG_BGxHOFS(n)   (*((volatile uint16_t *)(0x04000010 + 4*(n))))
#define REG_BGxVOFS(n)   (*((volatile uint16_t *)(0x04000012 + 4*(n))))
#define REG_BG2PA        (*((volatile uint16_t *) 0x04000020))
#define REG_BG2PB        (*((volatile uint16_t *) 0x04000022))
#define REG_BG2PC        (*((volatile uint16_t *) 0x04000024))
#define REG_BG2PD        (*((volatile uint16_t *) 0x04000026))
#define REG_WIN0H        (*((volatile uint16_t *) 0x04000040))
#define REG_WIN0V        (*((volatile uint16_t *) 0x04000044))
#define REG_WININ        (*((volatile uint16_t *) 0x04000048))
#define REG_WINOUT       (*((volatile uint16_t *) 0x0400004A))

#define REG_BG2X         (*((volatile uint32_t *) 0x04000028))
#define REG_BG2Y         (*((volatile uint32_t *) 0x0400002C))

#define REG_KEYINPUT     (*((volatile uint16_t *) 0x04000130))

#define REG_IE           (*((volatile uint16_t *) 0x04000200))

#define DMA_SAD(n)       (*(((volatile uint32_t *)(0x040000B0) + (n) * 3)))
#define DMA_DAD(n)       (*(((volatile uint32_t *)(0x040000B4) + (n) * 3)))
#define DMA_CTL(n)       (*(((volatile uint32_t *)(0x040000B8) + (n) * 3)))

#define MEM_PALETTE_SIZE (96*1024)
#define MEM_PALETTE      (((volatile uint16_t *) 0x05000000))

#define MEM_VRAM_SIZE    (96*1024)
#define MEM_VRAM         (((volatile uint16_t *) 0x06000000))
#define MEM_VRAM_U8      (((volatile  uint8_t *) 0x06000000))
#define MEM_VRAM_OBJS    (((volatile  uint8_t *) 0x06014000))
#define MEM_VRAM_BG3(n)  (((volatile uint16_t *)(0x06000000 + 38400*(n))))

#define MEM_OAM          (((volatile uint16_t *) 0x07000000))

// Not really a reg, but handle it like one
#define REG_IRQ_HANDLER_ADDR      (*((volatile uint32_t *) 0x03007FFC))

#define RGB2GBA(color)   (            \
  (((color) & 0xF80000) >> 19) |      \
  (((color) & 0x00F800) >>  6) |      \
  (((color) & 0x0000F8) <<  7)        \
)

#define dup8(v)  ( (v) | ((v) << 8) )
#define dup16(v) ( (v) | ((v) << 16) )

void dma_memset16(volatile void *ptr, uint16_t value, uint16_t count);
void dma_memcpy16(volatile void *dst, const void *src, uint16_t count);
void dma_memcpy32(volatile void *dst, const void *src, uint16_t count);




