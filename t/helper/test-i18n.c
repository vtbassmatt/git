#include "test-tool.h"
#include "cache.h"
#include "grep.h"

static const char *usage_msg = "\n"
"  test-tool i18n cmp <file1> <file2>\n"
"  test-tool i18n grep <regex> <file>\n";

static inline char do_rot13(char c)
{
	if (c >= 'a' && c <= 'm')
		return c + 'n' - 'a';
	if (c >= 'n' && c <= 'z')
		return c + 'a' - 'n';
	if (c >= 'A' && c <= 'M')
		return c + 'N' - 'A';
	if (c >= 'N' && c <= 'Z')
		return c + 'A' - 'N';
	return c;
}

static size_t unrot13(char *buf)
{
	char *p = buf, *q = buf;

	while (*p) {
		const char *begin = strstr(p, "<rot13>"), *end;

		if (!begin)
			break;

		while (p != begin)
			*(q++) = *(p++);

		p += strlen("<rot13>");
		end = strstr(p, "</rot13>");
		if (!end)
			BUG("could not find </rot13> in\n%s", buf);

		while (p != end)
			*(q++) = do_rot13(*(p++));
		p += strlen("</rot13>");
	}

	while (*p)
		*(q++) = *(p++);

	return q - buf;
}

static void unrot13_strbuf(struct strbuf *buf)
{
	size_t len = unrot13(buf->buf);

	if (len == buf->len)
		die("not ROT13'ed:\n%s", buf->buf);
	buf->len = len;
}

static int i18n_cmp(const char **argv)
{
	const char *path1 = *(argv++), *path2 = *(argv++);
	struct strbuf a = STRBUF_INIT, b = STRBUF_INIT;

	if (!path1 || !path2 || *argv)
		usage(usage_msg);

	if (strbuf_read_file(&a, path1, 0) < 0)
		die_errno("could not read %s", path1);
	if (strbuf_read_file(&b, path2, 0) < 0)
		die_errno("could not read %s", path2);
	unrot13_strbuf(&b);

	if (a.len != b.len || memcmp(a.buf, b.buf, a.len))
		return 1;

	return 0;
}

static int i18n_grep(const char **argv)
{
	int dont_match = 0;
	const char *pattern, *path;

	struct grep_opt opt;
	struct grep_source source;
	struct strbuf buf = STRBUF_INIT;
	int hit;

	if (*argv && !strcmp("!", *argv)) {
		dont_match = 1;
		argv++;
	}

	pattern = *(argv++);
	path = *(argv++);

	if (!pattern || !path || *argv)
		usage(usage_msg);

	grep_init(&opt, the_repository, NULL);
	append_grep_pattern(&opt, pattern, "command line", 0, GREP_PATTERN);
	compile_grep_patterns(&opt);

	if (strbuf_read_file(&buf, path, 0) < 0)
		die_errno("could not read %s", path);
	unrot13_strbuf(&buf);
	grep_source_init(&source, GREP_SOURCE_BUF, path, path, path);
	source.buf = buf.buf;
	source.size = buf.len;
	hit = grep_source(&opt, &source);
	strbuf_release(&buf);
	return dont_match ^ !hit;
}

int cmd__i18n(int argc, const char **argv)
{
	argv++;
	if (!*argv)
		usage(usage_msg);
	if (!strcmp(*argv, "cmp"))
		return i18n_cmp(argv+1);
	else if (!strcmp(*argv, "grep"))
		return i18n_grep(argv+1);
	else
		usage(usage_msg);

	return 0;
}
