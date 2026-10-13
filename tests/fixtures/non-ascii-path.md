# A non-ASCII path opens from the command line

Copy this file to a folder and name that neither ASCII nor your code page can
spell, then open it **from the command line** and **by double-clicking it** in
Explorer once Qinta is the `.md` handler (`qinta /register`):

| Copy to | Fails before 2026-10-14 on a cp1252 host because |
| --- | --- |
| `über\café.md` | `é` reaches `main()` as the single byte `0xE9`, not UTF-8 |
| `über\笔记.md` | the ideographs are not in cp1252 and arrive as `??` |

- The document opens, and the caption reads `Qinta - café.md`.
- `qinta über\笔记.md --printpages out-ü` writes its pages **into** `out-ü`.
- A double-click is the same path: the association launches `qinta.exe "%1"`.

> The rest must lay out as before: **bold**, *italic*, `inline code`, a
> [link](https://example.com) and $x^2$.

```text
tinta::launchArguments() -- QCoreApplication::arguments() after the program name, UTF-8
```
