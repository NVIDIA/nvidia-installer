/*
 * nvidia-installer: A tool for installing NVIDIA software packages on
 * Unix and Linux systems.
 *
 * Copyright (C) 2003 NVIDIA Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 * 
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses>.
 *
 *
 * crc.c - this source file contains code for generating a 32-bit
 * checksum.  Based on the 16 bit CRC algorithm in Numerical Recipes
 * 2nd Ed., pp 900-901.
 *
 * For a generator, we use the polynomial:
 *
 * x^32 + x^26 + x^23 + x^22 + x^16 + x^12 + x^11 + x^10 + x^8 + x^7 +
 * x^5 + x^4 + x^2 + x^1 + 1.
 *
 * (without bit 32)
 *
 * See the source for cksum in GNU textutils for additional
 * references.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

#include "nvidia-installer.h"
#include "user-interface.h"
#include "misc.h"
#include "crc.h"

#define BIT(x) (1 << (x))
#define CRC_GEN_MASK (BIT(26) | BIT(23) | BIT(22) | BIT(16) | BIT(12) | \
                      BIT(11) | BIT(10) | BIT(8)  | BIT(7)  | BIT(5)  | \
                      BIT(4)  | BIT(2)  | BIT(1)  | BIT(0))


static uint32 crc_init(uint32 crc)
{
    int i;
    uint32 ans = crc;

    for (i=0; i < 8; i++) {
        if (ans & 0x80000000) {
            ans = (ans << 1) ^ CRC_GEN_MASK;
        } else {
            ans <<= 1;
        }
    }
    return ans;

} /* crc_init() */



uint32 compute_crc_from_buffer(const uint8 *buf, int len)
{
    uint32 cword = ~0;
    static uint32 *crctab = NULL;
    int i;

    if (!crctab) {
        crctab = (uint32 *) nvalloc(sizeof(uint32) * 256);
        for (i=0; i < 256; i++) {
            crctab[i] = crc_init(i << 24);
        }
    }

    for (i = 0; i < len; i++) {
        cword = crctab[buf[i] ^ (cword >> 24)] ^ (cword << 8);
    }

    return cword;
}



uint32 compute_crc(Options *op, const char *filename)
{
    uint32 cword = ~0;
    uint8 *buf = MAP_FAILED;
    int success = FALSE;
    int fd;
    struct stat stat_buf;
    size_t len = 0;

    if ((fd = open(filename, O_RDONLY)) == -1) goto done;
    if (fstat(fd, &stat_buf) == -1) goto done;

    if (stat_buf.st_size == 0) {
        cword = 0;
        success = TRUE;
        goto done;
    }
    len = stat_buf.st_size;

    buf = mmap(0, len, PROT_READ, MAP_FILE | MAP_SHARED, fd, 0);
    if (buf == MAP_FAILED) goto done;

    cword = compute_crc_from_buffer(buf, len);

    success = TRUE;

 done:
    if (!success) {
        ui_warn(op, "Unable to compute CRC for file '%s' (%s).",
                filename, strerror(errno));
    }

    if (buf != MAP_FAILED) {
        munmap(buf, len);
    }
    if (fd >= 0) {
        close(fd);
    }
    
    return cword;
        
} /* compute_crc() */
