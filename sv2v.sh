#!/bin/sh
for f in *.sv; do sv2v --write=./netlist/${f%.sv}.v -E=Always -E=Assert -E=Interface -E=Logic -E=UnbasedUnsized $f; done
