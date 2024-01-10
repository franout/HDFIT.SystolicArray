#!/bin/bash

echo "Applying patch for allowing synthesis"
cd ..
git apply ${SDENV_DESIGN_DIR}/0001-Integrating-HDFIT.patch

