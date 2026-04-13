USE_OPENAMP ?= "1"

IMAGE_INSTALL:append = " \
	${@bb.utils.contains("USE_OPENAMP", "1", " libmetal ", "", d)} \
	${@bb.utils.contains("USE_OPENAMP", "1", " open-amp ", "", d)} \
	${@bb.utils.contains("USE_OPENAMP", "1", " rpmsg-sample ", "", d)} \
	${@bb.utils.contains("USE_OPENAMP", "1", " rpmsg-sample-v2h ", "", d)} \
	${@bb.utils.contains("USE_OPENAMP", "1", " cm33-firmware ", "", d)} \
"
