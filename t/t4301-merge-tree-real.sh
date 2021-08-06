#!/bin/sh

test_description='git merge-tree --real'

. ./test-lib.sh

# This test is ort-specific
GIT_TEST_MERGE_ALGORITHM=ort
export GIT_TEST_MERGE_ALGORITHM

test_expect_success setup '
	test_write_lines 1 2 3 4 5 >numbers &&
	echo hello >greeting &&
	echo foo >whatever &&
	git add numbers greeting whatever &&
	git commit -m initial &&

	git branch side1 &&
	git branch side2 &&

	git checkout side1 &&
	test_write_lines 1 2 3 4 5 6 >numbers &&
	echo hi >greeting &&
	echo bar >whatever &&
	git add numbers greeting whatever &&
	git commit -m rename-and-modify &&

	git checkout side2 &&
	test_write_lines 0 1 2 3 4 5 >numbers &&
	echo yo >greeting &&
	git rm whatever &&
	mkdir whatever &&
	>whatever/empty &&
	git add numbers greeting whatever/empty &&
	git commit -m remove-and-rename
'

test_expect_success 'Content merge and a few conflicts' '
	git checkout side1^0 &&
	test_must_fail git merge side2 &&
	cp .git/AUTO_MERGE EXPECT &&
	E_TREE=$(cat EXPECT) &&

	git reset --hard &&
	test_must_fail git merge-tree --real side1 side2 >RESULT &&
	R_TREE=$(cat RESULT) &&

	# Due to differences of e.g. "HEAD" vs "side1", the results will not
	# exactly match.  Dig into individual files.

	# Numbers should have three-way merged cleanly
	test_write_lines 0 1 2 3 4 5 6 >expect &&
	git show ${R_TREE}:numbers >actual &&
	test_cmp expect actual &&

	# whatever and whatever~<branch> should have same HASHES
	git rev-parse ${E_TREE}:whatever ${E_TREE}:whatever~HEAD >expect &&
	git rev-parse ${R_TREE}:whatever ${R_TREE}:whatever~side1 >actual &&
	test_cmp expect actual &&

	# greeting should have a merge conflict
	git show ${E_TREE}:greeting >tmp &&
	cat tmp | sed -e s/HEAD/side1/ >expect &&
	git show ${R_TREE}:greeting >actual &&
	test_cmp expect actual
'

test_expect_success 'Barf on misspelled option' '
	# Mis-spell with single "s" instead of double "s"
	test_expect_code 129 git merge-tree --real --mesages FOOBAR side1 side2 2>expect &&

	grep "error: unknown option.*mesages" expect
'

test_expect_success 'Barf on too many arguments' '
	test_expect_code 129 git merge-tree --real side1 side2 side3 2>expect &&

	grep "^usage: git merge-tree" expect
'

test_done
