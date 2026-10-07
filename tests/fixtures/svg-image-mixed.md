# SVG images inside a mixed document

A local SVG image must render inline, at its intrinsic size, without covering the
**bold**, *italic* or `code` text around it. The sample draws a magenta bar over a green
circle, two colours no theme uses, so a page render can count them.

![magenta bar and green circle](svg-image-sample.svg)

## After the image

The paragraph after the image must start below it. A [link](https://example.com) and a
list follow:

- the first item
- the second item, with `inline code`

> A quote after the image, holding its own **emphasis**.

| Shape | Colour |
|---|---|
| Bar | magenta `#E040FB` |
| Circle | green `#00C853` |

```text
A code block after the table.
```

### The image again, inside a list

1. Before the image.
2. ![the same SVG](svg-image-sample.svg)
3. After the image.
