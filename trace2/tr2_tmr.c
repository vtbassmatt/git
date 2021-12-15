#include "cache.h"
#include "thread-utils.h"
#include "trace2/tr2_tls.h"
#include "trace2/tr2_tmr.h"

/*
 * Define metadata for each stopwatch timer.  This list must match the
 * set defined in "enum trace2_timer_id".
 */
struct tr2tmr_def {
	const char *category;
	const char *name;

	unsigned int want_thread_events:1;
};

static struct tr2tmr_def tr2tmr_def_block[TRACE2_NUMBER_OF_TIMERS] = {
	[TRACE2_TIMER_ID_TEST1] = { "test", "test1", 0 },
	[TRACE2_TIMER_ID_TEST2] = { "test", "test2", 1 },
};

void tr2tmr_start(enum trace2_timer_id tid)
{
	struct tr2tls_thread_ctx *ctx = tr2tls_get_self();
	struct tr2tmr_timer *t = &ctx->timers.timer[tid];

	t->recursion_count++;
	if (t->recursion_count > 1)
		return; /* ignore recursive starts */

	t->start_us = getnanotime() / 1000;
}

void tr2tmr_stop(enum trace2_timer_id tid)
{
	struct tr2tls_thread_ctx *ctx = tr2tls_get_self();
	struct tr2tmr_timer *t = &ctx->timers.timer[tid];
	uint64_t us_now;
	uint64_t us_interval;

	assert(t->recursion_count > 0);

	t->recursion_count--;
	if (t->recursion_count > 0)
		return; /* still in recursive call */

	us_now = getnanotime() / 1000;
	us_interval = us_now - t->start_us;

	t->total_us += us_interval;

	if (!t->interval_count) {
		t->min_us = us_interval;
		t->max_us = us_interval;
	} else {
		if (us_interval < t->min_us)
			t->min_us = us_interval;
		if (us_interval > t->max_us)
			t->max_us = us_interval;
	}

	t->interval_count++;
}

void tr2tmr_aggregate_timers(struct tr2tmr_block *merged,
			     const struct tr2tmr_block *src)
{
	enum trace2_timer_id tid;

	for (tid = 0; tid < TRACE2_NUMBER_OF_TIMERS; tid++) {
		struct tr2tmr_timer *t_merged = &merged->timer[tid];
		const struct tr2tmr_timer *t = &src->timer[tid];

		t_merged->is_aggregate = 1;

		if (t->recursion_count > 0) {
			/*
			 * A thread exited with a stopwatch running.
			 *
			 * NEEDSWORK: should we assert or throw a warning
			 * for the open interval.  I'm going to ignore it
			 * and keep going because we may have valid data
			 * for previously closed intervals on this timer.
			 */
		}

		if (!t->interval_count)
			continue; /* this timer was not used by this thread. */

		t_merged->total_us += t->total_us;

		if (!t_merged->interval_count) {
			t_merged->min_us = t->min_us;
			t_merged->max_us = t->max_us;
		} else {
			if (t->min_us < t_merged->min_us)
				t_merged->min_us = t->min_us;
			if (t->max_us > t_merged->max_us)
				t_merged->max_us = t->max_us;
		}

		t_merged->interval_count += t->interval_count;
	}

	merged->is_aggregate = 1;
}

void tr2tmr_emit_block(tr2_tgt_evt_timer_t *pfn, uint64_t us_elapsed_absolute,
		       const struct tr2tmr_block *blk, const char *thread_name)
{
	enum trace2_timer_id tid;

	for (tid = 0; tid < TRACE2_NUMBER_OF_TIMERS; tid++) {
		const struct tr2tmr_timer *t = &blk->timer[tid];
		const struct tr2tmr_def *d = &tr2tmr_def_block[tid];

		if (!t->interval_count)
			continue; /* timer was not used */

		if (!d->want_thread_events && !t->is_aggregate)
			continue; /* per-thread events not wanted */

		pfn(us_elapsed_absolute, thread_name, d->category, d->name,
		    t->interval_count, t->total_us, t->min_us, t->max_us);
	}
}
