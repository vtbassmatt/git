#!/bin/sh

test_description='git status color option'

. ./test-lib.sh

test_expect_success setup '
	echo 1 >original &&
	git add .
'

# Normal git status does not pipe colors
test_expect_success 'git status' '
	git status >out &&
	test_i18ngrep "original$" out
'

# Test new color option with never (expect same as above)
test_expect_success 'git status --color=never' '
	git status --color=never >out &&
	test_i18ngrep "original$" out
'

# Test new color (default is always)
test_expect_success 'git status --color' '
	git status --color |
	test_decode_color >out &&
	test_i18ngrep "original<RESET>$" out
'

# Test new color option with always
test_expect_success 'git status --color=always' '
	git status --color=always |
	test_decode_color >out &&
	test_i18ngrep "original<RESET>$" out
'

# Test verbose (default)
test_expect_success 'git status -v' '
	git status -v |
	test_decode_color >out &&
	test_i18ngrep "+1" out
'

# Test verbose --color=never
test_expect_success 'git status -v --color=never' '
	git status -v --color=never |
	test_decode_color >out &&
	test_i18ngrep "+1" out
'

# Test verbose --color (default always)
test_expect_success 'git status -v --color' '
	git status -v --color |
	test_decode_color >out &&
	test_i18ngrep "<CYAN>@@ -0,0 +1 @@<RESET>" out &&
	test_i18ngrep "<GREEN>+<RESET><GREEN>1<RESET>" out
'

test_done
# Test verbose --color=always
test_expect_success 'git status -v --color=always' '
	git status -v --color=always |
	test_decode_color >out &&
	test_i18ngrep "<CYAN>@@ -0,0 +1 @@<RESET>" out &&
	test_i18ngrep "GREEN>+<RESET><GREEN>1<RESET>" out
'

test_done
