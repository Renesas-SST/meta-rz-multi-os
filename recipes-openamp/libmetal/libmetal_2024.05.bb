FILESEXTRAPATHS:prepend := "${THISDIR}/files:"
SRCBRANCH ?= "master"
SRCREV ?= "3aee6be866b190d2e2b4997fedbd976a0c37c0c6"
LIC_FILES_CHKSUM ?= "file://LICENSE.md;md5=f4d5df0f12dcea1b1a0124219c0dbab4"

SRC_URI:append = " \
    file://0001-machine-chipname-fix.patch \
  "

include libmetal.inc
