#!/bin/sh
# Opens an issue when upstream lichess-bot has a newer version than the
# one in deploy/lichess-bot.txt, listing the commits since then (without
# the automatic version bumps). One issue per version: an existing one,
# open or closed, is not repeated. Needs gh with access to the repository
# (GH_TOKEN). DRY_RUN=1 only prints the issue.
set -eu
upstream=lichess-bot-devs/lichess-bot
file=deploy/lichess-bot.txt
deployed=$(sed -n 's/^version: *//p' "$file")
commit=$(sed -n 's/^commit: *//p' "$file")
latest=$(gh api "repos/$upstream/contents/lib/versioning.yml" \
         -H "Accept: application/vnd.github.raw" |
         sed -n 's/^lichess_bot_version: *//p')
echo "deployed $deployed, upstream $latest"
if [ -z "$latest" ] || [ "$latest" = "$deployed" ]; then
    exit 0
fi
title="lichess-bot $latest available (the bot runs $deployed)"
# The repository's owner, mentioned so that GitHub notifies them.
owner=${GITHUB_REPOSITORY_OWNER:-$(gh repo view ${GH_REPO:+"$GH_REPO"} --json owner --jq .owner.login)}
if [ -z "${DRY_RUN:-}" ] &&
   [ -n "$(gh issue list --state all --search "lichess-bot $latest available in:title" \
           --json number --jq '.[].number')" ]; then
    echo "issue exists already"
    exit 0
fi
commits=$(gh api "repos/$upstream/compare/$commit...master" \
          --jq '.commits[] | select(.commit.message | startswith("Auto update version") | not)
                | "- [`\(.sha[0:7])`](\(.html_url)) \(.commit.message | split("\n")[0])"')
body=$(cat <<END
Upstream [lichess-bot](https://github.com/$upstream) is at **$latest**; the bot on the VPS runs **$deployed** (\`$commit\`).

Commits since then (automatic version bumps left out):

${commits:-(only version bumps)}

To upgrade: \`git pull --ff-only\` in lichess-bot on the VPS, check \`requirements.txt\` and the config for changes, restart the bot when no game is in progress, and update \`$file\`.

cc @$owner
END
)
if [ -n "${DRY_RUN:-}" ]; then
    printf '%s\n\n%s\n' "$title" "$body"
else
    gh issue create --title "$title" --body "$body"
fi
