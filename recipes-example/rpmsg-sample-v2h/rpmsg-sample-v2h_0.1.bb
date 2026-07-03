#
# RPMsg Sample for Linux
#

SUMMARY = "Sample rpmsg application"
SECTION = "examples"
LICENSE = "BSD-3-Clause"
LIC_FILES_CHKSUM = "file://LICENSE.md;md5=5dc58bc85a5f47a764ce05e21ff4bd2b"

DEPENDS = "libmetal open-amp"

SRC_URI = " \
    file://LICENSE.md \
    file://platform_info.c \
    file://platform_info.h \
    file://OpenAMP_RPMsg_cfg.h \
    file://helper.c \
    file://rsc_table.h \
    file://main.c \
    file://rz_rproc.c \
    file://Makefile \
"

S = "${WORKDIR}/sources-unpack"

do_compile() {
    oe_runmake
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 rpmsg_sample_client_v2h ${D}${bindir}
}
