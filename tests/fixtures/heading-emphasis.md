# Heading emphasis

Emphasis inside a heading keeps the heading's size and baseline. Before the fix, `*italic*` and
`**bold**` in a heading drew at body size, top-aligned, so the word read like a superscript. The
same was true one level down: `*italic*` in a table header cell lost the header's bold.

Check each heading below: every emphasised word should be as tall as the words beside it and sit on
the same baseline. Body text keeps its own emphasis unchanged.

# H1 with *italic*, **bold** and ***both***

## H2 with *italic*, **bold** and ***both***

### H3 with *italic*, **bold** and ***both***

#### H4 with *italic*, **bold** and ***both***

##### H5 with *italic*, **bold** and ***both***

###### H6 with *italic*, **bold** and ***both***

## Nested and mixed: *italic with **bold** inside*, [a *link*](https://example.com), ~~*struck*~~

## Other spans: `code`, [link](https://example.com), ~~strike~~ and ==highlight==

Body paragraph with *italic*, **bold**, ***both***, `code` and a [link](https://example.com), for
comparison with the headings above.

> ### A heading in a quote with *italic* and **bold**
>
> Quoted body with *italic* and **bold**.

- List item with *italic* and **bold**
- Second item, plain

| Header *italic* | Header **bold** | Plain header |
|-----------------|-----------------|--------------|
| cell *italic*   | cell **bold**   | plain cell   |

```text
Code blocks are untouched: *not italic* and **not bold**.
```
