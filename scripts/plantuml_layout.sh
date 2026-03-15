#!/usr/bin/env bash
set -euo pipefail

TARGET_PATH="${1:-docs/diagrams/primitives_class.puml}"

if [[ ! -f "$TARGET_PATH" ]]; then
  echo "plantuml_layout: file not found: $TARGET_PATH" >&2
  exit 1
fi

tmp_path="${TARGET_PATH}.tmp"

awk '
  BEGIN { inserted = 0 }
  /^(left to right direction|top to bottom direction)$/ { next }
  {
    print
    if (!inserted && $0 ~ /^@startuml/) {
      print "top to bottom direction"
      inserted = 1
    }
  }
  END {
    if (!inserted) {
      print "top to bottom direction"
    }
  }
' "$TARGET_PATH" > "$tmp_path"

mv "$tmp_path" "$TARGET_PATH"
