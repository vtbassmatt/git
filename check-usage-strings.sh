{
  if test -d ".git"
  then
    rev=${1:-"HEAD"}
    for entry in $(git grep -l 'struct option .* = {$' "$rev" -- \*.c);
    do
      git show "$entry" |
      sed -n '/struct option .* = {/,/OPT_END/{=;p;}' |
      sed "N;s/^\\([0-9]*\\)\\n/$(echo "$entry" | sed 's/\//\\&/g'):\\1/";
    done
  else
    for entry in $(grep -rl --include="*.c" 'struct option .* = {$' . );
    do
      cat "$entry" |
      sed -n '/struct option .* = {/,/OPT_END/{=;p;}' |
      sed "N;s/^\\([0-9]*\\)\\n/$(echo "$entry" | sed -e 's/\//\\&/g' -e 's/^\.\\\///'):\\1/";
    done
  fi
} |
grep -Pe '((?<!OPT_GROUP\(N_\(|OPT_GROUP\()"(?!GPG|DEPRECATED|SHA1|HEAD)[A-Z]|(?<!"|\.\.)\.")' |
{
  status=0
  while read content;
  do
    if test -n "$content"
    then
      echo "$content";
      status=1;
    fi
  done

  exit $status
}
