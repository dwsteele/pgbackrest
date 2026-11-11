#!/bin/sh
####################################################################################################################################
# Update the apt index. When the Ubuntu archive fails, replace it in the apt sources with a mirror and update again.
#
# archive.ubuntu.com and security.ubuntu.com are replaced with mirrors.edge.kernel.org. ports.ubuntu.com is replaced with
# mirrors.mit.edu, a mirror that has ubuntu-ports. The mirrors use http: base images have no CA certificates and apt verifies the
# signed release files.
#
# Run as root.
####################################################################################################################################
set -eu

# Apt sources that use the Ubuntu archive
sourceList()
{
    grep -lE 'https?://((archive|security)\.ubuntu\.com/ubuntu|ports\.ubuntu\.com/ubuntu-ports)' /etc/apt/apt-mirrors.txt \
        /etc/apt/sources.list /etc/apt/sources.list.d/* 2>/dev/null || true
}

# Replace the Ubuntu archive with the mirrors in the apt sources
mirrorUse()
{
    for file in $(sourceList); do
        sed -i -E -e 's#https?://(archive|security)\.ubuntu\.com/ubuntu#http://mirrors.edge.kernel.org/ubuntu#g' \
            -e 's#https?://ports\.ubuntu\.com/ubuntu-ports#http://mirrors.mit.edu/ubuntu-ports#g' "${file}"
        echo "${file} now uses a mirror"
    done
}

# Use the mirrors when the archive does not serve the release file for this release. curl is not in the base images.
if [ -n "$(sourceList)" ] && command -v curl > /dev/null; then
    case $(dpkg --print-architecture) in
        amd64|i386)
            archive=http://archive.ubuntu.com/ubuntu
            ;;
        *)
            archive=http://ports.ubuntu.com/ubuntu-ports
            ;;
    esac

    if ! curl -sfI --max-time 20 -o /dev/null "${archive}/dists/$(. /etc/os-release && echo "${VERSION_CODENAME}")/InRelease"; then
        echo "${archive} did not respond"
        mirrorUse
    fi
fi

# Refresh the index under a total bound, which is the part apt has no setting for. A mirror delivering a few bytes per second resets
# apt's inactivity timeout indefinitely, so without this the job hangs until the workflow timeout rather than failing. A fresh
# attempt may be served by a different backend, so retry before giving up. apt-get update exits with 0 when an index fails to
# download unless --error-on=any is set. Use the mirrors after a failed attempt.
for attempt in 1 2 3; do
    if timeout 120 apt-get update --error-on=any; then
        exit 0
    fi

    echo "apt-get update attempt ${attempt} of 3 timed out or failed"
    mirrorUse
done

exit 1
