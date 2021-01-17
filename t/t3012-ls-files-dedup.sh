#!/bin/sh

test_description='git ls-files --deduplicate test'

. ./test-lib.sh

test_expect_success 'setup' '
	>a.txt &&
	>b.txt &&
	>delete.txt &&
	git add a.txt b.txt delete.txt &&
	git commit -m master:1 &&
	echo a >a.txt &&
	echo b >b.txt &&
	echo delete >delete.txt &&
	git add a.txt b.txt delete.txt &&
	git commit -m master:2 &&
	git checkout HEAD~ &&
	git switch -c dev &&
	test_when_finished "git switch master" &&
	echo change >a.txt &&
	git add a.txt &&
	git commit -m dev:1 &&
	test_must_fail git merge master &&
	git ls-files --deduplicate >actual &&
	cat >expect <<-\EOF &&
	a.txt
	b.txt
	delete.txt
	EOF
	test_cmp expect actual &&
	rm delete.txt &&
	git ls-files -d -m --deduplicate >actual &&
	cat >expect <<-\EOF &&
	a.txt
	delete.txt
	EOF
	test_cmp expect actual &&
	git ls-files -d -m -t  --deduplicate >actual &&
	cat >expect <<-\EOF &&
	C a.txt
	C a.txt
	C a.txt
	R delete.txt
	C delete.txt
	EOF
	test_cmp expect actual &&
	git ls-files -d -m -c  --deduplicate >actual &&
	cat >expect <<-\EOF &&
	a.txt
	b.txt
	delete.txt
	EOF
	test_cmp expect actual &&
	git merge --abort
'
test_done
