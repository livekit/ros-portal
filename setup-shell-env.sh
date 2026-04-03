#!/bin/bash

set -euo pipefail

cat <<EOF >/etc/profile.d/ros-livekit-bridge.sh
export ROS_DISTRO=${ROS_DISTRO}
export WS=${WS}

_source_ros_env() {
    if [ -n "\${ZSH_VERSION:-}" ]; then
        source /opt/ros/${ROS_DISTRO}/setup.zsh
    else
        source /opt/ros/${ROS_DISTRO}/setup.bash
    fi
}

_source_ws_overlay() {
    if [ -n "\${ZSH_VERSION:-}" ]; then
        if [ -f ${WS}/install/setup.zsh ]; then
            source ${WS}/install/setup.zsh
        fi
    else
        if [ -f ${WS}/install/setup.bash ]; then
            source ${WS}/install/setup.bash
        fi
    fi
}

_source_ros_env

alias bros='cd ${WS} && colcon build'
alias dros='cd ${WS} && rosdep update && rosdep install --from-paths src --ignore-src -r -y'
alias sros='_source_ros_env && _source_ws_overlay'

cbpu()
{
    cd ${WS} && sros && colcon build --packages-up-to "\$@"
}

cbps()
{
    cd ${WS} && sros && colcon build --packages-select "\$@" && sros
}

waver() {
    if [[ "\$1" == "description" && -z "\$2" ]]; then
        bros && sros && ros2 launch waver_description description.launch.xml
    elif [[ "\$1" == "gazebo" && -z "\$2" ]]; then
        bros && sros && ros2 launch waver_gazebo gazebo.launch.xml
    elif [[ "\$1" == "nav" && -z "\$2" ]]; then
        bros && sros && ros2 launch nav2_bringup navigation_launch.py
    elif [[ "\$1" == "nav" && "\$2" == "slam_toolbox" && -z "\$3" ]]; then
        bros && sros && echo "Use: async_slam_toolbox_node | online_async_launch"
    elif [[ "\$1" == "nav" && "\$2" == "slam_toolbox" && "\$3" == "async_slam_toolbox_node" ]]; then
        bros && sros && ros2 launch waver_nav async_slam_toolbox.launch.xml
    elif [[ "\$1" == "nav" && "\$2" == "slam_toolbox" && "\$3" == "online_async_launch" ]]; then
        bros && sros && ros2 launch waver_nav online_async.launch.xml
    elif [[ "\$1" == "rviz" && -z "\$2" ]]; then
        bros && sros && ros2 launch waver_viz rviz.launch.xml
    elif [[ "\$1" == "teleop" && -z "\$2" ]]; then
        sros && ros2 run teleop_twist_keyboard teleop_twist_keyboard
    else
        echo "Use: waver [description|gazebo|nav|rviz|teleop]"
    fi
}

if [ -n "\${BASH_VERSION:-}" ]; then
    _waver_completion() {
        local cur=\${COMP_WORDS[COMP_CWORD]}
        local prev=\${COMP_WORDS[COMP_CWORD-1]}

        if [[ \$COMP_CWORD -eq 1 ]]; then
            COMPREPLY=( \$(compgen -W "description gazebo nav rviz teleop" -- "\$cur") )
        elif [[ \$COMP_CWORD -eq 2 && "\$prev" == "nav" ]]; then
            COMPREPLY=( \$(compgen -W "slam_toolbox" -- "\$cur") )
        elif [[ \$COMP_CWORD -eq 3 && "\$prev" == "slam_toolbox" ]]; then
            COMPREPLY=( \$(compgen -W "async_slam_toolbox_node online_async_launch" -- "\$cur") )
        fi
    }
    complete -F _waver_completion waver
fi

if [ -n "\${ZSH_VERSION:-}" ]; then
    PROMPT='(ros-livekit) %n@%m:%~ %# '
else
    export PS1='(ros-livekit) \u@\h:\w \$ '
fi
EOF

cat <<'EOF' >/root/.bashrc
source /etc/profile.d/ros-livekit-bridge.sh
EOF

cat <<'EOF' >/root/.bash_profile
source /root/.bashrc
EOF

cat <<'EOF' >/root/.zshrc
source /etc/profile.d/ros-livekit-bridge.sh
autoload -Uz compinit
compinit
EOF

cat <<'EOF' >/root/.zprofile
source /root/.zshrc
EOF
