#
# RPMsg Sample for Linux
#

SUMMARY = "Sample rpmsg application"
SECTION = "examples"
LICENSE = "CLOSED"

DEPENDS = "libmetal open-amp"

SRC_URI = " \
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
