#!/bin/sh
# Copies the carts favourited in Splore into the PICO-8 roms folder, named by
# their BBS title. Run by Tools/Splore.pak after each Splore session.
#
#   pico8_copy_favourites.sh <pico8 home> <roms dir>
#
# - favourites.txt is read from <home> and <home>/.lexaloffle/pico-8 (a build that
#   ignores -home nests its data there). PICO-8 0.2.7 writes one row per favourite,
#   |<file> |<slug> |<id> |<author> |... |<title>, padding every field with spaces;
#   older builds wrote |0|bbs/1/<id>.p8.png|<id> with no title. An empty or missing
#   file copies nothing.
# - <file> is the cart's name in the bbs cache without .p8.png: <slug>-<rev>, or a
#   numeric id with no revision. The newest cached revision is copied, so a cart
#   favourited before an update still comes over at its latest revision.
# - The target is <title>.p8.png, made safe for exFAT. A file of that name with the
#   same bytes is taken as the copy; a different one (the user's) is left alone and
#   the cart goes to "<title> (<slug>).p8.png" instead.
# - Copies are recorded in <roms>/.splore/copied.txt as
#   slug<TAB>rev<TAB>filename<TAB>crc/size (cksum of the copy). Only a recorded copy
#   is ever replaced, only by a newer revision, and only while the file still holds
#   what was copied: a file the user changed is left alone. A row from before the
#   checksum column counts as the copy only while the file matches that revision in
#   the bbs cache. A recorded copy the user removed is not brought back. Nothing is
#   deleted.
# - Prints the number of carts copied or updated on stdout (Splore.pak rebuilds the
#   game list only then); everything else goes to the log.
#
# The favourites.txt traps (padded fields, CRLF, empty file, title taken from the
# file rather than the cart's Lua comments) were found by minui-pico-8-pak
# (MIT, issues #111, #113, #117, #123), whose launch.sh this follows.

P8_HOME=${1:?usage: pico8_copy_favourites.sh <pico8 home> <roms dir>}
ROMS=${2:?usage: pico8_copy_favourites.sh <pico8 home> <roms dir>}
TAB=$(printf '\t')

log() {
	if [ -n "${LOGS_PATH:-}" ]; then
		echo "$*" >>"$LOGS_PATH/splore.txt"
	else
		echo "$*" >&2
	fi
}

mkdir -p "$ROMS/.splore" || exit 1
COPIED="$ROMS/.splore/copied.txt"
[ -f "$COPIED" ] || : >"$COPIED"
WORK="${TMPDIR:-/tmp}/pico8_favourites.$$"
rm -rf "$WORK"
mkdir -p "$WORK" || exit 1
trap 'rm -rf "$WORK"' EXIT
trap 'exit 1' INT TERM

# favourites -> slug<TAB>title, one per slug, title already safe as a file name
FAVS=""
for f in "$P8_HOME/favourites.txt" "$P8_HOME/.lexaloffle/pico-8/favourites.txt"; do
	[ -f "$f" ] && FAVS="$FAVS$f$TAB"
done
if [ -z "$FAVS" ]; then
	log "favourites: no favourites.txt under $P8_HOME"
	echo 0
	exit 0
fi
OLDIFS=$IFS
IFS=$TAB
# shellcheck disable=SC2086
LC_ALL=C awk -F'|' '
	function trim(s) { gsub(/^[ \t\r]+|[ \t\r]+$/, "", s); return s }
	# exFAT-safe: reserved characters and controls become -, no leading dot (hidden),
	# no trailing space or dot, at most 120 bytes and never half a UTF-8 character
	function safe(s,    i, j, b, need) {
		gsub(/[\001-\037\177]/, "-", s)
		gsub(/[\/\\:*?"<>|]/, "-", s)
		s = trim(s)
		sub(/^[. ]+/, "", s)
		if (length(s) > 120) {
			s = substr(s, 1, 120)
			for (j = length(s); j > 0 && j > length(s) - 4; j--) {
				b = ord[substr(s, j, 1)]
				if (b < 128 || b >= 192) break
			}
			if (j > 0 && b >= 192) {
				need = b >= 240 ? 4 : b >= 224 ? 3 : 2
				if (length(s) - j + 1 < need) s = substr(s, 1, j - 1)
			}
		}
		sub(/[. ]+$/, "", s)
		return s
	}
	BEGIN { for (i = 128; i < 256; i++) ord[sprintf("%c", i)] = i }
	{
		sub(/\r$/, "")
		# the cart is field 2, or in the old |0|bbs/1/<id>.p8.png|<id> layout (no title
		# column) the first field naming a .p8 file
		stem = trim($2)
		if (NF <= 4 && stem ~ /^[0-9]+$/)
			for (i = 3; i <= NF; i++)
				if (index($i, ".p8")) { stem = trim($i); break }
		sub(/.*\//, "", stem)
		sub(/\.p8(\.png)?$/, "", stem)
		if (stem == "" || stem ~ /^temp-/) next
		slug = stem
		if (match(stem, /-[0-9]+$/) && RSTART > 1) slug = substr(stem, 1, RSTART - 1)
		if (seen[slug]++) next
		title = NF >= 7 ? safe($7) : ""
		if (title == "") title = safe(slug)
		if (title == "") next
		print slug "\t" title
	}' $FAVS >"$WORK/favs"
IFS=$OLDIFS
if [ ! -s "$WORK/favs" ]; then
	log "favourites: none listed"
	echo 0
	exit 0
fi

# the bbs cache -> slug<TAB>rev<TAB>path, newest revision per slug; every revision in all
for b in "$P8_HOME/bbs" "$P8_HOME/.lexaloffle/pico-8/bbs"; do
	[ -d "$b" ] && find "$b" -type f -name '*.p8.png'
done | LC_ALL=C awk -v all="$WORK/all" '
	{
		name = $0
		sub(/.*\//, "", name)
		if (name ~ /^temp-/) next   # partial downloads
		stem = name
		sub(/\.p8\.png$/, "", stem)
		slug = stem; rev = 0
		if (match(stem, /-[0-9]+$/) && RSTART > 1) {
			slug = substr(stem, 1, RSTART - 1)
			rev = substr(stem, RSTART + 1) + 0
		}
		print slug "\t" rev "\t" $0 > all
		if (!(slug in best) || rev > best[slug]) { best[slug] = rev; path[slug] = $0 }
	}
	END { for (s in best) print s "\t" best[s] "\t" path[s] }' >"$WORK/cache"
[ -f "$WORK/all" ] || : >"$WORK/all"

# favourites that are cached -> slug<TAB>rev<TAB>path<TAB>title
LC_ALL=C awk -F'\t' '
	FILENAME == ARGV[1] { rev[$1] = $2; path[$1] = $3; next }
	$1 in rev { print $1 "\t" rev[$1] "\t" path[$1] "\t" $2; next }
	{ print "favourites: " $1 " is not in the bbs cache" > "/dev/stderr" }' \
	"$WORK/cache" "$WORK/favs" >"$WORK/todo" 2>"$WORK/missing"
[ -s "$WORK/missing" ] && while IFS= read -r line; do log "$line"; done <"$WORK/missing"

# sum_of <file>: crc/size from cksum, empty if it cannot be read
sum_of() {
	cksum "$1" 2>/dev/null | awk '{ print $1 "/" $2; exit }'
}

# record <slug> <rev> <filename>: one row per slug in copied.txt, with the copy's sum
record() {
	_sum=$(sum_of "$ROMS/$3")
	awk -F'\t' -v s="$1" '$1 != s' "$COPIED" >"$WORK/copied" &&
		printf '%s\t%s\t%s\t%s\n' "$1" "$2" "$3" "$_sum" >>"$WORK/copied" &&
		cat "$WORK/copied" >"$COPIED"
}

# still_ours <slug> <rev> <sum> <filename>: the recorded copy is unchanged. A row with
# no sum (written before there was one) is ours while the file is that revision's cart.
still_ours() {
	if [ -n "$3" ]; then
		[ "$(sum_of "$ROMS/$4")" = "$3" ]
		return
	fi
	_was=$(awk -F'\t' -v s="$1" -v r="$2" '$1 == s && $2 == r { print $3; exit }' "$WORK/all")
	[ -n "$_was" ] && cmp -s "$_was" "$ROMS/$4"
}

# copy_to <src> <filename>: through a temp file, so the list never sees half a cart
copy_to() {
	cp -f "$1" "$ROMS/.splore/.incoming" && mv -f "$ROMS/.splore/.incoming" "$ROMS/$2"
}

n=0
while IFS=$TAB read -r slug rev src title; do
	[ -n "$slug" ] || continue
	row=$(awk -F'\t' -v s="$slug" '$1 == s { r = $0 } END { print r }' "$COPIED")
	if [ -n "$row" ]; then
		old_rev=$(printf '%s' "$row" | cut -f2)
		old_file=$(printf '%s' "$row" | cut -f3)
		old_sum=$(printf '%s' "$row" | cut -f4)
		if [ ! -f "$ROMS/$old_file" ]; then
			continue # removed by the user: not brought back
		fi
		if [ "$rev" -gt "$old_rev" ] 2>/dev/null; then
			if ! still_ours "$slug" "$old_rev" "$old_sum" "$old_file"; then
				log "favourites: $old_file is not the copy made of $slug, not updating it to revision $rev"
			elif copy_to "$src" "$old_file"; then
				record "$slug" "$rev" "$old_file"
				log "favourites: updated $old_file to revision $rev"
				n=$((n + 1))
			else
				log "favourites: could not update $old_file"
			fi
		fi
		continue
	fi
	target="$title.p8.png"
	if [ -f "$ROMS/$target" ] && ! cmp -s "$src" "$ROMS/$target"; then
		target="$title ($slug).p8.png"
	fi
	if [ -f "$ROMS/$target" ]; then
		if cmp -s "$src" "$ROMS/$target"; then
			record "$slug" "$rev" "$target"
		else
			log "favourites: $target is another file, $slug not copied"
		fi
		continue
	fi
	if copy_to "$src" "$target"; then
		record "$slug" "$rev" "$target"
		log "favourites: copied $slug revision $rev as $target"
		n=$((n + 1))
	else
		log "favourites: could not copy $slug as $target"
	fi
done <"$WORK/todo"

[ "$n" -gt 0 ] && sync
log "favourites: $n copied or updated"
echo "$n"
exit 0
