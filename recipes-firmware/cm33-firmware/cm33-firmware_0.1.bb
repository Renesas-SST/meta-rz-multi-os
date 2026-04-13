SUMMARY = "CM33 RPMsg firmware"
LICENSE = "CLOSED" 

EXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI = " \
    file://rzv2l_cm33_rpmsg_linux-rtos_demo.elf \
    file://rzv2h_cm33_rpmsg_linux-rtos_demo.elf \
    file://rzv2h_cm33_rpmsg_linux-rtos_demo.bin \
    file://rzv2h_cr8_core0_rpmsg_linux-rtos_demo_itcm.bin \
    file://rzv2h_cr8_core0_rpmsg_linux-rtos_demo_sdram.bin \
    file://rzv2h_cr8_core0_rpmsg_linux-rtos_demo_sram.bin \
    file://rzv2h_cr8_core1_rpmsg_linux-rtos_demo_itcm.bin \
    file://rzv2h_cr8_core1_rpmsg_linux-rtos_demo_sdram.bin \
    file://rzv2h_cr8_core1_rpmsg_linux-rtos_demo_sram.bin \
    file://rzv2l_cm33_rpmsg_linux-rtos_demo_non_secure_code.bin \
    file://rzv2l_cm33_rpmsg_linux-rtos_demo_non_secure_vector.bin \
    file://rzv2l_cm33_rpmsg_linux-rtos_demo_secure_code.bin \
    file://rzv2l_cm33_rpmsg_linux-rtos_demo_secure_vector.bin \
" 

INSANE_SKIP:${PN} = "arch" 

do_install() {
    install -d ${D}/usr/lib/firmware
    install -d ${D}/boot
    install -m 0644 ${THISDIR}/files/rzv2l_cm33_rpmsg_linux-rtos_demo.elf ${D}/usr/lib/firmware/
    install -m 0644 ${THISDIR}/files/rzv2h_cm33_rpmsg_linux-rtos_demo.elf ${D}/usr/lib/firmware/
    install -m 0644 ${THISDIR}/files/rzv2h_cm33_rpmsg_linux-rtos_demo.bin ${D}/boot/
    install -m 0644 ${THISDIR}/files/rzv2h_cr8_core0_rpmsg_linux-rtos_demo_itcm.bin ${D}/boot/
    install -m 0644 ${THISDIR}/files/rzv2h_cr8_core0_rpmsg_linux-rtos_demo_sdram.bin ${D}/boot/
    install -m 0644 ${THISDIR}/files/rzv2h_cr8_core0_rpmsg_linux-rtos_demo_sram.bin ${D}/boot/
    install -m 0644 ${THISDIR}/files/rzv2h_cr8_core1_rpmsg_linux-rtos_demo_itcm.bin ${D}/boot/
    install -m 0644 ${THISDIR}/files/rzv2h_cr8_core1_rpmsg_linux-rtos_demo_sdram.bin ${D}/boot/
    install -m 0644 ${THISDIR}/files/rzv2h_cr8_core1_rpmsg_linux-rtos_demo_sram.bin ${D}/boot/
    install -m 0644 ${THISDIR}/files/rzv2l_cm33_rpmsg_linux-rtos_demo_non_secure_code.bin ${D}/boot/
    install -m 0644 ${THISDIR}/files/rzv2l_cm33_rpmsg_linux-rtos_demo_non_secure_vector.bin ${D}/boot/
    install -m 0644 ${THISDIR}/files/rzv2l_cm33_rpmsg_linux-rtos_demo_secure_code.bin ${D}/boot/
    install -m 0644 ${THISDIR}/files/rzv2l_cm33_rpmsg_linux-rtos_demo_secure_vector.bin ${D}/boot/
} 

FILES:${PN} += " \
    usr/lib/firmware \
    /boot \
"
