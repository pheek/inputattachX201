# Copyright 2025 Gentoo Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=8

DESCRIPTION="/dev/ttyS4 hard binded bridge for X201"
HOMEPAGE="https://github.com/pheek/inputattachX201"

if [[ "${PV}" == *9999* ]]; then
   inherit git-r3
   EGIT_REPO_URI="https://github.com/pheek/inputattachX201.git"
else
   SRC_URI="https://github.com/pheek/archive/${PV}.tar.gz"
   KEYWORDS="~amd64"
fi

LICENSE="GPL-2"
SLOT="0"
#KEYWORDS="~amd64 ~x86"
IUSE=""
DEPEND=""
RDEPEND="${DEPEND}"
BDEPEND=""

src_compile() {
   #compile.sh
   gcc "${WORKDIR}"/"$P"/src/"inputattachX201.c" -o "${WORKDIR}"/"$P"/src/inputattachX201 || die "Compile failed"
}

src_install() {
   #dobin inputattachX201
   exeinto /usr/bin
   doexe "${WORKDIR}"/"$P"/src/inputattachX201

   insinto /etc/udev/rules.d
   newins - 72-inputattachX201.rules <<<"KERNEL==\"uinput\", SUBSYSTEM==\"misc\", MODE=\"0660\", TAG+=\"uaccess\", OPTIONS+=\"static_node=uinput\""
}
