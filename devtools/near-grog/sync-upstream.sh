#!/bin/sh
# Bring Near-Grog up to date with ScummVM's master branch.
# Applies the upstream changes between the commit in UPSTREAM and upstream's
# latest master on top of Near-Grog, keeping Near-Grog's own files.
set -e
cd "$(git rev-parse --show-toplevel)"
UP=https://github.com/scummvm/scummvm
OLD=$(cat devtools/near-grog/UPSTREAM)

git fetch --depth 1 "$UP" "$OLD"
git fetch --depth 1 "$UP" master
NEW=$(git rev-parse FETCH_HEAD)
if [ "$OLD" = "$NEW" ]; then
	echo "Already at upstream $NEW"
	exit 0
fi

# Near-Grog's own copies of these files win (including its logo and app
# icons; if upstream changes the theme zips, rebuild them with
# gui/themes/scummtheme.py makeall so they keep the Near-Grog logo);
# README.md is upstream's README, which Near-Grog keeps at doc/README-ScummVM.md.
git diff --binary "$OLD" "$NEW" -- . ':(exclude)AGENTS.md' ':(exclude)CLAUDE.md' ':(exclude)README.md' ':(exclude).github/workflows' \
	':(exclude)dists/ios7/Images.xcassets' ':(exclude)gui/themes/common-svg/logo.svg' ':(exclude)gui/themes/*/logo.bmp' \
	':(exclude)gui/themes/*/logo_small.bmp' > /tmp/near-grog-upstream.diff
git diff --binary "$OLD" "$NEW" -- README.md | sed 's#a/README.md#a/doc/README-ScummVM.md#; s#b/README.md#b/doc/README-ScummVM.md#' >> /tmp/near-grog-upstream.diff

git apply --3way --index /tmp/near-grog-upstream.diff
echo "$NEW" > devtools/near-grog/UPSTREAM
git add devtools/near-grog/UPSTREAM
echo "Applied upstream $OLD..$NEW. Check for conflicts, build, then commit:"
echo "  git commit -m \"Sync with ScummVM upstream ${NEW%"${NEW#???????}"}\""
