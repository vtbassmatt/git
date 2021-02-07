#!/bin/sh
# git-difftool--helper is a GIT_EXTERNAL_DIFF-compatible diff tool launcher.
# This script is typically launched by using the 'git difftool'
# convenience command.
#
# Copyright (c) 2009, 2010 David Aguilar

TOOL_MODE=diff
GIT_DIFFTOOL_LAST_POSITION="$GIT_DIR/difftool-last-position"
DIFFTOOL_FIRST_NUM="1"
. git-mergetool--lib

# difftool.prompt controls the default prompt/no-prompt behavior
# and is overridden with $GIT_DIFFTOOL*_PROMPT.
should_prompt () {
	prompt_merge=$(git config --bool mergetool.prompt || echo true)
	prompt=$(git config --bool difftool.prompt || echo $prompt_merge)
	if test "$prompt" = true
	then
		test -z "$GIT_DIFFTOOL_NO_PROMPT"
	else
		test -n "$GIT_DIFFTOOL_PROMPT"
	fi
}

# Indicates that --extcmd=... was specified
use_ext_cmd () {
	test -n "$GIT_DIFFTOOL_EXTCMD"
}

launch_merge_tool () {
	# Merged is the filename as it appears in the work tree
	# Local is the contents of a/filename
	# Remote is the contents of b/filename
	# Custom merge tool commands might use $BASE so we provide it
	MERGED="$1"
	LOCAL="$2"
	REMOTE="$3"
	BASE="$1"

	# $LOCAL and $REMOTE are temporary files so prompt
	# the user with the real $MERGED name before launching $merge_tool.
	if should_prompt
	then
		if test -f "$GIT_DIFFTOOL_LAST_POSITION"
		then
			if SAVE_POINT_NUM=$(cat 2>/dev/null "$GIT_DIFFTOOL_LAST_POSITION") &&
				test "$SAVE_POINT_NUM" -le "$GIT_DIFF_PATH_TOTAL" &&
					test "$SAVE_POINT_NUM" -gt "$GIT_DIFF_PATH_COUNTER"
			then
				if test "$GIT_DIFF_PATH_COUNTER" -eq "$DIFFTOOL_FIRST_NUM"
				then
					printf "Do you want to start from the possible last file you were viewing? [Y/n]?"
					read skip_ans || return
					if test "$skip_ans" = y
					then
						return
					fi
				else
					return
				fi
			fi
		fi
		if test -z "$SAVE_POINT_NUM" ||
			test "$SAVE_POINT_NUM" -ne "$GIT_DIFF_PATH_COUNTER"
		then
			echo "$GIT_DIFF_PATH_COUNTER" >"$GIT_DIFFTOOL_LAST_POSITION"
		fi
		printf "\nViewing (%s/%s): '%s'\n" "$GIT_DIFF_PATH_COUNTER" \
			"$GIT_DIFF_PATH_TOTAL" "$MERGED"
		if use_ext_cmd
		then
			printf "Launch '%s' [Y/n]? " \
				"$GIT_DIFFTOOL_EXTCMD"
		else
			printf "Launch '%s' [Y/n]? " "$merge_tool"
		fi
		read ans || return
		if test "$ans" = n
		then
			return
		fi
	fi

	if use_ext_cmd
	then
		export BASE
		eval $GIT_DIFFTOOL_EXTCMD '"$LOCAL"' '"$REMOTE"'
	else
		run_merge_tool "$merge_tool"
	fi
}

if ! use_ext_cmd
then
	if test -n "$GIT_DIFF_TOOL"
	then
		merge_tool="$GIT_DIFF_TOOL"
	else
		merge_tool="$(get_merge_tool)"
	fi
fi

if test -n "$GIT_DIFFTOOL_DIRDIFF"
then
	LOCAL="$1"
	REMOTE="$2"
	run_merge_tool "$merge_tool" false
else
	# Launch the merge tool on each path provided by 'git diff'
	while test $# -gt 6
	do
		launch_merge_tool "$1" "$2" "$5"
		status=$?
		if test $status -ge 126
		then
			# Command not found (127), not executable (126) or
			# exited via a signal (>= 128).
			exit $status
		fi

		if test "$status" != 0 &&
			test "$GIT_DIFFTOOL_TRUST_EXIT_CODE" = true
		then
			exit $status
		fi
		shift 7
	done
fi

if test "$GIT_DIFF_PATH_COUNTER" -eq "$GIT_DIFF_PATH_TOTAL"
then
	rm -f "$GIT_DIFFTOOL_LAST_POSITION"

fi
exit 0
