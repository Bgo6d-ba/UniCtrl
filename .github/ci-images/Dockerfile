# Container images the CI jobs run in, pinned by digest. Never built: the ci-images workflow reads
# the FROM lines, and Dependabot's docker ecosystem bumps the digests (it can't update `container:`
# in workflow files). Jobs look an image up by the name before the digest, e.g. debian:stable-slim.
FROM debian:stable-slim@sha256:5bc3287b25407c965a30f38e32603dc253a3869e1b12a21ac09bfc27fd8b13ce AS debian-stable
FROM debian:testing-slim@sha256:fcaae10ef02f2c0cc2e4cb291ade86c2d259143fb919ee4544a0236a90cc975a AS debian-testing
FROM debian:trixie-slim@sha256:a99cfc517144bc59b1978475ec53b46ecabec7e43635402ee5b77cc54cd1b20a AS debian-trixie
FROM fedora:latest@sha256:43b29f65a41eb9c35e1cd5323e3bdf3b655c2357a9f4f1ff2f9c2798e5045d80 AS fedora-latest
FROM fedora:rawhide@sha256:a9bab18d01cf2c2cf62f3e79c72623405bce14ac062995cc6651a3073c802e41 AS fedora-rawhide
FROM opensuse/tumbleweed:latest@sha256:7d675c57702d593e2b92961d9d7670728052468a7bea4abbf0d599f104a0d407 AS opensuse-tumbleweed
FROM archlinux:latest@sha256:b21322c663be387c0ed9cbc7bbbfe18e41633ad4e7b7c77cfad45f128be20040 AS archlinux
FROM ubuntu:latest@sha256:da6fc2be547864451aa253836dd926da33623312df4a9a243e35dc877c378a78 AS ubuntu-latest
FROM ubuntu:devel@sha256:ee126c2fa0249079a7e24ae3a3d29b04783ef93ab33c868751c9a1289d3fffff AS ubuntu-devel
FROM ghcr.io/flathub-infra/flatpak-github-actions:kde-6.10@sha256:62920cbbb53c4e047801562c0475d70393482de9698dfeac740aad99e6c674e6 AS flatpak-kde
