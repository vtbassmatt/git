#!/bin/sh

test_description='test git worktree init-worktree-config'

. ./test-lib.sh

test_expect_success setup '
	git init base &&
	test_commit -C base commit &&
	git -C base worktree add --detach worktree
'

reset_config_when_finished () {
	test_when_finished git -C base config --unset core.repositoryFormatVersion &&
	test_when_finished git -C base config --unset extensions.worktreeConfig &&
	rm -rf base/.git/config.worktree &&
	rm -rf base/.git/worktrees/worktree/config.worktree
}

test_expect_success 'upgrades repo format and adds extension' '
	reset_config_when_finished &&
	git -C base worktree init-worktree-config >out 2>err &&
	test_must_be_empty out &&
	test_must_be_empty err &&
	test_cmp_config -C base 1 core.repositoryFormatVersion &&
	test_cmp_config -C base true extensions.worktreeConfig
'

test_expect_success 'relocates core.worktree' '
	reset_config_when_finished &&
	mkdir dir &&
	git -C base config core.worktree ../../dir &&
	git -C base worktree init-worktree-config >out 2>err &&
	test_must_be_empty out &&
	test_must_be_empty err &&
	test_cmp_config -C base 1 core.repositoryFormatVersion &&
	test_cmp_config -C base true extensions.worktreeConfig &&
	test_cmp_config -C base ../../dir core.worktree &&
	test_must_fail git -C worktree core.worktree
'

test_expect_success 'relocates core.bare' '
	reset_config_when_finished &&
	git -C base config core.bare true &&
	git -C base worktree init-worktree-config >out 2>err &&
	test_must_be_empty out &&
	test_must_be_empty err &&
	test_cmp_config -C base 1 core.repositoryFormatVersion &&
	test_cmp_config -C base true extensions.worktreeConfig &&
	test_cmp_config -C base true core.bare &&
	test_must_fail git -C worktree core.bare
'

test_expect_success 'skips upgrade is already upgraded' '
	reset_config_when_finished &&
	git -C base worktree init-worktree-config &&
	git -C base config core.bare true &&

	# this should be a no-op, even though core.bare
	# makes the worktree be broken.
	git -C base worktree init-worktree-config >out 2>err &&
	test_must_be_empty out &&
	test_must_be_empty err &&
	test_must_fail git -C base config --worktree core.bare &&
	git -C base config core.bare
'

test_done
