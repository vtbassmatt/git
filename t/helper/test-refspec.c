#include "cache.h"
#include "parse-options.h"
#include "refspec.h"
#include "strbuf.h"
#include "test-tool.h"

static const char * const refspec_usage[] = {
	N_("test-tool refspec [--fetch]"),
	NULL
};

int cmd__refspec(int argc, const char **argv)
{
	struct strbuf line = STRBUF_INIT;
	int fetch = 0;

	struct option refspec_options [] = {
		OPT_BOOL(0, "fetch", &fetch,
			 N_("enable the 'fetch' option for parsing refpecs")),
		OPT_END()
	};

	argc = parse_options(argc, argv, NULL, refspec_options,
			     refspec_usage, 0);

	while (strbuf_getline(&line, stdin) != EOF) {
		struct refspec_item rsi;

		if (!refspec_item_init(&rsi, line.buf, fetch)) {
			printf("failed to parse %s\n", line.buf);
			continue;
		}

		printf("%s\n", refspec_item_format(&rsi));
		refspec_item_clear(&rsi);
	}

	return 0;
}
