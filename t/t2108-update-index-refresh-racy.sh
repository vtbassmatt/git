#!/bin/sh

test_description='update-index refresh tests related to racy timestamps'

. ./test-lib.sh

reset_mtime() {
	test-tool chmtime =$(test-tool chmtime --get .git/fs-tstamp) $1
}

update_assert_unchanged() {
	local ts1=$(test-tool chmtime --get .git/index) &&
	git update-index $1 &&
	local ts2=$(test-tool chmtime --get .git/index) &&
	[ $ts1 -eq $ts2 ]
}

update_assert_changed() {
	local ts1=$(test-tool chmtime --get .git/index) &&
	test_might_fail git update-index $1 &&
	local ts2=$(test-tool chmtime --get .git/index) &&
	[ $ts1 -ne $ts2 ]
}

test_expect_success 'setup' '
	touch .git/fs-tstamp &&
	test-tool chmtime -1 .git/fs-tstamp &&
	echo content >file &&
	reset_mtime file &&

	git add file &&
	git commit -m "initial import"
'

test_expect_success '--refresh has no racy timestamps to fix' '
	reset_mtime .git/index &&
	test-tool chmtime +1 .git/index &&
	update_assert_unchanged --refresh
'

test_expect_success '--refresh should fix racy timestamp' '
	reset_mtime .git/index &&
	update_assert_changed --refresh
'

test_expect_success '--really-refresh should fix racy timestamp' '
	reset_mtime .git/index &&
	update_assert_changed --really-refresh
'

test_expect_success '--refresh should fix racy timestamp even if needs update' '
	echo content2 >file &&
	reset_mtime file &&
	reset_mtime .git/index &&
	update_assert_changed --refresh
'

test_done
