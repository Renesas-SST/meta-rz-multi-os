SUMMARY = "RPMsg firmware"
LICENSE = "CLOSED"

inherit deploy

EXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI = " \
    file://rzg2l_cm33_rpmsg_linux_rtos_example.elf \
    file://rzv2h_cm33_rpmsg_linux_rtos_example.elf \
    file://rzv2h_cr8_core0_rpmsg_linux_rtos_example.elf \
    file://rzv2h_cr8_core1_rpmsg_linux_rtos_example.elf \
    file://rzg2l_cm33_rpmsg_linux_rtos_example_non_secure_code.bin \
    file://rzg2l_cm33_rpmsg_linux_rtos_example_non_secure_vector.bin \
    file://rzg2l_cm33_rpmsg_linux_rtos_example_secure_code.bin \
    file://rzg2l_cm33_rpmsg_linux_rtos_example_secure_vector.bin \
    file://rzv2h_cm33_rpmsg_linux_rtos_example.bin \
    file://rzv2h_cr8_core0_rpmsg_linux_rtos_example_itcm.bin \
    file://rzv2h_cr8_core0_rpmsg_linux_rtos_example_sdram.bin \
    file://rzv2h_cr8_core0_rpmsg_linux_rtos_example_sram.bin \
    file://rzv2h_cr8_core1_rpmsg_linux_rtos_example_itcm.bin \
    file://rzv2h_cr8_core1_rpmsg_linux_rtos_example_sdram.bin \
    file://rzv2h_cr8_core1_rpmsg_linux_rtos_example_sram.bin \
"

INSANE_SKIP:${PN} = "arch"

do_install() {
    install -d ${D}${nonarch_base_libdir}/firmware
    install -d ${D}/boot

    install -m 0644 ${UNPACKDIR}/*.elf ${D}${nonarch_base_libdir}/firmware/

    install -m 0644 ${UNPACKDIR}/*.bin ${D}/boot/
}

do_deploy() {
    install -m 0644 ${UNPACKDIR}/*rpmsg* ${DEPLOYDIR}/
}

addtask deploy after do_install before do_build

# Force the package to be redeployed for each target. This is essential
# to ensure the firmware files is available in DEPLOYDIR, allowing it to be
# installed into partition 1.
# Without this, other targets may fail during the build process.
do_deploy[nostamp] = "1"

FILES:${PN} += " \
    ${nonarch_base_libdir}/firmware \
    /boot \
"
