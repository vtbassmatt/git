#!/bin/bash

# This script verifies that the non-binary files tracked in the Git index do
# not contain any Unicode directional formatting: such formatting could be used
# to deceive reviewers into interpreting code differently from the compiler.
# This is intended to run on an Ubuntu agent in a GitHub workflow.
#
# `git grep` as well as GNU grep do not handle `\u` as a way to specify UTF-8.
# A PCRE-enabled `git grep` would handle `\u` as desired, but Ubuntu does
# not build its `git` packages with PCRE support.
#
# To work around that, we use `printf` to produce the pattern as a byte
# sequence, and then feed that to `git grep` as a byte sequence (setting
# `LC_CTYPE` to make sure that the arguments are interpreted as intended).
#
# Note: we need to use Bash here because its `printf` interprets `\uNNNN` as
# UTF-8 code points, as desired. Running this script through Ubuntu's `dash`,
# for example, would use a `printf` that does not understand that syntax.

# U+202a..U+2a2e: LRE, RLE, PDF, LRO and RLO
# U+2066..U+2069: LRI, RLI, FSI and PDI
regex='(\u202a|\u202b|\u202c|\u202d|\u202e|\u2066|\u2067|\u2068|\u2069)'

! git ls-files -z ':(attr:!binary)' |
LC_CTYPE=C xargs -0r git grep -Ele "$(LC_CTYPE=C.UTF-8 printf "$regex")" --
