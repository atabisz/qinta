# GitHub alert titles in the UI language

Each alert below has a title line: an icon and a label. The label follows the UI language
(Settings > Language), in the viewer, in printed pages, and in HTML and DOCX exports. It used to
be English in every language, though the five labels were translated.

> [!NOTE]
> A note with **bold**, `code` and a [link](https://example.com).

> [!TIP]
> A tip with a list:
> - one
> - two

> [!IMPORTANT]
> Important, with ~~struck~~ and ==marked== words.

> [!WARNING]
> A warning that runs long enough to wrap onto a second line at the normal reading width, so the
> title and the body can be compared across a wrapped paragraph.

> [!CAUTION]
> Caution, before a table:
>
> | Language | Warning label |
> | --- | --- |
> | English | Warning |
> | German | Warnung |

> An ordinary quote has no title line.

| Check | Expected |
| --- | --- |
| UI in German | Hinweis, Tipp, Wichtig, Warnung, Achtung |
| UI in English | Note, Tip, Important, Warning, Caution |
