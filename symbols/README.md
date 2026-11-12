# symbols/ : the address map of `__FF.EXE` v1.09

One row for every function and data address of the original program that the C names. The build reads these files (`tools/gen_port.py` for the port, `verify/gen_thunks.py` for the tests).

| Column | Meaning |
|---|---|
| `address` | Address in the original program (code object at 0x244000, data object at 0x355000) |
| `name` | The C name |
| `kind` | `func` or `data` |
| `nargs` | For functions: the number of arguments |
| `library` | The library or `GAME` |
| `status` | `decomp`: the C in `src/` replaces it. `orig`: left as the original (third-party code, or data) |
| `notes` | The source file, and anything worth knowing |

The files are split by the parts the work was done in (`symbols-g4-b.csv` and so on): see "The part names" in [docs/README.md](../docs/README.md).
