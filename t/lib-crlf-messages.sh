# Setup refs with commit and tag messages containing CRLF

LIB_CRLF_BRANCHES=""

create_crlf_ref () {
	message="$1" &&
	subject="$2" &&
	body="$3" &&
	branch="$4" &&
	printf "${message}" >.crlf-message-${branch}.txt &&
	printf "${subject}" >.crlf-subject-${branch}.txt &&
	printf "${body}" >.crlf-body-${branch}.txt &&
	LIB_CRLF_BRANCHES="${LIB_CRLF_BRANCHES} ${branch}"
	test_tick &&
	hash=$(git commit-tree HEAD^{tree} -p HEAD -F .crlf-message-${branch}.txt) &&
	git branch ${branch} ${hash} &&
	git tag tag-${branch} ${branch} -F .crlf-message-${branch}.txt --cleanup=verbatim
}

create_crlf_refs () {
	message="Subject first line\r\n\r\nBody first line\r\nBody second line\r\n" &&
	body="Body first line\r\nBody second line\r\n" &&
	subject="Subject first line" &&
	branch="crlf" &&
	create_crlf_ref "${message}" "${subject}" "${body}" "${branch}" &&
	message="Subject first line\r\n\r\n\r\nBody first line\r\nBody second line\r\n" &&
	branch="crlf-empty-lines-after-subject" &&
	create_crlf_ref "${message}" "${subject}" "${body}" "${branch}" &&
	message="Subject first line\r\nSubject second line\r\n\r\nBody first line\r\nBody second line\r\n" &&
	subject="Subject first line Subject second line" &&
	branch="crlf-two-line-subject" &&
	create_crlf_ref "${message}" "${subject}" "${body}" "${branch}" &&
	message="Subject first line\r\nSubject second line" &&
	subject="Subject first line Subject second line" &&
	body="" &&
	branch="crlf-two-line-subject-no-body" &&
	create_crlf_ref "${message}" "${subject}" "${body}" "${branch}" &&
	message="Subject first line\r\nSubject second line\r\n" &&
	branch="crlf-two-line-subject-no-body-trailing-newline" &&
	create_crlf_ref "${message}" "${subject}" "${body}" "${branch}" &&
	message="Subject first line\r\nSubject second line\r\n\r" &&
	branch="crlf-two-line-subject-no-body-trailing-newline2" &&
	create_crlf_ref "${message}" "${subject}" "${body}" "${branch}"
}

test_create_crlf_refs () {
	test_expect_success 'setup refs with CRLF commit messages' '
		create_crlf_refs
	'
}

cleanup_crlf_refs () {
	for branch in ${LIB_CRLF_BRANCHES}; do
		git branch -D ${branch} &&
		git tag -d tag-${branch} &&
		rm .crlf-message-${branch}.txt &&
		rm .crlf-subject-${branch}.txt &&
		rm .crlf-body-${branch}.txt
	done
}

test_cleanup_crlf_refs () {
	test_expect_success 'cleanup refs with CRLF commit messages' '
		cleanup_crlf_refs
	'
}

test_crlf_subject_body_and_contents() {
	command_and_args="$@" &&
	command=$1 &&
	if [ ${command} = "branch" ] || [ ${command} = "for-each-ref" ] || [ ${command} = "tag" ]; then
		atoms="(contents:subject) (contents:body) (contents)"
	elif [ ${command} = "log" ] || [ ${command} = "show" ]; then
		atoms="s b B"
	fi &&
	files="subject body message" &&
	while  [ -n "${atoms}" ]; do
		set ${atoms} && atom=$1 && shift && atoms="$*" &&
		set ${files} &&	file=$1 && shift && files="$*" &&
		test_expect_success "${command}: --format='%${atom}' works with CRLF input" "
			rm -f expect &&
			for ref in ${LIB_CRLF_BRANCHES}; do
				cat .crlf-${file}-\"\${ref}\".txt >>expect &&
				printf \"\n\" >>expect
			done &&
			git $command_and_args --format=\"%${atom}\" >actual &&
			test_cmp expect actual
		"
	done
}
