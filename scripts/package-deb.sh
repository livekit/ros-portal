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

# Bundle an existing isolated colcon install tree into a Debian package. This
# script does not compile, configure, install, or download anything.

set -euo pipefail

readonly repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly ros_distro="${ROS_DISTRO:-}"
readonly output_dir="${OUTPUT_DIR:-${repo_root}/artifacts/debian}"
readonly source_install_input="${SOURCE_INSTALL_BASE:-}"
readonly packaged_packages=(
  ros2_livekit_bridge
  ros2_livekit_bridge_config
  ros2_livekit_bridge_msgs
  ros2_medkit_cmake
  ros2_medkit_serialization
)

if [[ -z "${source_install_input}" ]]; then
  echo "SOURCE_INSTALL_BASE is required." >&2
  echo "Build the workspace first, then pass its install prefix, for example:" >&2
  echo "  SOURCE_INSTALL_BASE=${repo_root}/install ./scripts/package-deb.sh" >&2
  exit 2
fi

if [[ ! -d "${source_install_input}" ]]; then
  echo "SOURCE_INSTALL_BASE does not exist: ${source_install_input}" >&2
  exit 2
fi

readonly source_install_base="$(cd "${source_install_input}" && pwd)"
readonly layout_file="${source_install_base}/.colcon_install_layout"
if [[ ! -f "${layout_file}" ]] ||
  [[ "$(tr -d '[:space:]' <"${layout_file}")" != "isolated" ]]; then
  echo "SOURCE_INSTALL_BASE must be a completed isolated colcon install: ${source_install_base}" >&2
  exit 2
fi

declare -a installed_package_xmls=()
for package in "${packaged_packages[@]}"; do
  package_marker="${source_install_base}/${package}/share/colcon-core/packages/${package}"
  package_xml="${source_install_base}/${package}/share/${package}/package.xml"
  if [[ ! -f "${package_marker}" || ! -f "${package_xml}" ]]; then
    echo "Required package '${package}' is missing from ${source_install_base}" >&2
    exit 2
  fi
  installed_package_xmls+=("${package_xml}")
done

readonly source_bridge_prefix="${source_install_base}/ros2_livekit_bridge"
readonly source_bridge_node="${source_bridge_prefix}/lib/ros2_livekit_bridge/ros2_livekit_bridge_node"
if [[ ! -x "${source_bridge_node}" ]]; then
  echo "Built bridge node is missing: ${source_bridge_node}" >&2
  exit 2
fi

for required_setup_file in setup.bash local_setup.bash _local_setup_util_sh.py; do
  if [[ ! -f "${source_install_base}/${required_setup_file}" ]]; then
    echo "Incomplete colcon install; missing ${source_install_base}/${required_setup_file}" >&2
    exit 2
  fi
done

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

mapfile -t package_versions < <(
  python3 - \
    "${source_install_base}/ros2_livekit_bridge/share/ros2_livekit_bridge/package.xml" \
    "${source_install_base}/ros2_livekit_bridge_config/share/ros2_livekit_bridge_config/package.xml" \
    "${source_install_base}/ros2_livekit_bridge_msgs/share/ros2_livekit_bridge_msgs/package.xml" <<'PY'
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
readonly work_root="${repo_root}/package-deb/${ros_distro}-${debian_arch}"
readonly package_root="${work_root}/package-root"
readonly staging_prefix="${package_root}${install_prefix}"
readonly shlibs_root="${work_root}/shlibs"
readonly deb_path="${output_dir}/${package_name}_${debian_version}_${debian_arch}.deb"

set +u
source "/opt/ros/${ros_distro}/setup.bash"
set -u

rm -rf "${work_root}"
mkdir -p "${staging_prefix}" "${output_dir}"
touch "${work_root}/COLCON_IGNORE"

for package in "${packaged_packages[@]}"; do
  cp -a "${source_install_base}/${package}" "${staging_prefix}/"
done

for setup_file in \
  .colcon_install_layout \
  _local_setup_util_sh.py \
  _local_setup_util_ps1.py \
  local_setup.bash \
  local_setup.sh \
  local_setup.zsh \
  local_setup.ps1 \
  setup.bash \
  setup.sh \
  setup.zsh \
  setup.ps1
do
  if [[ -f "${source_install_base}/${setup_file}" ]]; then
    cp -a "${source_install_base}/${setup_file}" "${staging_prefix}/"
  fi
done

# POSIX shell setup files cannot discover their own location when sourced, so
# replace the build-time install prefix with the package's final prefix.
python3 - "${staging_prefix}" "${source_install_base}" "${install_prefix}" <<'PY'
from pathlib import Path
import sys

staging_prefix = Path(sys.argv[1])
source_prefix = sys.argv[2]
install_prefix = sys.argv[3]

for setup_file in staging_prefix.rglob("*.sh"):
    contents = setup_file.read_text()
    relocated = contents.replace(source_prefix, install_prefix)
    if relocated != contents:
        setup_file.write_text(relocated)
PY

mkdir -p "${package_root}/usr/bin"
cat >"${package_root}/usr/bin/${command_name}" <<EOF
#!/usr/bin/env bash
set -eo pipefail
source "${install_prefix}/setup.bash"
set -u
exec ros2 launch ros2_livekit_bridge livekit_bridge.launch.py "\$@"
EOF
chmod 0755 "${package_root}/usr/bin/${command_name}"

mapfile -t rosdep_keys < <(
  python3 - "${installed_package_xmls[@]}" <<'PY'
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
done < <(find "${staging_prefix}" -type f -print0)

if [[ "${#elf_arguments[@]}" -eq 0 ]]; then
  echo "No ELF runtime artifacts found in staged prefix ${staging_prefix}" >&2
  exit 1
fi

shlibs_output="$(
  cd "${shlibs_root}"
  declare -a library_paths=("-l/opt/ros/${ros_distro}/lib")
  for package in "${packaged_packages[@]}"; do
    package_lib="${staging_prefix}/${package}/lib"
    if [[ -d "${package_lib}" ]]; then
      library_paths+=("-l${package_lib}")
    fi
  done
  dpkg-shlibdeps \
    --ignore-missing-info \
    -O \
    "${library_paths[@]}" \
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

echo "Packaged ${deb_path} from ${source_install_base}"
