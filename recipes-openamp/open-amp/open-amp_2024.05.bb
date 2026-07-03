FILESEXTRAPATHS:prepend := "${THISDIR}/files:"
SRCBRANCH ?= "master"
SRCREV ?= "c468328487a1e0596307a5ef7172756819e15745"
LIC_FILES_CHKSUM ?= "file://LICENSE.md;md5=dfc0adf4d04cc738ba65b7d3f587dca5"

SRC_URI:append = " \
        file://0001-change-addr-of-vring-desc-to-zero.patch \
  "

include open-amp.inc
