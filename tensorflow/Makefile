SHELL = /bin/bash

CC=gcc
CXX=g++

tensorflow_REPO = https://github.com/tensorflow/tensorflow.git
tensorflow_COMMIT = v2.7.0
hdfit_PATCH = 0001-Integrating-HDFIT.patch

.DEFAULT_GOAL := tensorflow
.PHONY: tensorflow

all: tensorflow clean veryclean

clean:
	-cd tensorflow && make clean

veryclean:
	rm -rf tensorflow

tensorflow:
	@echo "Cloning $(@D)..."
	git clone $($(@D)_REPO) $(@D)
	cd $(@D) && git checkout $($(@D)_COMMIT)
	chmod -R u=rwX,go=rX $(@D)
	@touch $(@D)/.cloned


