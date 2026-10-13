#!/usr/bin/env bash
# Download the SuiteSparse matrices used in Phase 3 and convert them to the
# harness's binary CSR format. Output goes to matrices/ (git-ignored).
#
#   scripts/env.sh scripts/download_matrices.sh
#
# Source: SuiteSparse Matrix Collection, https://sparse.tamu.edu
# (T. A. Davis and Y. Hu, "The University of Florida Sparse Matrix Collection",
#  ACM TOMS 38(1), 2011). Matrices are fetched as Matrix Market tarballs from
#  https://sparse.tamu.edu/MM/<group>/<name>.tar.gz
#
#   name            group      structure                      (from the collection)
#   bcsstk17        HB         FEM, structural stiffness       symmetric, banded
#   scircuit        Hamm       circuit simulation              unsymmetric, irregular
#   web-NotreDame   SNAP       web graph (directed)            pattern, power-law rows
#   luxembourg_osm  DIMACS10   road network (OpenStreetMap)    pattern, very short rows
set -euo pipefail
cd "$(dirname "$0")/.."

MATRICES=(
    "HB bcsstk17"
    "Hamm scircuit"
    "SNAP web-NotreDame"
    "DIMACS10 luxembourg_osm"
)

mkdir -p matrices
manifest=matrices/MANIFEST.txt
: > "$manifest"
for entry in "${MATRICES[@]}"; do
    read -r group name <<< "$entry"
    tgz="matrices/$name.tar.gz"
    out="matrices/$name.csr"
    if [ ! -f "$tgz" ]; then
        # The file server intermittently answers 403; wget does not retry those.
        ok=0
        for attempt in 1 2 3 4 5 6; do
            if wget --no-verbose -O "$tgz.part" "https://sparse.tamu.edu/MM/$group/$name.tar.gz"; then
                ok=1; break
            fi
            echo "download attempt $attempt failed for $group/$name; retrying in $((attempt * 10))s" >&2
            sleep $((attempt * 10))
        done
        [ $ok = 1 ] || { rm -f "$tgz.part"; echo "giving up on $group/$name" >&2; exit 1; }
        mv "$tgz.part" "$tgz"
    fi
    echo "$(sha256sum "$tgz" | cut -d' ' -f1)  $group/$name.tar.gz" >> "$manifest"
    if [ ! -f "$out" ]; then
        tar -xzf "$tgz" -C matrices "$name/$name.mtx"
        python3 scripts/mtx2bin.py "matrices/$name/$name.mtx" "$out"
        rm -rf "matrices/$name"
    fi
done
echo "sha256 of downloaded archives: $manifest"
