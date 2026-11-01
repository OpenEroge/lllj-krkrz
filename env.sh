# Build environment for the LLLJ krkrz_dev port (keeps linuxbrew out of the toolchain)
export PATH=/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin
export VCPKG_ROOT=$HOME/vcpkg
export VCPKG_FORCE_SYSTEM_BINARIES=1
export CMAKE_POLICY_VERSION_MINIMUM=3.5
unset PKG_CONFIG_PATH LD_LIBRARY_PATH CPATH LIBRARY_PATH
export LLLJ="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
