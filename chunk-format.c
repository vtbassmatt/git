#include "git-compat-util.h"
#include "chunk-format.h"
#include "csum-file.h"
#define CHUNK_LOOKUP_WIDTH 12

void write_table_of_contents(struct hashfile *f,
			     uint64_t cur_offset,
			     struct chunk_info *chunks,
			     int nr)
{
	int i;

	/* Add the table of contents to the current offset */
	cur_offset += (nr + 1) * CHUNK_LOOKUP_WIDTH;

	for (i = 0; i < nr; i++) {
		hashwrite_be32(f, chunks[i].id);
		hashwrite_be64(f, cur_offset);

		cur_offset += chunks[i].size;
	}

	/* Trailing entry marks the end of the chunks */
	hashwrite_be32(f, 0);
	hashwrite_be64(f, cur_offset);
}
