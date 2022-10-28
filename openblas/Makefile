SHELL = /bin/bash

CC=gcc
CXX=g++

openblas_REPO = https://github.com/xianyi/OpenBLAS
openblas_COMMIT = 00534523ad999d89945d23b7df0eafc69c31f1b3
hdfit_PATCH = 0001-Integrating-HDFIT.patch

.DEFAULT_GOAL := openblas
.PRECIOUS: %/.cloned %/.patched %/.compiled
.PHONY: openblas

all: openblas clean veryclean

clean:
	-cd openblas && make clean

veryclean:
	rm -rf openblas

openblas/.cloned:
	@echo "Cloning $(@D)..."
	git clone $($(@D)_REPO) $(@D)
	cd $(@D) && git checkout $($(@D)_COMMIT)
	chmod -R u=rwX,go=rX $(@D)
	@touch $(@D)/.cloned

openblas/.patched: openblas/.cloned
	@echo "Patching $(@D)..."
	cd $(@D) && git am ../$(hdfit_PATCH)
	@touch $(@D)/.patched

openblas: openblas/.patched
	@echo "Compiling $(@)..."
	cd $(@) && make CC=$(CC)
