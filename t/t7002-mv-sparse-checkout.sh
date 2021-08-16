#!/bin/sh

test_description='git mv in sparse working trees'

. ./test-lib.sh

test_expect_success 'setup' "
	mkdir -p sub/dir sub/dir2 &&
	touch a b c sub/d sub/dir/e sub/dir2/e &&
	git add -A &&
	git commit -m files &&

	cat >sparse_error_header <<-EOF &&
	The following pathspecs didn't match any eligible path, but they do match index
	entries outside the current sparse checkout:
	EOF

	cat >sparse_hint <<-EOF
	hint: Disable or modify the sparsity rules or use the --sparse option if you intend to update such entries.
	hint: Disable this message with \"git config advice.updateSparsePath false\"
	EOF
"

test_expect_success 'mv refuses to move sparse-to-sparse' '
	rm -f e &&
	git reset --hard &&
	git sparse-checkout set a &&
	touch b &&
	test_must_fail git mv b e 2>stderr &&
	cat sparse_error_header >expect &&
	echo b >>expect &&
	echo e >>expect &&
	cat sparse_hint >>expect &&
	test_cmp expect stderr &&
	git mv --sparse b e 2>stderr &&
	test_must_be_empty stderr
'

test_expect_success 'mv refuses to move sparse-to-sparse, ignores failure' '
	rm -f e &&
	git reset --hard &&
	git sparse-checkout set a &&
	touch b &&
	git mv -k b e 2>stderr &&
	cat sparse_error_header >expect &&
	echo b >>expect &&
	echo e >>expect &&
	cat sparse_hint >>expect &&
	test_cmp expect stderr &&
	git mv --sparse -k b e 2>stderr &&
	test_must_be_empty stderr
'

test_expect_success 'mv refuses to move non-sparse-to-sparse' '
	rm -f e &&
	git reset --hard &&
	git sparse-checkout set a &&
	test_must_fail git mv a e 2>stderr &&
	cat sparse_error_header >expect &&
	echo e >>expect &&
	cat sparse_hint >>expect &&
	test_cmp expect stderr &&
	git mv --sparse a e 2>stderr &&
	test_must_be_empty stderr
'

test_expect_success 'mv refuses to move sparse-to-non-sparse' '
	rm -f e &&
	git reset --hard &&
	git sparse-checkout set a e &&
	touch b &&
	test_must_fail git mv b e 2>stderr &&
	cat sparse_error_header >expect &&
	echo b >>expect &&
	cat sparse_hint >>expect &&
	test_cmp expect stderr &&
	git mv --sparse b e 2>stderr &&
	test_must_be_empty stderr
'

test_expect_success 'recursive mv refuses to move (possible) sparse' '
	rm -f e &&
	git reset --hard &&
	# Without cone mode, "sub" and "sub2" do not match
	git sparse-checkout set sub/dir sub2/dir &&
	test_must_fail git mv sub sub2 2>stderr &&
	cat sparse_error_header >expect &&
	echo sub >>expect &&
	echo sub2 >>expect &&
	cat sparse_hint >>expect &&
	test_cmp expect stderr &&
	git mv --sparse sub sub2 2>stderr &&
	test_must_be_empty stderr &&
	git commit -m "moved sub to sub2" &&
	git rev-parse HEAD~1:sub >expect &&
	git rev-parse HEAD:sub2 >actual &&
	test_cmp expect actual &&
	git reset --hard HEAD~1
'

test_expect_success 'recursive mv refuses to move sparse' '
	git reset --hard &&
	# Use cone mode so "sub/" matches the sparse-checkout patterns
	git sparse-checkout init --cone &&
	git sparse-checkout set sub/dir sub2/dir &&
	test_must_fail git mv sub sub2 2>stderr &&
	cat sparse_error_header >expect &&
	echo sub/dir2/e >>expect &&
	echo sub2/dir2/e >>expect &&
	cat sparse_hint >>expect &&
	test_cmp expect stderr &&
	git mv --sparse sub sub2 2>stderr &&
	test_must_be_empty stderr &&
	git commit -m "moved sub to sub2" &&
	git rev-parse HEAD~1:sub >expect &&
	git rev-parse HEAD:sub2 >actual &&
	test_cmp expect actual &&
	git reset --hard HEAD~1
'

test_done
