#!/bin/sh

test_description='git ls-files --dedup test.

This test prepares the following in the cache:

    a.txt       - a file(base)
    a.txt	- a file(master)
    a.txt       - a file(dev)
    b.txt       - a file
    delete.txt  - a file
    expect1	- a file
    expect2	- a file

'

. ./test-lib.sh

test_expect_success 'setup' '
	> a.txt &&
	> b.txt &&
	> delete.txt &&
	cat >expect1<<-\EOF &&
	M a.txt
	H b.txt
	H delete.txt
	H expect1
	H expect2
	EOF
	cat >expect2<<-EOF &&
	C a.txt
	R delete.txt
	EOF
	git add a.txt b.txt delete.txt expect1 expect2 &&
	git commit -m master:1 &&
	echo a>a.txt &&
	echo b>b.txt &&
	echo delete >delete.txt &&
	git add a.txt b.txt delete.txt &&
	git commit -m master:2 &&
	git checkout HEAD~ &&
	git switch -c dev &&
	echo change >a.txt &&
	git add a.txt &&
	git commit -m dev:1 &&
	test_must_fail git merge master &&
	git ls-files -t --dedup >actual1 &&
	test_cmp expect1 actual1 &&
	rm delete.txt &&
	git ls-files -d -m -t --dedup >actual2 &&
	test_cmp expect2 actual2
'

test_done
