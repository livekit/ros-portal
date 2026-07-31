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

_source_ros_env

alias bros='cd "\${WS}" && colcon build'
alias dros='cd "\${WS}" && rosdep update && rosdep install --from-paths src --ignore-src -r -y'

alias sros='_source_ros_env && _source_ws_overlay'

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
    cd "\${WS}" && sros && colcon build --packages-up-to "\$@" && colcon test --packages-up-to "\$@" && colcon test-result --verbose
}

clean_ws()
{
    cd "\${WS}" && rm -rf build/ log/ install/
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
autoload -Uz compinit
compinit
EOF

cat <<'EOF' >/root/.zprofile
source /root/.zshrc
EOF
