#ifndef CHUNK_FORMAT_H
#define CHUNK_FORMAT_H

#include "git-compat-util.h"

struct hashfile;
struct chunkfile;

struct chunkfile *init_chunkfile(struct hashfile *f);
void free_chunkfile(struct chunkfile *cf);
int get_num_chunks(struct chunkfile *cf);
typedef int (*chunk_write_fn)(struct hashfile *f,
			      void *data);
void add_chunk(struct chunkfile *cf,
	       uint64_t id,
	       chunk_write_fn fn,
	       size_t size);
int write_chunkfile(struct chunkfile *cf, void *data);

int read_table_of_contents(struct chunkfile *cf,
			   const unsigned char *mfile,
			   size_t mfile_size,
			   uint64_t toc_offset,
			   int toc_length);

/*
 * When reading a table of contents, we find the chunk with matching 'id'
 * then call its read_fn to populate the necessary 'data' based on the
 * chunk start and size.
 */
typedef int (*chunk_read_fn)(const unsigned char *chunk_start,
			     size_t chunk_size, void *data);


#define CHUNK_NOT_FOUND (-2)
int pair_chunk(struct chunkfile *cf,
	       uint32_t chunk_id,
	       chunk_read_fn fn,
	       void *data);

#endif
