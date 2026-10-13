# Mermaid file type: `.mmd` opens in *Qinta*

The companion [mermaid-file-type.mmd](mermaid-file-type.mmd) is a bare Mermaid file.
On each OS it should be a file type Qinta claims:

| OS | How it is declared | Check |
| --- | --- | --- |
| Windows | `qinta.exe /register` (registry) | Open With lists Qinta |
| macOS | `UTExportedTypeDeclarations` + a second document type | Double-click opens Qinta |
| Linux | `text/x-mermaid` in `usr/share/mime/packages` + `MimeType=` | Open With lists Qinta once integrated |

1. **macOS:** double-click `mermaid-file-type.mmd` in Finder. It opens in Qinta and
   renders the flowchart. Get Info names the kind **Mermaid diagram**.
2. **Linux:** integrate the AppImage (or `xdg-mime install
   usr/share/mime/packages/qinta-mermaid.xml` from the extracted AppDir), then
   `xdg-mime query filetype mermaid-file-type.mmd` reads `text/x-mermaid`.
3. `qinta /register` off Windows is refused and names `.mmd` alongside `.md`.

```mermaid
flowchart TD
    A[Fixture] --> B{Type declared?}
    B -->|yes| C[Opens in Qinta]
```

> The diagram above sits between a table, a list and a quote with `code`.
