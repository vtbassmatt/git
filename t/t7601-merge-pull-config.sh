#!/bin/sh

test_description='git merge

Testing pull.* configuration parsing.'

. ./test-lib.sh

test_expect_success 'setup' '
	echo c0 >c0.c &&
	git add c0.c &&
	git commit -m c0 &&
	git tag c0 &&
	echo c1 >c1.c &&
	git add c1.c &&
	git commit -m c1 &&
	git tag c1 &&
	git reset --hard c0 &&
	echo c2 >c2.c &&
	git add c2.c &&
	git commit -m c2 &&
	git tag c2 &&
	git reset --hard c0 &&
	echo c3 >c3.c &&
	git add c3.c &&
	git commit -m c3 &&
	git tag c3
'

test_expect_success 'pull.rebase not set' '
	git reset --hard c0 &&
	git pull . c1 2>err &&
	test_i18ngrep ! "Pulling without specifying how to reconcile" err
'

test_expect_success 'pull.rebase not set and pull.ff=true' '
	git reset --hard c0 &&
	test_config pull.ff true &&
	git pull . c1 2>err &&
	test_i18ngrep ! "Pulling without specifying how to reconcile" err
'

test_expect_success 'pull.rebase not set and pull.ff=false' '
	git reset --hard c0 &&
	test_config pull.ff false &&
	git pull . c1 2>err &&
	test_i18ngrep ! "Pulling without specifying how to reconcile" err
'

test_expect_success 'pull.rebase not set and pull.ff=only' '
	git reset --hard c0 &&
	test_config pull.ff only &&
	git pull . c1 2>err &&
	test_i18ngrep ! "Pulling without specifying how to reconcile" err
'

test_expect_success 'pull.rebase not set and --rebase given' '
	git reset --hard c0 &&
	git pull --rebase . c1 2>err &&
	test_i18ngrep ! "Pulling without specifying how to reconcile" err
'

test_expect_success 'pull.rebase not set and --no-rebase given' '
	git reset --hard c0 &&
	git pull --no-rebase . c1 2>err &&
	test_i18ngrep ! "Pulling without specifying how to reconcile" err
'

test_expect_success 'pull.rebase not set and --ff given' '
	git reset --hard c0 &&
	git pull --ff . c1 2>err &&
	test_i18ngrep ! "Pulling without specifying how to reconcile" err
'

test_expect_success 'pull.rebase not set and --no-ff given' '
	git reset --hard c0 &&
	git pull --no-ff . c1 2>err &&
	test_i18ngrep ! "Pulling without specifying how to reconcile" err
'

test_expect_success 'pull.rebase not set and --ff-only given' '
	git reset --hard c0 &&
	git pull --ff-only . c1 2>err &&
	test_i18ngrep ! "Pulling without specifying how to reconcile" err
'

test_expect_success 'pull.rebase not set (not-fast-forward)' '
	git reset --hard c2 &&
	git -c color.advice=always pull . c1 2>err &&
	test_decode_color <err >decoded &&
	test_i18ngrep "<YELLOW>hint: " decoded &&
	test_i18ngrep "Pulling without specifying how to reconcile" decoded
'

test_expect_success 'pull.rebase not set and pull.ff=true (not-fast-forward)' '
	git reset --hard c2 &&
	test_config pull.ff true &&
	git pull . c1 2>err &&
	test_i18ngrep ! "Pulling without specifying how to reconcile" err
'

test_expect_success 'pull.rebase not set and pull.ff=false (not-fast-forward)' '
	git reset --hard c2 &&
	test_config pull.ff false &&
	git pull . c1 2>err &&
	test_i18ngrep ! "Pulling without specifying how to reconcile" err
'

test_expect_success 'pull.rebase not set and pull.ff=only (not-fast-forward)' '
	git reset --hard c2 &&
	test_config pull.ff only &&
	test_must_fail git pull . c1 2>err &&
	test_i18ngrep ! "Pulling without specifying how to reconcile" err
'

test_expect_success 'pull.rebase not set and --rebase given (not-fast-forward)' '
	git reset --hard c2 &&
	git pull --rebase . c1 2>err &&
	test_i18ngrep ! "Pulling without specifying how to reconcile" err
'

test_expect_success 'pull.rebase not set and --no-rebase given (not-fast-forward)' '
	git reset --hard c2 &&
	git pull --no-rebase . c1 2>err &&
	test_i18ngrep ! "Pulling without specifying how to reconcile" err
'

test_expect_success 'pull.rebase not set and --ff given (not-fast-forward)' '
	git reset --hard c2 &&
	git pull --ff . c1 2>err &&
	test_i18ngrep ! "Pulling without specifying how to reconcile" err
'

test_expect_success 'pull.rebase not set and --no-ff given (not-fast-forward)' '
	git reset --hard c2 &&
	git pull --no-ff . c1 2>err &&
	test_i18ngrep ! "Pulling without specifying how to reconcile" err
'

test_expect_success 'pull.rebase not set and --ff-only given (not-fast-forward)' '
	git reset --hard c2 &&
	test_must_fail git pull --ff-only . c1 2>err &&
	test_i18ngrep ! "Pulling without specifying how to reconcile" err
'

test_does_rebase() {
	git reset --hard c2 &&
	git "$@" . c1 &&
	# Check that we actually did a rebase
	git rev-list --count HEAD >actual &&
	git rev-list --merges --count HEAD >>actual &&
	test_write_lines 3 0 >expect &&
	test_cmp expect actual &&
	rm actual expect
}

test_does_merge_noff() {
	git reset --hard c0 &&
	git "$@" . c1 &&
	# Check that we actually did a merge
	git rev-list --count HEAD >actual &&
	git rev-list --merges --count HEAD >>actual &&
	test_write_lines 3 1 >expect &&
	test_cmp expect actual &&
	rm actual expect
}

test_does_merge_ff() {
	git reset --hard c0 &&
	git "$@" . c1 &&
	# Check that we actually did a merge
	git rev-list --count HEAD >actual &&
	git rev-list --merges --count HEAD >>actual &&
	test_write_lines 2 0 >expect &&
	test_cmp expect actual &&
	rm actual expect
}

test_does_need_full_merge() {
	git reset --hard c2 &&
	git "$@" . c1 &&
	# Check that we actually did a merge
	git rev-list --count HEAD >actual &&
	git rev-list --merges --count HEAD >>actual &&
	test_write_lines 4 1 >expect &&
	test_cmp expect actual &&
	rm actual expect
}

test_attempts_fast_forward() {
	git reset --hard c2 &&
	test_must_fail git "$@" . c1 2>err &&
	test_i18ngrep "Not possible to fast-forward, aborting" err
}

#
# Rule 1: --ff-only takes precedence over --[no-]rebase
# (Corollary: pull.ff=only overrides pull.rebase)
#
test_expect_success '--ff-only takes precedence over --rebase' '
	test_attempts_fast_forward pull --rebase --ff-only
'

test_expect_success '--ff-only takes precedence over --rebase even if first' '
	test_attempts_fast_forward pull --ff-only --rebase
'

test_expect_success '--ff-only takes precedence over --no-rebase' '
	test_attempts_fast_forward pull --ff-only --no-rebase
'

test_expect_success 'pull.ff=only overrides pull.rebase=true' '
	test_attempts_fast_forward -c pull.ff=only -c pull.rebase=true pull
'

test_expect_success 'pull.ff=only overrides pull.rebase=false' '
	test_attempts_fast_forward -c pull.ff=only -c pull.rebase=false pull
'

# Rule 2: --rebase=[!false] takes precedence over --no-ff and --ff
# (Corollary: pull.rebase=!false overrides pull.ff=!only)
test_expect_success '--rebase takes precedence over --no-ff' '
	test_does_rebase pull --rebase --no-ff
'

test_expect_success '--rebase takes precedence over --ff' '
	test_does_rebase pull --rebase --ff
'

test_expect_success 'pull.rebase=true takes precedence over pull.ff=false' '
	test_does_rebase -c pull.rebase=true -c pull.ff=false pull
'

test_expect_success 'pull.rebase=true takes precedence over pull.ff=true' '
	test_does_rebase -c pull.rebase=true -c pull.ff=true pull
'

# Rule 3: command line flags take precedence over config
test_expect_success '--ff-only takes precedence over pull.rebase=true' '
	test_attempts_fast_forward -c pull.rebase=true pull --ff-only
'

test_expect_success '--ff-only takes precedence over pull.rebase=false' '
	test_attempts_fast_forward -c pull.rebase=false pull --ff-only
'

test_expect_success '--no-rebase overrides pull.ff=only' '
	test_does_need_full_merge -c pull.ff=only pull --no-rebase
'

test_expect_success '--rebase takes precedence over pull.ff=only' '
	test_does_rebase -c pull.ff=only pull --rebase
'

test_expect_success '--rebase takes precedence over pull.ff=true' '
	test_does_rebase -c pull.ff=true pull --rebase
'

test_expect_success '--rebase takes precedence over pull.ff=false' '
	test_does_rebase -c pull.ff=false pull --rebase
'

test_expect_success '--rebase takes precedence over pull.ff unset' '
	test_does_rebase pull --rebase
'

# Rule 4: --no-rebase heeds pull.ff=!only or explict --ff or --no-ff

test_expect_success '--no-rebase works with --no-ff' '
	test_does_merge_noff pull --no-rebase --no-ff
'

test_expect_success '--no-rebase works with --ff' '
	test_does_merge_ff pull --no-rebase --ff
'

test_expect_success '--no-rebase does ff if pull.ff unset' '
	test_does_merge_ff pull --no-rebase
'

test_expect_success '--no-rebase heeds pull.ff=true' '
	test_does_merge_ff -c pull.ff=true pull --no-rebase
'

test_expect_success '--no-rebase heeds pull.ff=false' '
	test_does_merge_noff -c pull.ff=false pull --no-rebase
'

# Rule 5: pull.rebase=!false takes precedence over --no-ff and --ff
test_expect_success 'pull.rebase=true takes precedence over --no-ff' '
	test_does_rebase -c pull.rebase=true pull --no-ff
'

test_expect_success 'pull.rebase=true takes precedence over --ff' '
	test_does_rebase -c pull.rebase=true pull --ff
'

# End of precedence rules

test_expect_failure 'Multiple heads does not warn about fast forwarding' '
	git reset --hard c1 &&
	git pull . c2 c3 2>err &&
	test_i18ngrep ! "Pulling without specifying how to reconcile" err
'

test_expect_success 'Cannot fast-forward with multiple heads' '
	git reset --hard c0 &&
	test_must_fail git -c pull.ff=only pull . c1 c2 c3 2>err &&
	test_i18ngrep ! "Pulling without specifying how to reconcile" err &&
	test_i18ngrep "Not possible to fast-forward, aborting" err
'

test_expect_success 'Cannot rebase with multiple heads' '
	git reset --hard c0 &&
	test_must_fail git -c pull.rebase=true pull . c1 c2 c3 2>err &&
	test_i18ngrep ! "Pulling without specifying how to reconcile" err &&
	test_i18ngrep "Cannot rebase onto multiple branches." err
'

test_expect_success 'merge c1 with c2' '
	git reset --hard c1 &&
	test -f c0.c &&
	test -f c1.c &&
	test ! -f c2.c &&
	test ! -f c3.c &&
	git merge c2 &&
	test -f c1.c &&
	test -f c2.c
'

test_expect_success 'fast-forward pull succeeds with "true" in pull.ff' '
	git reset --hard c0 &&
	test_config pull.ff true &&
	git pull . c1 &&
	test "$(git rev-parse HEAD)" = "$(git rev-parse c1)"
'

test_expect_success 'pull.ff=true overrides merge.ff=false' '
	git reset --hard c0 &&
	test_config merge.ff false &&
	test_config pull.ff true &&
	git pull . c1 &&
	test "$(git rev-parse HEAD)" = "$(git rev-parse c1)"
'

test_expect_success 'fast-forward pull creates merge with "false" in pull.ff' '
	git reset --hard c0 &&
	test_config pull.ff false &&
	git pull . c1 &&
	test "$(git rev-parse HEAD^1)" = "$(git rev-parse c0)" &&
	test "$(git rev-parse HEAD^2)" = "$(git rev-parse c1)"
'

test_expect_success 'pull prevents non-fast-forward with "only" in pull.ff' '
	git reset --hard c1 &&
	test_config pull.ff only &&
	test_must_fail git pull . c3
'

test_expect_success 'merge c1 with c2 (ours in pull.twohead)' '
	git reset --hard c1 &&
	git config pull.twohead ours &&
	git merge c2 &&
	test -f c1.c &&
	! test -f c2.c
'

test_expect_success 'merge c1 with c2 and c3 (recursive in pull.octopus)' '
	git reset --hard c1 &&
	git config pull.octopus "recursive" &&
	test_must_fail git merge c2 c3 &&
	test "$(git rev-parse c1)" = "$(git rev-parse HEAD)"
'

test_expect_success 'merge c1 with c2 and c3 (recursive and octopus in pull.octopus)' '
	git reset --hard c1 &&
	git config pull.octopus "recursive octopus" &&
	git merge c2 c3 &&
	test "$(git rev-parse c1)" != "$(git rev-parse HEAD)" &&
	test "$(git rev-parse c1)" = "$(git rev-parse HEAD^1)" &&
	test "$(git rev-parse c2)" = "$(git rev-parse HEAD^2)" &&
	test "$(git rev-parse c3)" = "$(git rev-parse HEAD^3)" &&
	git diff --exit-code &&
	test -f c0.c &&
	test -f c1.c &&
	test -f c2.c &&
	test -f c3.c
'

conflict_count()
{
	{
		git diff-files --name-only
		git ls-files --unmerged
	} | wc -l
}

# c4 - c5
#    \ c6
#
# There are two conflicts here:
#
# 1) Because foo.c is renamed to bar.c, recursive will handle this,
# resolve won't.
#
# 2) One in conflict.c and that will always fail.

test_expect_success 'setup conflicted merge' '
	git reset --hard c0 &&
	echo A >conflict.c &&
	git add conflict.c &&
	echo contents >foo.c &&
	git add foo.c &&
	git commit -m c4 &&
	git tag c4 &&
	echo B >conflict.c &&
	git add conflict.c &&
	git mv foo.c bar.c &&
	git commit -m c5 &&
	git tag c5 &&
	git reset --hard c4 &&
	echo C >conflict.c &&
	git add conflict.c &&
	echo secondline >> foo.c &&
	git add foo.c &&
	git commit -m c6 &&
	git tag c6
'

# First do the merge with resolve and recursive then verify that
# recursive is chosen.

test_expect_success 'merge picks up the best result' '
	git config --unset-all pull.twohead &&
	git reset --hard c5 &&
	test_must_fail git merge -s resolve c6 &&
	resolve_count=$(conflict_count) &&
	git reset --hard c5 &&
	test_must_fail git merge -s recursive c6 &&
	recursive_count=$(conflict_count) &&
	git reset --hard c5 &&
	test_must_fail git merge -s recursive -s resolve c6 &&
	auto_count=$(conflict_count) &&
	test $auto_count = $recursive_count &&
	test $auto_count != $resolve_count
'

test_expect_success 'merge picks up the best result (from config)' '
	git config pull.twohead "recursive resolve" &&
	git reset --hard c5 &&
	test_must_fail git merge -s resolve c6 &&
	resolve_count=$(conflict_count) &&
	git reset --hard c5 &&
	test_must_fail git merge -s recursive c6 &&
	recursive_count=$(conflict_count) &&
	git reset --hard c5 &&
	test_must_fail git merge c6 &&
	auto_count=$(conflict_count) &&
	test $auto_count = $recursive_count &&
	test $auto_count != $resolve_count
'

test_expect_success 'merge errors out on invalid strategy' '
	git config pull.twohead "foobar" &&
	git reset --hard c5 &&
	test_must_fail git merge c6
'

test_expect_success 'merge errors out on invalid strategy' '
	git config --unset-all pull.twohead &&
	git reset --hard c5 &&
	test_must_fail git merge -s "resolve recursive" c6
'

test_done
