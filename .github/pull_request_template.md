### Before you submit your PR

Make sure the following is true before submitting your PR:

- [ ] I have read the [contributing guidelines](https://github.com/livekit/ros-portal/blob/main/docs/contributing.md) and validated that this PR will be accepted.
- [ ] I have read and followed the principles regarding breaking changes, testing, and code quality.

### PR description

Describe the changes in this PR. Explain what the PR is meant to solve and how to reproduce the issue in the first place.

### Breaking changes

If this PR introduces breaking changes, list them here and document the rationale for introducing such a change.

### Minimum Supported client-sdk-cpp Version

If the PR modifies the client-sdk-cpp version, document it here.

### Testing

Ideally, unit test the code you add, but ensure you're not repeating existing test cases. Use as many already written scaffolding, utilities as possible; write your own, when needed.
If external services, APIs, tokens are required (e.g., running an LK server instance), provide the necessary information. Make sure your tests perform useful, context-aware assertions and do not simply emulate "happy paths".
