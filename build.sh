#!/bin/bash
# $1: install dir (absoluted or related path)
SRC_ROOT=$(pwd)

function build_all()
{
	export CFLAGS+="-I${INSTALL_PATH}/include"
	export LDFLAGS+="-L${INSTALL_PATH}/lib"
	autoreconf -fiv || exit 1
	if [ "${HOST}" == "" ]; then
		./configure --prefix=${INSTALL_PATH} || exit 1
	else
		./configure --host ${HOST} --prefix=${INSTALL_PATH} || exit 1
	fi

	make
}

function install_all()
{
	make install
}

function init_system_env()
{
	INSTALL_PATH=${1:-"../out"}
	if [[ "${INSTALL_PATH}" != "" ]]; then
		mkdir "${INSTALL_PATH}"
		cd "${INSTALL_PATH}"
		INSTALL_PATH=$(pwd)
		cd "${SRC_ROOT}"
	fi
}

init_system_env "$@"
build_all
install_all

