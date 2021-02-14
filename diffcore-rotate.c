/*
 * Copyright (C) 2021, Google LLC.
 * Based on diffcore-order.c, which is Copyright (C) 2005, Junio C Hamano
 */
#include "cache.h"
#include "diff.h"
#include "diffcore.h"

void diffcore_rotate(const char *rotate_to_filename)
{
	struct diff_queue_struct *q = &diff_queued_diff;
	struct diff_queue_struct outq;
	int rotate_to, i;

	if (!q->nr)
		return;

	for (i = 0; i < q->nr; i++)
		if (strcmp(rotate_to_filename, q->queue[i]->two->path) <= 0)
			break;
	/* we did not find the specified path */
	if (q->nr <= i)
		return;

	DIFF_QUEUE_CLEAR(&outq);
	rotate_to = i;

	for (i = rotate_to; i < q->nr; i++)
		diff_q(&outq, q->queue[i]);
	for (i = 0; i < rotate_to; i++)
		diff_q(&outq, q->queue[i]);

	free(q->queue);
	*q = outq;
}
