#!/bin/bash

OCR_HOME=/home/berserker/main/workspace/Research/OCRDataRaceDetection/OCRDebugRuntime

APP_HOME=/home/berserker/main/src/ocr-1.1/xstg/apps/apps

export OCR_INSTALL=$OCR_HOME/install && export LD_LIBRARY_PATH=$OCR_INSTALL/lib:$APP_HOME/libs/install/x86/lib:$LD_LIBRARY_PATH && export OCR_CONFIG=$APP_HOME/examples/$1/install/x86/generated.cfg

EXE_POS="$APP_HOME/examples/$1/install/x86/$1 ${@:2}"

echo $EXE_POS

#$EXE_POS
pin -t obj-intel64/OCRViz.so -- $EXE_POS
#pin -pause_tool 20 -t obj-intel64/OCRViz.so -- $EXE_POS
#/home/berserker/main/src/pin-3.2-81205-gcc-linux/pin -t obj-intel64/OCRViz.so -- $EXE_POS
#dot -Teps -o cg.eps cg.dot
#see cg.eps
