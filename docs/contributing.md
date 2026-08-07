# Contributing

Thanks for your interest in contributing!

## Before writing code

If you'd like to contribute code, it's recommended to first discuss your idea on the
[LiveKit Developer Community](https://community.livekit.io/c/robotics). This helps keep changes aligned with the LiveKit roadmap
and avoids duplicated or unnecessary work.

## Before you open a pull request

Please make sure your change passes all of the following:

```sh
colcon build --packages-up-to ros_portal                                     # building
./scripts/clang-format.sh --fix                                              # formatting
./scripts/clang-tidy.sh                                                      # correctness (note: CI blocks errors only)
colcon test --packages-up-to ros_portal                                      # tests (be sure to set credentials)
ros2 launch ros_portal ros_portal.launch.py config_path=/path/to/config.yaml # app still launches and behaves correctly
```

- Add tests for ROS Portal logic where practical
- Keep commits focused; a single logical change per PR is easiest to review
- For a better dev process, install the pre-commit hooks described in [cpp-tools](https://github.com/livekit/cpp-tools#quick-start)

## Pull requests

- Open PRs against the `main` branch
- Describe *what* the change does and *why*
- When applicable, add a screenshot or short screen recording to help illustrate the change
- Link any related issues (e.g. `Closes #123`)

## Reporting bugs

The issue tracker is for bugs or suspected bugs only. Bug reports must use the "Bug Report" template; issues that don't will be closed automatically.

## License

By contributing, you agree that your contributions will be licensed under the same terms as this project (see the `LICENSE` file)