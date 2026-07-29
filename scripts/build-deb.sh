#!/usr/bin/env bash

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

# This script is intended for the repository's rootful devcontainer and CI.
# It removes and recreates /opt/livekit/ros/$ROS_DISTRO as a build staging
# prefix, so running it directly on a developer host is not supported.

set -euo pipefail

readonly repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly ros_distro="${ROS_DISTRO:-}"
readonly output_dir="${OUTPUT_DIR:-${repo_root}/artifacts/debian}"

case "${ros_distro}" in
  humble | jazzy | kilted | lyrical) ;;
  *)
    echo "ROS_DISTRO must be one of: humble, jazzy, kilted, lyrical" >&2
    exit 2
    ;;
esac

if [[ ! -f "/opt/ros/${ros_distro}/setup.bash" ]]; then
  echo "ROS ${ros_distro} is not installed under /opt/ros/${ros_distro}" >&2
  exit 2
fi

if [[ ! -f "${repo_root}/src/externals/ros2_medkit/src/ros2_medkit_serialization/package.xml" ]]; then
  echo "Pinned externals are missing; import external.repos before packaging" >&2
  exit 2
fi

"${repo_root}/scripts/apply-external-patches.sh"

mapfile -t package_versions < <(
  python3 - \
    "${repo_root}/src/ros2_livekit_bridge/package.xml" \
    "${repo_root}/src/ros2_livekit_bridge_config/package.xml" \
    "${repo_root}/src/ros2_livekit_bridge_msgs/package.xml" <<'PY'
import sys
import xml.etree.ElementTree as ET

for package_xml in sys.argv[1:]:
    print(ET.parse(package_xml).getroot().findtext("version"))
PY
)

if [[ "${#package_versions[@]}" -ne 3 ]] ||
  [[ "${package_versions[0]}" != "${package_versions[1]}" ]] ||
  [[ "${package_versions[0]}" != "${package_versions[2]}" ]]; then
  echo "First-party package.xml versions must be identical" >&2
  exit 2
fi

readonly upstream_version="${package_versions[0]}"
ubuntu_codename="$(
  sed -nE 's/^VERSION_CODENAME="?([^"]+)"?$/\1/p' /etc/os-release
)"
if [[ -z "${ubuntu_codename}" ]]; then
  echo "Unable to determine the Ubuntu codename from /etc/os-release" >&2
  exit 2
fi
readonly ubuntu_codename
readonly debian_version="${upstream_version}-1${ubuntu_codename}"
readonly debian_arch="$(dpkg --print-architecture)"
readonly package_name="ros-${ros_distro}-livekit-bridge"
readonly command_name="livekit-ros2-bridge-${ros_distro}"
readonly install_prefix="/opt/livekit/ros/${ros_distro}"
readonly work_root="${repo_root}/build-deb/${ros_distro}-${debian_arch}"
readonly build_base="${work_root}/build"
readonly log_base="${work_root}/log"
readonly package_root="${work_root}/package-root"
readonly shlibs_root="${work_root}/shlibs"
readonly deb_path="${output_dir}/${package_name}_${debian_version}_${debian_arch}.deb"

set +u
source "/opt/ros/${ros_distro}/setup.bash"
set -u

rm -rf "${work_root}" "${install_prefix}"
mkdir -p "${build_base}" "${log_base}" "${output_dir}"

declare -a cmake_args=(-DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Release)
if [[ "${BUILD_LIVEKIT_SDK_FROM_SOURCE:-false}" == "true" ]]; then
  cmake_args+=(-DLIVEKIT_BUILD_SDK_FROM_SOURCE=ON)
fi

colcon --log-base "${log_base}" build \
  --base-paths \
    "${repo_root}/src/ros2_livekit_bridge" \
    "${repo_root}/src/ros2_livekit_bridge_config" \
    "${repo_root}/src/ros2_livekit_bridge_msgs" \
    "${repo_root}/src/externals/ros2_medkit/src/ros2_medkit_cmake" \
    "${repo_root}/src/externals/ros2_medkit/src/ros2_medkit_serialization" \
  --build-base "${build_base}" \
  --install-base "${install_prefix}" \
  --merge-install \
  --packages-up-to ros2_livekit_bridge \
  --cmake-args "${cmake_args[@]}"

mkdir -p "${package_root}${install_prefix}" "${package_root}/usr/bin"
cp -a "${install_prefix}/." "${package_root}${install_prefix}/"

cat >"${package_root}/usr/bin/${command_name}" <<EOF
#!/usr/bin/env bash
set -eo pipefail
source "${install_prefix}/setup.bash"
set -u
exec ros2 launch ros2_livekit_bridge livekit_bridge.launch.xml "\$@"
EOF
chmod 0755 "${package_root}/usr/bin/${command_name}"

mapfile -t rosdep_keys < <(
  python3 - \
    "${repo_root}/src/ros2_livekit_bridge/package.xml" \
    "${repo_root}/src/ros2_livekit_bridge_config/package.xml" \
    "${repo_root}/src/ros2_livekit_bridge_msgs/package.xml" \
    "${repo_root}/src/externals/ros2_medkit/src/ros2_medkit_cmake/package.xml" \
    "${repo_root}/src/externals/ros2_medkit/src/ros2_medkit_serialization/package.xml" <<'PY'
import sys
import xml.etree.ElementTree as ET

bundled = {
    "ros2_livekit_bridge",
    "ros2_livekit_bridge_config",
    "ros2_livekit_bridge_msgs",
    "ros2_medkit_cmake",
    "ros2_medkit_serialization",
}
keys = set()
for package_xml in sys.argv[1:]:
    root = ET.parse(package_xml).getroot()
    for tag in ("depend", "exec_depend"):
        for dependency in root.findall(tag):
            if dependency.text and dependency.text.strip() not in bundled:
                keys.add(dependency.text.strip())
print("\n".join(sorted(keys)))
PY
)

declare -a runtime_dependencies=("ros-${ros_distro}-ros-base")
for rosdep_key in "${rosdep_keys[@]}"; do
  resolution_output=""
  if ! resolution_output="$(rosdep resolve --rosdistro "${ros_distro}" "${rosdep_key}")"; then
    echo "Failed to resolve runtime dependency '${rosdep_key}' for ROS ${ros_distro}" >&2
    exit 1
  fi

  installer=""
  declare -a resolved_packages=()
  while IFS= read -r resolution; do
    if [[ "${resolution}" == \#* ]]; then
      installer="${resolution#\#}"
    elif [[ "${installer}" == "apt" && -n "${resolution}" ]]; then
      read -r -a packages_on_line <<<"${resolution}"
      resolved_packages+=("${packages_on_line[@]}")
    fi
  done <<<"${resolution_output}"

  if [[ "${#resolved_packages[@]}" -eq 0 ]]; then
    echo "Runtime dependency '${rosdep_key}' did not resolve to an APT package for ROS ${ros_distro}" >&2
    exit 1
  fi
  runtime_dependencies+=("${resolved_packages[@]}")
done

mkdir -p "${shlibs_root}/debian"
cat >"${shlibs_root}/debian/control" <<EOF
Source: ${package_name}
Section: misc
Priority: optional
Maintainer: LiveKit <sderosa@livekit.io>

Package: ${package_name}
Architecture: any
Description: LiveKit bridge for ROS 2 ${ros_distro}
EOF

declare -a elf_arguments=()
while IFS= read -r -d '' candidate; do
  if [[ "$(file -b "${candidate}")" == *ELF* ]]; then
    elf_arguments+=("-e${candidate}")
  fi
done < <(find "${package_root}${install_prefix}" -type f -print0)

if [[ "${#elf_arguments[@]}" -eq 0 ]]; then
  echo "No ELF runtime artifacts found in ${install_prefix}" >&2
  exit 1
fi

shlibs_output="$(
  cd "${shlibs_root}"
  dpkg-shlibdeps \
    --ignore-missing-info \
    -O \
    "-l${package_root}${install_prefix}/lib" \
    "-l/opt/ros/${ros_distro}/lib" \
    "${elf_arguments[@]}"
)"
shlibs_dependencies="${shlibs_output#shlibs:Depends=}"
if [[ "${shlibs_dependencies}" != "${shlibs_output}" ]]; then
  IFS=',' read -r -a resolved_shlibs <<<"${shlibs_dependencies}"
  for dependency in "${resolved_shlibs[@]}"; do
    runtime_dependencies+=("${dependency#"${dependency%%[![:space:]]*}"}")
  done
fi

mapfile -t runtime_dependencies < <(printf '%s\n' "${runtime_dependencies[@]}" | LC_ALL=C sort -u)
printf -v depends_field '%s, ' "${runtime_dependencies[@]}"
depends_field="${depends_field%, }"

mkdir -p "${package_root}/DEBIAN"
installed_size="$(du -sk "${package_root}" | cut -f1)"
cat >"${package_root}/DEBIAN/control" <<EOF
Package: ${package_name}
Version: ${debian_version}
Section: misc
Priority: optional
Architecture: ${debian_arch}
Maintainer: LiveKit <sderosa@livekit.io>
Homepage: https://github.com/livekit/ros-livekit-bridge
Installed-Size: ${installed_size}
Depends: ${depends_field}
Description: LiveKit bridge for ROS 2 ${ros_distro}
 Connects a ROS 2 graph to LiveKit for remote topics, services, video,
 diagnostics, and ROS 2 CLI operations.
EOF

dpkg-deb --root-owner-group --build "${package_root}" "${deb_path}"
(
  cd "${output_dir}"
  sha256sum "$(basename "${deb_path}")" >"$(basename "${deb_path}").sha256"
)

echo "Built ${deb_path}"
