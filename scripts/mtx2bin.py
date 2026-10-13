#!/usr/bin/env python3
"""Convert a Matrix Market (.mtx) file to the harness's binary CSR format.

    mtx2bin.py INPUT.mtx OUTPUT.csr

Supported: `matrix coordinate` with field real / integer / pattern and
symmetry general / symmetric / skew-symmetric.
  - symmetric / skew-symmetric: the stored triangle is mirrored
    (negated for skew-symmetric) so the output holds the full matrix,
  - pattern: values are synthesised deterministically as
    ((i + j) mod 11 + 1) / 11 with 0-based (i, j),
  - duplicate entries are summed, columns are sorted within each row,
  - values are stored as float32.

Output layout (little-endian), read by load_csrbin() in src/harness/matrix.c:
    b"CSRB", uint32 version=1, rows, cols, nnz,
    uint32 row_ptr[rows+1], uint32 col_idx[nnz], float32 values[nnz]
"""
import struct
import sys

import numpy as np


def read_mtx(path):
    with open(path, "r") as f:
        header = f.readline().split()
        if len(header) < 5 or header[0] != "%%MatrixMarket" or header[1] != "matrix":
            raise ValueError(f"{path}: not a Matrix Market matrix file")
        layout, field, symmetry = (h.lower() for h in header[2:5])
        if layout != "coordinate":
            raise ValueError(f"{path}: only coordinate format is supported")
        if field not in ("real", "integer", "pattern"):
            raise ValueError(f"{path}: unsupported field '{field}'")
        if symmetry not in ("general", "symmetric", "skew-symmetric"):
            raise ValueError(f"{path}: unsupported symmetry '{symmetry}'")
        line = f.readline()
        while line.startswith("%"):
            line = f.readline()
        rows, cols, nnz = (int(t) for t in line.split())
        data = np.array(f.read().split(), dtype=np.float64)

    width = 2 if field == "pattern" else 3
    if data.size != nnz * width:
        raise ValueError(f"{path}: expected {nnz} entries, found {data.size // width}")
    data = data.reshape(nnz, width)
    i = data[:, 0].astype(np.int64) - 1
    j = data[:, 1].astype(np.int64) - 1
    v = ((i + j) % 11 + 1) / 11.0 if field == "pattern" else data[:, 2]

    if symmetry != "general":
        off = i != j
        sign = -1.0 if symmetry == "skew-symmetric" else 1.0
        i, j, v = (np.concatenate([i, j[off]]), np.concatenate([j, i[off]]),
                   np.concatenate([v, sign * v[off]]))
        if field == "pattern":  # mirrored entries get their own synthetic value
            v = ((i + j) % 11 + 1) / 11.0
    return rows, cols, i, j, v, field, symmetry


def to_csr(rows, i, j, v):
    order = np.lexsort((j, i))
    i, j, v = i[order], j[order], v[order]
    if i.size:  # sum duplicates
        new = np.ones(i.size, dtype=bool)
        new[1:] = (i[1:] != i[:-1]) | (j[1:] != j[:-1])
        idx = np.cumsum(new) - 1
        v = np.bincount(idx, weights=v)
        i, j = i[new], j[new]
    row_ptr = np.zeros(rows + 1, dtype=np.uint32)
    np.cumsum(np.bincount(i, minlength=rows), out=row_ptr[1:])
    return row_ptr, j.astype(np.uint32), v.astype(np.float32)


def main(argv):
    if len(argv) != 3:
        sys.exit(__doc__)
    rows, cols, i, j, v, field, symmetry = read_mtx(argv[1])
    if max(rows, cols) >= 2**32 or i.size >= 2**32:
        sys.exit("matrix too large for uint32 indices")
    row_ptr, col_idx, values = to_csr(rows, i, j, v)
    with open(argv[2], "wb") as f:
        f.write(b"CSRB")
        f.write(struct.pack("<4I", 1, rows, cols, col_idx.size))
        f.write(row_ptr.astype("<u4").tobytes())
        f.write(col_idx.astype("<u4").tobytes())
        f.write(values.astype("<f4").tobytes())
    lens = np.diff(row_ptr.astype(np.int64))
    print(f"{argv[2]}: {rows}x{cols} nnz={col_idx.size} field={field} symmetry={symmetry} "
          f"row_len mean={lens.mean():.2f} max={lens.max()} cv={lens.std() / lens.mean():.3f}")


if __name__ == "__main__":
    main(sys.argv)
