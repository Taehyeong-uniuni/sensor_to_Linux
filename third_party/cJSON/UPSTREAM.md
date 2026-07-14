# cJSON — third-party dependency

- Upstream repository: https://github.com/DaveGamble/cJSON
- Version / tag: v1.7.19
- Commit SHA: c859b25da02955fef659d658b8f324b5cde87be3 (2025-09-09)
- Included files: cJSON.c, cJSON.h, LICENSE
- License: MIT
- Integration: source vendor + static compile only (no dynamic link, no
  system package). `cJSON_Utils` is not included or compiled.
- Vendored source is unmodified (no patches applied).
- Decision reference: 08_BLOCKERS.md DEC-20260714-05.

## SHA-256 of vendored files

```text
298581a04a36c0165da4b0aade235c23088cb2faa58651d720ea2f3706ed0b0d  cJSON.c
25b0145150d500498e4d209cec69c18c42cf818bffcc54690be3b895a2a16dee  cJSON.h
a36dda207c36db5818729c54e7ad4e8b0c6fba847491ba64f372c1a2037b6d5c  LICENSE
```

These three lines are byte-for-byte reproduced in
`third_party/DEPENDENCY_MANIFEST.sha256` (relative-path, LF-only,
deterministic manifest used for cross-repo comparison).
