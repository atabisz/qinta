# Strikethrough and highlight across inline elements

A `~~` or `==` pair spans the inline elements between its two halves, as on GitHub. Before the fix,
`~~*struck*~~` struck the text *after* the pair instead: the opening `~~` found no partner, and the
next two pairs matched the wrong runs. Every line below should strike exactly the words named in
it, and nothing else.

The line crosses the middle of the lowercase letters, in code spans too. It used to sit on the
baseline, where it read as an underline, and a code span inside a strike was not struck at all.

Body ~~*struck italic*~~ and ~~plain struck~~, then plain again.

~~**struck bold**~~, ~~[a struck link](https://example.com) and words~~, ~~`struck code`~~.

==*highlighted italic*== and ==plain highlight==, then plain again.

A strike ~~that runs over
a soft line break~~ ends here.

Spans inside a span: *italic with ~~struck~~ inside*, and ~~*struck italic with ==mark==*~~.

## Heading with ~~*struck italic*~~ and ~~plain struck~~

| Column | ~~*Struck header*~~ |
| --- | --- |
| row | ~~**struck bold cell**~~ and plain |
| row | ==*highlighted cell*== and plain |

- List item with ~~*struck italic*~~ and plain.
- ~~This half stays literal *x*
  - because a nested list ends the run~~

> Quote with ~~*struck italic*~~ and ~~plain struck~~.

Superscript and subscript still refuse whitespace, so x^a
b^ stays literal across the line break, while x^2^ and H~2~O work.

```
Code keeps ~~*literal*~~ markers.
```
