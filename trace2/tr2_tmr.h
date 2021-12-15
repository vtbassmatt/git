#ifndef TR2_TM_H
#define TR2_TM_H

#include "trace2.h"
#include "trace2/tr2_tgt.h"

/*
 * Define a mechanism to allow "stopwatch" timers.
 *
 * Timers can be used to measure "interesting" activity that does not
 * fit the "region" model, such as code called from many different
 * regions (like zlib) and/or where data for individual calls are not
 * interesting or are too numerous to be efficiently logged.
 *
 * Timer values are accumulated during program execution and emitted
 * to the Trace2 logs at program exit.
 *
 * To make this model efficient, we define a compile-time fixed set of
 * timers and timer ids.  This lets us avoid the complexities of
 * dynamically allocating a timer on demand and sharing that
 * definition with other threads.
 *
 * Timer values are stored in a fixed size "timer block" inside the
 * TLS CTX.  This allows data to be collected on a thread-by-thread
 * basis without locking.
 *
 * We define (at compile time) a set of "timer ids" to access the
 * various timers inside the fixed size "timer block".
 *
 * Timer definitions include the Trace2 "category" and similar fields.
 * This eliminates the need to include those args on the various timer
 * APIs.
 *
 * Timer results are summarized and emitted by the main thread at
 * program exit by iterating over the global list of CTX data.
 */

/*
 * The definition of an individual timer and used by an individual
 * thread.
 */
struct tr2tmr_timer {
	/*
	 * Total elapsed time for this timer in this thread.
	 */
	uint64_t total_us;

	/*
	 * The maximum and minimum interval values observed for this
	 * timer in this thread.
	 */
	uint64_t min_us;
	uint64_t max_us;

	/*
	 * The value of the clock when this timer was started in this
	 * thread.  (Undefined when the timer is not active in this
	 * thread.)
	 */
	uint64_t start_us;

	/*
	 * Number of times that this timer has been started and stopped
	 * in this thread.  (Recursive starts are ignored.)
	 */
	size_t interval_count;

	/*
	 * Number of nested starts on the stack in this thread.  (We
	 * ignore recursive starts and use this to track the recursive
	 * calls.)
	 */
	unsigned int recursion_count;

	/*
	 * Has data from multiple threads been combined into this object.
	 */
	unsigned int is_aggregate:1;
};

/*
 * A compile-time fixed-size block of timers to insert into the TLS CTX.
 *
 * We use this simple wrapper around the array of timer instances to
 * avoid C syntax quirks and the need to pass around an additional size_t
 * argument.
 */
struct tr2tmr_block {
	struct tr2tmr_timer timer[TRACE2_NUMBER_OF_TIMERS];

	/*
	 * Has data from multiple threads been combined into this block.
	 */
	unsigned int is_aggregate:1;
};

/*
 * Private routines used by trace2.c to actually start/stop an individual
 * timer in the current thread.
 */
void tr2tmr_start(enum trace2_timer_id tid);
void tr2tmr_stop(enum trace2_timer_id tid);

/*
 * Accumulate timer data from source block into the merged block.
 */
void tr2tmr_aggregate_timers(struct tr2tmr_block *merged,
			     const struct tr2tmr_block *src);

/*
 * Send stopwatch data for all of the timers in this block to the
 * target.
 *
 * This will generate an event record for each timer that had activity
 * during the program's execution.
 */
void tr2tmr_emit_block(tr2_tgt_evt_timer_t *pfn, uint64_t us_elapsed_absolute,
		       const struct tr2tmr_block *blk, const char *thread_name);

#endif /* TR2_TM_H */
