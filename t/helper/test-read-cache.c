#include "test-tool.h"
#include "cache.h"
#include "config.h"
#include "sparse-index.h"

static void print_cache_entry(struct cache_entry *ce)
{
	printf("%06o ", ce->ce_mode & 0777777);

	if (S_ISSPARSEDIR(ce->ce_mode))
		printf("tree ");
	else if (S_ISGITLINK(ce->ce_mode))
		printf("commit ");
	else
		printf("blob ");

	printf("%s\t%s\n",
	       oid_to_hex(&ce->oid),
	       ce->name);
}

static void print_cache(struct index_state *cache)
{
	int i;
	for (i = 0; i < the_index.cache_nr; i++)
		print_cache_entry(the_index.cache[i]);
}

int cmd__read_cache(int argc, const char **argv)
{
	struct repository *r = the_repository;
	int i, cnt = 1;
	const char *name = NULL;
	int table = 0, expand = 0;

	initialize_the_repository();
	prepare_repo_settings(r);
	r->settings.command_requires_full_index = 0;

	for (++argv, --argc; *argv && starts_with(*argv, "--"); ++argv, --argc) {
		if (skip_prefix(*argv, "--print-and-refresh=", &name))
			continue;
		if (!strcmp(*argv, "--table"))
			table = 1;
		else if (!strcmp(*argv, "--expand"))
			expand = 1;
	}

	if (argc == 1)
		cnt = strtol(argv[0], NULL, 0);
	setup_git_directory();
	git_config(git_default_config, NULL);

	for (i = 0; i < cnt; i++) {
		repo_read_index(r);

		if (expand)
			ensure_full_index(r->index);

		if (name) {
			int pos;

			refresh_index(r->index, REFRESH_QUIET,
				      NULL, NULL, NULL);
			pos = index_name_pos(r->index, name, strlen(name));
			if (pos < 0)
				die("%s not in index", name);
			printf("%s is%s up to date\n", name,
			       ce_uptodate(r->index->cache[pos]) ? "" : " not");
			write_file(name, "%d\n", i);
		}
		if (table)
			print_cache(r->index);
		discard_index(r->index);
	}
	return 0;
}
