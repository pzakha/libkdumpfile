/* Routines to read S390 dump files.
   Copyright (C) 2014 Petr Tesarik <ptesarik@suse.cz>

   This file is free software; you can redistribute it and/or modify
   it under the terms of either

     * the GNU Lesser General Public License as published by the Free
       Software Foundation; either version 3 of the License, or (at
       your option) any later version

   or

     * the GNU General Public License as published by the Free
       Software Foundation; either version 2 of the License, or (at
       your option) any later version

   or both in parallel, as here.

   libkdumpfile is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received copies of the GNU General Public License and
   the GNU Lesser General Public License along with this program.  If
   not, see <http://www.gnu.org/licenses/>.
*/

#define _GNU_SOURCE

#include "kdumpfile-priv.h"

#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#define S390_CPU_MAX	512

struct dump_header1 {
	uint64_t magic;
	uint32_t version;
	uint32_t hdr_size;
	uint32_t dump_level;
	uint32_t page_size;
	uint64_t mem_size;
	uint64_t mem_start;
	uint64_t mem_end;
	uint32_t num_pages;
	char     _pad1[4];	/* alignment */
	uint64_t tod;		/* creation timestamp (TOD format) */
	uint64_t cpu_id;
	uint32_t arch;
	uint32_t volnr;
	uint32_t build_arch;
	uint64_t mem_size_real;
	uint8_t  mvdump;
	uint16_t cpu_cnt;
	uint16_t real_cpu_cnt;
} __attribute((packed));

struct dump_header2 {
	uint64_t mvdump_sign;
	uint64_t mvdump_zipl_time;
} __attribute((packed));

struct dump_header {
	struct dump_header1 h1;
	char     _pad2[0x200-sizeof(struct dump_header1)];
	struct dump_header2 h2;
	char     _pad3[0x800-sizeof(struct dump_header2)-0x200];
	uint32_t lowcore[S390_CPU_MAX];
} __attribute((packed));

#define S390_MAGIC	0xa8190173618f23fdULL

#define S390_ARCH_32BIT	1
#define S390_ARCH_64BIT	2

#define END_MARKER	"DUMP_END"

struct end_marker {
	char     str[sizeof(END_MARKER)-1];
	uint64_t tod;
} __attribute__((packed));

struct s390dump_priv {
	off_t dataoff;		/* offset of data (size of s390 header) */
};

static kdump_status
s390_read_page(kdump_ctx *ctx, kdump_pfn_t pfn)
{
	struct s390dump_priv *sdp = ctx->fmtdata;
	kdump_paddr_t addr = pfn * ctx->page_size;
	off_t pos;

	if (pfn == ctx->last_pfn)
		return kdump_ok;

	if (pfn >= ctx->max_pfn)
		return kdump_nodata;

	pos = (off_t)addr + (off_t)sdp->dataoff;
	if (pread(ctx->fd, ctx->page, ctx->page_size, pos) != ctx->page_size)
		return kdump_syserr;

	ctx->last_pfn = pfn;
	return kdump_ok;
}

static kdump_status
s390_probe(kdump_ctx *ctx)
{
	struct dump_header *dh = ctx->buffer;
	struct s390dump_priv *sdp;
	struct end_marker marker;
	off_t pos;
	kdump_status ret;

	if (be64toh(dh->h1.magic) != S390_MAGIC)
		return kdump_unsupported;

	ctx->format = "S390";
	ctx->endian = __BIG_ENDIAN;

	switch (dump32toh(ctx, dh->h1.arch)) {
	case S390_ARCH_32BIT:
		kdump_set_arch(ctx, ARCH_S390);
		break;

	case S390_ARCH_64BIT:
		kdump_set_arch(ctx, ARCH_S390X);
		break;

	default:
		return kdump_unsupported;
	}

	ret = kdump_set_page_size(ctx, dump32toh(ctx, dh->h1.page_size));
	if (ret != kdump_ok)
		return ret;

	ctx->max_pfn = dump32toh(ctx, dh->h1.num_pages);

	pos = dump32toh(ctx, dh->h1.hdr_size) +
		dump64toh(ctx, dh->h1.mem_size);
	if (pread(ctx->fd, &marker, sizeof marker, pos) != sizeof marker)
		return kdump_syserr;
	if (memcmp(marker.str, END_MARKER, sizeof END_MARKER - 1) ||
	    dump64toh(ctx, marker.tod) < dump64toh(ctx, dh->h1.tod))
		return kdump_dataerr;

	sdp = malloc(sizeof *sdp);
	if (!sdp)
		return kdump_syserr;
	ctx->fmtdata = sdp;

	sdp->dataoff = dump32toh(ctx, dh->h1.hdr_size);

	return kdump_ok;
}

static void
s390_cleanup(kdump_ctx *ctx)
{
	struct s390dump_priv *sdp = ctx->fmtdata;

	free(sdp);
	ctx->fmtdata = NULL;
}

const struct format_ops kdump_s390_ops = {
	.probe = s390_probe,
	.read_page = s390_read_page,
	.cleanup = s390_cleanup,
};
