#!/bin/sh
# Runs grub-mkconfig style script output through grubcfg-proxy and makes sure
# the result is still valid grub script (menuentries inside of if/fi blocks
# must not be moved out of their block).
#
# usage: proxy-keeps-shell-blocks.sh <path to grubcfg-proxy> <fixture directory>

proxy="$1"
fixtures="$2"
tmp=$(mktemp -d) || exit 1
trap 'rm -rf "$tmp"' EXIT

checker=$(command -v grub-script-check || command -v grub2-script-check)
failed=0

for fixture in "$fixtures"/*.out; do
	name=$(basename "$fixture")
	"$proxy" "+*" < "$fixture" > "$tmp/$name" || { echo "FAIL $name: proxy exited with an error"; failed=1; continue; }

	# every if/fi pair must survive in its original order
	if [ "$(grep -cE '^[[:space:]]*if ' "$fixture")" != "$(grep -cE '^[[:space:]]*if ' "$tmp/$name")" ] ||
	   [ "$(grep -cE '^[[:space:]]*fi$' "$fixture")" != "$(grep -cE '^[[:space:]]*fi$' "$tmp/$name")" ]; then
		echo "FAIL $name: if/fi blocks were changed"
		failed=1
		continue
	fi

	if [ -n "$checker" ] && ! "$checker" "$tmp/$name"; then
		echo "FAIL $name: $checker rejected the proxy output:"
		cat "$tmp/$name"
		failed=1
		continue
	fi
	echo "ok   $name"
done

[ -n "$checker" ] || echo "note: grub-script-check not found, only structural checks were run"
exit $failed
