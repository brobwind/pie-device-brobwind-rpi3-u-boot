#!/bin/bash
#
# Copyright (C) 2016 The Android Open-Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

set -e -u

base_dir=$(realpath $(dirname $0))
cd "${base_dir}"

UPSTREAM_GIT="http://git.denx.de/u-boot.git"
UPSTREAM_FTP="ftp://ftp.denx.de/pub/u-boot/"
UPSTREAM="denx_de"
# The latest tag format is defined as follows:
UPSTREAM_TAG_REGEX="v[0-9]{4}\\.[0-9]{2}(\\.[0-9]{2})?"
README_FILE="README.version"

REPLICATION_BRANCH="upstream-master"

TEMP_FILES=()
cleanup() {
  trap - INT TERM ERR EXIT
  if [[ ${#TEMP_FILES[@]} -ne 0 ]]; then
    rm -f "${TEMP_FILES[@]}"
  fi
}
trap cleanup INT TERM ERR EXIT


# Update the contents of the README.version with the new version string passed
# as the first parameter.
update_readme() {
  local version="$1"
  local tmp_readme=$(mktemp update_readme.XXXXXX)
  TEMP_FILES+=("${tmp_readme}")

  cat >"${tmp_readme}" <<EOF
URL: ${UPSTREAM_FTP}u-boot-${version}.tar.bz2
Version: ${version}
EOF
  grep -v -E '^(URL|Version):' "${README_FILE}" >>"${tmp_readme}" 2>/dev/null \
    || true
  cp "${tmp_readme}" "${README_FILE}"
}


# Setup and fetch the upstream remote. While we have mirroring setup of the
# remote, we need to fetch all the tags from upstream to identify the latest
# release.
setup_git() {
  # Setup and fetch the remote.
  if ! git remote show | grep "^${UPSTREAM}\$" >/dev/null; then
    echo "Adding remote ${UPSTREAM} to ${UPSTREAM_GIT}"
    git remote add -t master "${UPSTREAM}" "${UPSTREAM_GIT}"
  fi

  TRACKING_BRANCH=$(git rev-parse --abbrev-ref --symbolic-full-name @{u})
  OUR_REMOTE="${TRACKING_BRANCH%/*}"

  echo "Fetching latest upstream code..."
  git fetch --quiet "${OUR_REMOTE}" "${REPLICATION_BRANCH}"
  git fetch --quiet --tags "${UPSTREAM}" master
}


main() {
  setup_git

  local tags=($(git tag | grep -E "^${UPSTREAM_TAG_REGEX}\$" |
                LC_ALL=C sort -r))

  if [[ ${#tags[@]} -eq 0 ]]; then
    echo "No versions detected, update tag regex."
    exit 1
  fi

  local latest_tag="${tags[0]}"
  local latest_version="${latest_tag#v}"
  local current_version=$(
    grep '^Version: ' "${README_FILE}" 2>/dev/null |
    cut -f 2 -d ' ' || true)

  echo "Current version: ${current_version}"
  echo "Latest version: ${latest_version}"
  echo

  if [[ "${latest_version}" == "${current_version}" ]]; then
    echo "Nothing to do. Exiting." >&2
    exit 0
  fi

  # Sanity check that we have all the CLs replicated in upstream-master.
  if [[ $(git log "${latest_tag}" --not "${OUR_REMOTE}/${REPLICATION_BRANCH}" \
    --oneline -- | tee /dev/stderr | wc -c) -ne 0 ]]; then
    echo
    echo "ERROR: The previous CLs are not in the replication branch" \
      "${OUR_REMOTE}/${REPLICATION_BRANCH}, please wait until it catches up" \
      "or ping the build team." >&2
    exit 1
  fi

  echo "Merging changes..."
  git merge --no-stat --no-ff "${latest_tag}" \
    -m "Update U-boot to ${latest_tag}

Bug: [COMPLETE]
Test: [COMPLETE]

"
  update_readme "${latest_version}"
  git add "README.version"
  git commit --amend --no-edit -- "README.version"

  git --no-pager show
  cat <<EOF

  Run:
    git commit --amend

  and edit the message to add bug number and test it.

EOF
}

main "$@"
