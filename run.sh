#!/bin/bash
export OCR_HOME=/home/berserker/main/src/ocr/ocr && export OCR_INSTALL=$OCR_HOME/install/x86 && export LD_LIBRARY_PATH=$OCR_HOME/install/x86/lib:$LD_LIBRARY_PATH && export OCR_CONFIG=$OCR_HOME/install/x86/config/default.cfg

EXE_POS="$OCR_HOME/examples/$1/install/x86/$1 $2"

echo $OCR_HOME/examples/$1/install/x86/$1 $2

pin -t obj-intel64/OCRViz.so -- $EXE_POS
dot -Teps -o cg.eps cg.dot
see cg.eps
