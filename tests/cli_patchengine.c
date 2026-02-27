/*
 * Copyright (C) 2025 David Guillen Fandos <david@davidgf.net>
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.   See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see
 * <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "patchengine.h"

#define BLK_SIZE     4*1024*1024

static void dummy(unsigned) {}

int main(int argc, char **argv) {
  if (argc <= 1) {
    printf("Usage: %s romfile\n", argv[0]);
    exit(1);
  }

  FILE *fd = fopen(argv[1], "rb");
  if (!fd) {
    printf("Could not open file %s\n", argv[1]);
    exit(1);
  }
  struct stat st;
  stat(argv[1], &st);

  t_patch_builder pb;
  patchengine_init(&pb, st.st_size);
  char *tmp = malloc(BLK_SIZE + 64);   // FIXME: the scanner overruns the buffer sometimes.

  while (true) {
    int r = fread(tmp, 1, BLK_SIZE, fd);
    if (r <= 0)
      break;

    patchengine_process_rom((uint32_t*)tmp, r, &pb, dummy);
  }

  free(tmp);
  patchengine_finalize(&pb);
  fclose(fd);

  // Print patches for manual inspection:
  printf("Save type: %d\n", pb.p.save_mode);

  printf("WAITCNT patches:\n");
  for (unsigned i = 0; i < pb.p.wcnt_ops; i++)
    printf(" %08x\n", pb.p.op[i]);
  printf("SAVE patches:\n");
  for (unsigned i = 0; i < pb.p.save_ops; i++)
    printf(" %08x\n", pb.p.op[pb.p.wcnt_ops + i]);
  printf("IRQ patches:\n");
  for (unsigned i = 0; i < pb.p.irqh_ops; i++)
    printf(" %08x\n", pb.p.op[pb.p.wcnt_ops + pb.p.save_ops + i]);
  printf("RTC patches:\n");
  for (unsigned i = 0; i < pb.p.rtc_ops; i++)
    printf(" %08x\n", pb.p.op[pb.p.wcnt_ops + pb.p.save_ops + pb.p.irqh_ops + i]);
  printf("Hole addr and size: %x %x\n", pb.p.hole_addr, pb.p.hole_size);
}

