#!/bin/sh
set -e

rm -Rf udptap-tunnel-0.1.0
mkdir udptap-tunnel-0.1.0
cp ../{Makefile,udptap.c} udptap-tunnel-0.1.0/
tar -czf udptap-tunnel_0.1.0.orig.tar.gz udptap-tunnel-0.1.0

cp -R debian udptap-tunnel-0.1.0/
cd udptap-tunnel-0.1.0
debuild
