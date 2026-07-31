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

# Bundle an existing isolated colcon install tree into a single Debian package.
# This script does not compile, configure, install, or download anything.

set -euo pipefail

readonly repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly ros_distro="${ROS_DISTRO:-}"
readonly output_dir="${OUTPUT_DIR:-${repo_root}/artifacts/debian}"
readonly packaged_packages=(
  ros_portal
  ros_portal_config
  ros_portal_msgs
  ros2_medkit_cmake
  ros2_medkit_serialization
)

if [[ -z "${SOURCE_INSTALL_BASE:-}" ]]; then
  echo "SOURCE_INSTALL_BASE is required." >&2
  exit 2
fi

if [[ ! -d "${SOURCE_INSTALL_BASE}" ]]; then
  echo "SOURCE_INSTALL_BASE does not exist: ${SOURCE_INSTALL_BASE}" >&2
  exit 2
fi

readonly source_install_base="$(cd "${SOURCE_INSTALL_BASE}" && pwd)"
if [[ "$(tr -d '[:space:]' <"${source_install_base}/.colcon_install_layout" 2>/dev/null)" != "isolated" ]]; then
  echo "SOURCE_INSTALL_BASE must be an isolated colcon install: ${source_install_base}" >&2
  exit 2
fi

for package in "${packaged_packages[@]}"; do
  if [[ ! -d "${source_install_base}/${package}" ]]; then
    echo "Required package '${package}' is missing from ${source_install_base}" >&2
    exit 2
  fi
done

if [[ ! -x "${source_install_base}/ros_portal/lib/ros_portal/ros_portal_node" ]]; then
  echo "Built ROS Portal node is missing from ${source_install_base}" >&2
  exit 2
fi

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

readonly package_xml="${source_install_base}/ros_portal/share/ros_portal/package.xml"
readonly upstream_version="$(sed -nE 's:.*<version>([^<]+)</version>.*:\1:p' "${package_xml}")"
if [[ -z "${upstream_version}" ]]; then
  echo "Unable to determine ROS Portal version from ${package_xml}" >&2
  exit 2
fi

readonly debian_version="${upstream_version}-1"
readonly debian_arch="$(dpkg --print-architecture)"
readonly package_name="ros-${ros_distro}-livekit-portal"
readonly command_name="ros-portal-${ros_distro}"
readonly install_prefix="/opt/livekit/ros/${ros_distro}"
readonly work_root="${repo_root}/package-deb/${ros_distro}-${debian_arch}"
readonly package_root="${work_root}/package-root"
readonly staging_prefix="${package_root}${install_prefix}"
readonly deb_path="${output_dir}/${package_name}_${debian_version}_${debian_arch}.deb"

rm -rf "${work_root}"
mkdir -p "${staging_prefix}" "${output_dir}"
touch "${work_root}/COLCON_IGNORE"

for package in "${packaged_packages[@]}"; do
  cp -a "${source_install_base}/${package}" "${staging_prefix}/"
done

shopt -s nullglob
for setup_file in "${source_install_base}"/{.colcon_install_layout,_local_setup_util_sh.py,_local_setup_util_ps1.py,local_setup.*,setup.*}; do
  cp -a "${setup_file}" "${staging_prefix}/"
done

# Colcon generates absolute paths in shell setup files.
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
exec ros2 launch ros_portal ros_portal.launch.py "\$@"
EOF
chmod 0755 "${package_root}/usr/bin/${command_name}"

declare -a package_xmls=()
for package in "${packaged_packages[@]}"; do
  package_xmls+=("${source_install_base}/${package}/share/${package}/package.xml")
done
mapfile -t rosdep_keys < <(
  sed -nE 's:.*<(depend|exec_depend)>([^<]+)</(depend|exec_depend)>.*:\2:p' \
    "${package_xmls[@]}" |
    grep -vxE 'ros_portal|ros_portal_config|ros_portal_msgs|ros2_medkit_cmake|ros2_medkit_serialization' |
    LC_ALL=C sort -u
)
mapfile -t rosdep_packages < <(
  rosdep resolve --rosdistro "${ros_distro}" "${rosdep_keys[@]}" |
    awk '
      $0 == "#apt" { apt = 1; next }
      /^#/ { apt = 0 }
      apt && NF { for (i = 1; i <= NF; ++i) print $i }
    ' |
    LC_ALL=C sort -u
)

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

mkdir -p "${package_root}/DEBIAN"
mkdir -p "${work_root}/debian"
printf 'Source: %s\n\nPackage: %s\nArchitecture: any\n' \
  "${package_name}" "${package_name}" >"${work_root}/debian/control"
declare -a library_paths=("-l/opt/ros/${ros_distro}/lib")
for package in "${packaged_packages[@]}"; do
  [[ -d "${staging_prefix}/${package}/lib" ]] &&
    library_paths+=("-l${staging_prefix}/${package}/lib")
done

shlibs_output="$(cd "${work_root}" && dpkg-shlibdeps --ignore-missing-info -O \
  "${library_paths[@]}" "${elf_arguments[@]}")"
declare -a runtime_dependencies=("ros-${ros_distro}-ros-base" "${rosdep_packages[@]}")
if [[ "${shlibs_output}" == shlibs:Depends=* ]]; then
  IFS=',' read -r -a shlibs_dependencies <<<"${shlibs_output#shlibs:Depends=}"
  runtime_dependencies+=("${shlibs_dependencies[@]}")
fi
mapfile -t runtime_dependencies < <(
  printf '%s\n' "${runtime_dependencies[@]}" |
    sed 's/^[[:space:]]*//' | LC_ALL=C sort -u
)
printf -v depends_field '%s, ' "${runtime_dependencies[@]}"
depends_field="${depends_field%, }"

installed_size="$(du -sk "${package_root}" | cut -f1)"
cat >"${package_root}/DEBIAN/control" <<EOF
Package: ${package_name}
Version: ${debian_version}
Section: misc
Priority: optional
Architecture: ${debian_arch}
Maintainer: LiveKit <sderosa@livekit.io>
Homepage: https://github.com/livekit/ros-portal
Installed-Size: ${installed_size}
Depends: ${depends_field}
Description: ROS Portal for ROS 2 ${ros_distro}
 Connects a ROS 2 graph to LiveKit for remote topics, services, video,
 diagnostics, and ROS 2 CLI operations.
EOF

dpkg-deb --root-owner-group --build "${package_root}" "${deb_path}"
(
  cd "${output_dir}"
  sha256sum "$(basename "${deb_path}")" >"$(basename "${deb_path}").sha256"
)

echo "Packaged ${deb_path} from ${source_install_base}"
