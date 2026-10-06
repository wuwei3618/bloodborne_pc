#!/bin/bash
# The x86_64 Homebrew in /usr/local that the macOS build uses (docs/MACOS.md), on an Apple silicon
# Mac. Homebrew's installer no longer sets it up: x86_64 macOS is a Tier 3 platform since
# Homebrew 7 (September 2026). brew still runs there, under Rosetta 2 too, and existing Intel
# bottles stay; newer formula versions build from source. This prepares /usr/local as the
# installer did and clones brew into it.
set -euo pipefail
if [[ $(uname -s) != Darwin ]]; then echo 'For macOS only.' >&2; exit 1; fi
prefix=/usr/local
if [[ -x $prefix/bin/brew ]]; then echo "$prefix/bin/brew is already installed."; exit 0; fi
user=$(id -un)
# The directories brew writes to, owned by the user and writable by the admin group.
dirs=()
for dir in bin etc include lib sbin share opt var Frameworks etc/bash_completion.d lib/pkgconfig \
    share/aclocal share/doc share/info share/locale share/man share/man/man1 share/man/man2 \
    share/man/man3 share/man/man4 share/man/man5 share/man/man6 share/man/man7 share/man/man8 \
    share/zsh share/zsh/site-functions var/log var/homebrew var/homebrew/linked Cellar Caskroom \
    Homebrew; do
    dirs+=("$prefix/$dir")
done
sudo mkdir -p "${dirs[@]}"
sudo chown "$user:admin" "${dirs[@]}"
sudo chmod u+rwx,g+rwx "${dirs[@]}"
sudo chmod go-w "$prefix/share/zsh" "$prefix/share/zsh/site-functions" # zsh's compaudit
git clone https://github.com/Homebrew/brew "$prefix/Homebrew"
ln -sf ../Homebrew/bin/brew "$prefix/bin/brew"
arch -x86_64 "$prefix/bin/brew" --version
