#!/bin/bash

# Copyright 2026 LiveKit
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

set -euo pipefail

cat <<EOF >/etc/profile.d/ros-portal.sh
export ROS_DISTRO=${ROS_DISTRO}
export WS="${WS}"

_source_ros_env() {
    if [ -n "\${ZSH_VERSION:-}" ]; then
        source /opt/ros/${ROS_DISTRO}/setup.zsh
    else
        source /opt/ros/${ROS_DISTRO}/setup.bash
    fi
}

_source_ws_overlay() {
    if [ -n "\${ZSH_VERSION:-}" ]; then
        if [ -f "\${WS}/install/setup.zsh" ]; then
            source "\${WS}/install/setup.zsh"
        fi
    else
        if [ -f "\${WS}/install/setup.bash" ]; then
            source "\${WS}/install/setup.bash"
        fi
    fi
}

_enable_ros_autocomplete() {
    if command -v register-python-argcomplete3 >/dev/null 2>&1; then
        eval "\$(register-python-argcomplete3 ros2)"
    fi
}

_source_ros_env

alias bros='cd "\${WS}" && colcon build'
alias dros='cd "\${WS}" && rosdep update && rosdep install --from-paths src --ignore-src -r -y'

alias sros='_source_ros_env && _source_ws_overlay && _enable_ros_autocomplete'

# Helper for running the project clang-format wrapper within the devcontainer
clang_format()
{
    cd "\${WS}" && ./scripts/clang-format.sh --fix "\$@"
}

cbpu()
{
    cd "\${WS}" && sros
    if [ "\$#" -eq 0 ]; then
        colcon build --packages-up-to ros_portal
    else
        colcon build --packages-up-to "\$@"
    fi
}

cbps()
{
    cd "\${WS}" && sros && colcon build --packages-select "\$@" && sros
}

cbtps()
{
    pkgs="\$@"
    cd "\${WS}" && sros && colcon build --packages-select "\$pkgs" && colcon test --packages-select "\$pkgs" && colcon test-result --verbose
}

cbtpu()
{
    cd "\${WS}" && sros
    if [ "\$#" -eq 0 ]; then
        set -- ros_portal
    fi
    colcon build --packages-up-to "\$@" && colcon test --packages-up-to "\$@" && colcon test-result --verbose
}

_prepare_test_env()
{
    cd "\${WS}" && _source_ros_env && _source_ws_overlay
}

_source_test_tokens_if_needed()
{
    if [ -n "\${LIVEKIT_URL:-}" ] && [ -n "\${LIVEKIT_TOKEN_A:-}" ] && [ -n "\${LIVEKIT_TOKEN_B:-}" ]; then
        return 0
    fi
    if [ -f "\${WS}/.token_helpers/set_test_tokens.bash" ]; then
        source "\${WS}/.token_helpers/set_test_tokens.bash"
    fi
}

# Run the developer stress harness. Assumes ros_portal has already been built.
test_stress()
{
    cd "\${WS}" && _source_ros_env && _source_ws_overlay
    if ! ros2 pkg prefix ros_portal >/dev/null 2>&1; then
        echo "test_stress: ros_portal is not built; run cbps ros_portal first" >&2
        return 1
    fi
    _source_test_tokens_if_needed
    if [ ! -x "\${WS}/.venv-stress/bin/python" ]; then
        echo "test_stress: creating .venv-stress..."
        if ! python3 -m venv --system-site-packages "\${WS}/.venv-stress"; then
            if ! command -v apt-get >/dev/null 2>&1 || [ "\$(id -u)" -ne 0 ]; then
                echo "test_stress: Python venv support is required; install python3-venv and retry" >&2
                return 1
            fi
            echo "test_stress: installing Python venv support..."
            apt-get update && apt-get install -y python3-venv
            python3 -m venv --system-site-packages "\${WS}/.venv-stress"
        fi
    fi
    "\${WS}/.venv-stress/bin/python" -m pip install --quiet -r "\${WS}/src/test/stress/requirements.txt"
    "\${WS}/.venv-stress/bin/python" "\${WS}/src/test/stress/run.py" "\$@"
}

# Run unit tests
test_unit()
{
    _prepare_test_env
    test_exit=0
    GTEST_COLOR=1 colcon test --packages-up-to ros_portal test_utilities custom_msgs \
        --packages-skip ros2_medkit_serialization \
        --event-handlers console_direct+ \
        --return-code-on-test-failure \
        --ctest-args -R ros_portal_unit_tests || test_exit=\$?
    colcon test-result --verbose --all
    return "\${test_exit}"
}

# Run integration tests
test_int()
{
    _prepare_test_env
    export CI="\${CI:-true}"
    _source_test_tokens_if_needed
    test_exit=0
    GTEST_COLOR=1 colcon test --packages-select ros_portal \
        --event-handlers console_direct+ \
        --return-code-on-test-failure \
        --ctest-args -R ros_portal_integration_tests || test_exit=\$?
    colcon test-result --verbose --all
    return "\${test_exit}"
}

# Run connection fault tests
test_conn()
{
    _prepare_test_env
    export CI="\${CI:-true}"
    _source_test_tokens_if_needed
    export ROS_PORTAL_RUN_CONNECTION_FAULT_TESTS=1
    test_exit=0
    GTEST_COLOR=1 colcon test --packages-select ros_portal \
        --event-handlers console_direct+ \
        --return-code-on-test-failure \
        --ctest-args -R ros_portal_connection_fault_tests || test_exit=\$?
    colcon test-result --verbose --all
    return "\${test_exit}"
}

clean_ws()
{
    cd "\${WS}" && rm -rf build*/ log*/ install/
}

rtl()
{
    cd "\${WS}" && sros && ros2 topic list "\$@"
}

rte()
{
    cd "\${WS}" && sros && ros2 topic echo "\$@"
}

rsl()
{
    cd "\${WS}" && sros && ros2 service list "\$@"
}

rnl()
{
    cd "\${WS}" && sros && ros2 node list "\$@"
}

# print domain id
did()
{
    echo "\${ROS_DOMAIN_ID:-}"
}

# source/set domain id
sdid()
{
    if [ "\$#" -eq 0 ]; then
        echo "Usage: sdid <domain_id>" >&2
        return 1
    fi
    export ROS_DOMAIN_ID="\$1"
}

if [ -n "\${ZSH_VERSION:-}" ]; then
    PROMPT='(ros-portal) %n@%m:%~ %# '
else
    export PS1='(ros-portal) \u@\h:\w \$ '
fi
EOF

cat <<'EOF' >/root/.bashrc
source /etc/profile.d/ros-portal.sh
EOF

cat <<'EOF' >/root/.bash_profile
source /root/.bashrc
EOF

cat <<'EOF' >/root/.zshrc
source /etc/profile.d/ros-portal.sh
autoload -Uz compinit bashcompinit
compinit
bashcompinit
EOF

cat <<'EOF' >/root/.zprofile
source /root/.zshrc
EOF
