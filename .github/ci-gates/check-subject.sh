#!/usr/bin/env bash
#
# Validate one commit subject or pull request title against the repository's
# required Conventional Commits format, `type(scope): concise message`.
#
# Usage:   check-subject.sh "<subject>"
# Exit 0:  the subject is valid.
# Exit 1:  the subject is rejected, with the reason and a fix printed to stdout.
#
# This script is deliberately free of third-party dependencies so it runs
# unchanged on GitHub's runners, on a self-hosted runner, and on a laptop.

set -euo pipefail

# Every type the gates accept. `git-delivery` documents what each one means and
# is the single owner of that vocabulary; this list implements it.
readonly ALLOWED_TYPES='feat|fix|refactor|perf|test|docs|build|ci|chore|revert|style|config'

# Hard ceiling. `git-delivery` asks for under 72 characters as a habit; this is
# the point past which a subject stops being a subject.
readonly MAX_LENGTH=100

# Print a rejection with the offending subject and how to fix it, then exit 1.
reject() {
  local reason=$1 fix=$2 subject=$3
  printf '::error::%s\n' "$reason"
  printf '\n  subject: %s\n' "$subject"
  printf '  problem: %s\n' "$reason"
  printf '  fix:     %s\n\n' "$fix"
  printf 'Required format: type(scope): concise message\n'
  printf 'Accepted types:  %s\n' "${ALLOWED_TYPES//|/, }"
  exit 1
}

# Check one subject against every rule and reject on the first one it breaks.
check_subject() {
  local subject=$1

  if [[ -z $subject ]]; then
    reject "the subject is empty" "write it as type(scope): concise message" "$subject"
  fi

  if (( ${#subject} > MAX_LENGTH )); then
    reject "the subject is ${#subject} characters, the limit is ${MAX_LENGTH}" \
      "shorten it, and move the detail into the body" "$subject"
  fi

  if [[ $subject == *. ]]; then
    reject "the subject ends with a period" \
      "drop the trailing period" "$subject"
  fi

  if ! [[ $subject =~ ^(${ALLOWED_TYPES})(\([a-z0-9][a-z0-9._/-]*\))?!?:\ .+$ ]]; then
    reject "the subject is not type(scope): concise message" \
      "use an accepted type, a lowercase scope in parentheses or no scope at all, then a colon and one space" \
      "$subject"
  fi

  # Sentence case reads like prose rather than an instruction. An all-capital
  # acronym such as BLE or KWS is a real word here, so only reject a capital
  # followed by a lowercase letter.
  local message=${subject#*: }
  if [[ $message =~ ^[A-Z][a-z] ]]; then
    reject "the message starts with a capitalized word" \
      "start it lowercase and in the imperative, for example 'add' rather than 'Added'" \
      "$subject"
  fi
}

check_subject "${1-}"
printf 'ok: %s\n' "$1"
