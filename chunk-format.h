#ifndef CHUNK_FORMAT_H
#define CHUNK_FORMAT_H

#include "git-compat-util.h"

struct hashfile;

typedef int (*chunk_write_fn)(struct hashfile *f,
			      void *data);

/*
 * When writing a chunk-based file format, collect the chunks in
 * an array of chunk_info structs. The size stores the _expected_
 * amount of data that will be written by write_fn.
 */
struct chunk_info {
	uint32_t id;
	uint64_t size;
	chunk_write_fn write_fn;
};

/*
 * Write the chunk data into the supplied hashfile.
 *
 * * 'cur_offset' indicates the number of bytes written to the hashfile
 *   before the table of contents starts.
 *
 * * 'nr' is the number of chunks with non-zero IDs, so 'nr + 1'
 *   chunks are written in total.
 */
void write_table_of_contents(struct hashfile *f,
			     uint64_t cur_offset,
			     struct chunk_info *chunks,
			     int nr);

/*
 * Write the data for the given chunk list using the provided
 * write_fn values. The given 'data' parameter is passed to those
 * methods.
 *
 * The data that is written by each write_fn is checked to be of
 * the expected size, and a BUG() is thrown if not specified correctly.
 */
int write_chunks(struct hashfile *f,
		 struct chunk_info *chunks,
		 int nr,
		 void *data);

#endif
