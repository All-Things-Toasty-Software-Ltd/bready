## Summary

<!-- One or two sentences describing what this PR changes and why. -->

## Related issue

Closes #<!-- issue number, if applicable -->

## Type of change

- [ ] Bug fix (non-breaking change that fixes an issue)
- [ ] New feature (non-breaking change that adds functionality)
- [ ] Breaking change (fix or feature that would cause existing behaviour
  to change)
- [ ] Documentation / comment update
- [ ] Refactor / code style improvement

## Pull request checklist

- [ ] Code compiles without errors or new warnings
  (`cmake --build build 2>&1 | grep -i "error:"`).
- [ ] All new files begin with the copyright header.
- [ ] Include guards follow the `BREADY_INCLUDE_<FILENAME>_H_` pattern.
- [ ] All new code is inside the appropriate namespace
  (`namespace bready`).
- [ ] New or changed public APIs have Google-style doc comments in the
  header file.
- [ ] `README.md` updated if new commands or environment
  variables were added.
- [ ] Commit message(s) use the imperative mood and are ≤ 72 characters
  on the subject line.

## Testing

<!-- Describe how you verified the change works.  Include the commands
     you ran and any relevant output (redact tokens / API keys). -->

## Additional notes

<!-- Anything else reviewers should know. -->