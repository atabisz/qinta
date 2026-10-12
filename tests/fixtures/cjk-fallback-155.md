# CJK fallback ordering 漢字フォールバック

This fixture exercises the font fallback chain `installFontFallback` seats (#155). Every range the
deleted DirectWrite chain mapped by hand appears below, mixed with ordinary content, so a run can
tell a document that fell back correctly from one that drew boxes — and so the content *around* the
CJK is checked too, not only the CJK itself.

The point of #155 is **order**, not coverage: Han unification shares code points across Chinese,
Japanese and Korean while the glyph shapes differ, so the ideographs below render in whichever
family resolves first. On a Japanese UI language 漢字 should come out of a Japanese face, on a Korean
one 漢字 out of a Korean face, and otherwise out of a Chinese one. A document that renders all of it
in *one* face is still readable, which is why no automated check reads pixels here — see
`tests/qt_entry_tests.cpp`'s `cjk_order` block for what is actually graded.

## Ideographs, the range the ordering decides

A paragraph of **unified ideographs** mixed with *emphasis*, `inline code` and a
[link](https://example.com): 这是中文，これは日本語、이것은 한국어입니다。

Compatibility ideographs (U+F900–U+FAFF): 豈 更 車 賈 滑. Extension A (U+3400–U+4DBF): 㐀 㐁 㐂.
Yijing hexagrams (U+4DC0–U+4DFF): ䷀ ䷁ ䷂.

Beyond the BMP, which is the range where a wide-character index map earns its keep —
extension B (U+20000+): 𠀋 𠂉 𠆢.

## Kana and hangul, the groups that led their own mapping

| Range | Sample | Expected face |
|---|---|---|
| Hiragana U+3040–309F | ひらがな の もじ | Japanese |
| Katakana U+30A0–30FF | カタカナ ノ モジ | Japanese |
| Halfwidth katakana U+FF65–FF9F | ｶﾀｶﾅ ﾊﾝｶｸ | Japanese |
| Hangul jamo U+1100–11FF | ᄀ ᄁ ᄂ | Korean |
| Hangul syllables U+AC00–D7AF | 안녕하세요 | Korean |
| Compatibility jamo U+3130–318F | ㄱ ㄴ ㄷ | Korean |

## Symbols, circles and squares

- Circled ideographs (U+3220–325F): ㈠ ㈡ ㈢ — and parenthesized hangul: ㈀ ㈜
- Circled katakana and era names (U+32D0–32FF): ㋐ ㋑ ㋿
- Squared katakana words (U+3300–3357): ㌔ ㌘ ㌧
  - Nested item, with CJK units (U+3358–33FF): ㎜ ㎡ ㏄ ㎞
- Vertical and compatibility forms (U+FE10–FE4F): ︐ ︑ ﹅ ﹆
- Fullwidth forms (U+FF00–FF64): （）：！？ ＡＢＣ １２３
- Bopomofo (U+3100–312F): ㄅ ㄆ ㄇ ㄈ
- Kangxi radicals and CJK punctuation (U+2E80–303F): ⺀ ⻱ 、 。 〈 〉
- Emoji, the tail of the chain: 🎨 🚀 ✅ — and a symbol with no colour form: ☯ ⚙

> A quote mixing scripts: 学而时习之，不亦说乎。 — 『論語』より、그리고 한국어 한 줄.

## Code, which must stay monospace

A fenced block, so a run can see the fallback did **not** reach the code face for ASCII while the
CJK inside it still resolves:

```text
ascii only on this line, so a monospace face is visible
漢字 on the next, which falls back without changing the line above
```

And inline: `const wchar_t* kJapanese[] = { L"Yu Gothic UI", L"Meiryo" };`

## Headings after the tables, to check the surrounding layout

### 三号标题 — a level-three heading with ideographs

Body text under it, so heading and body are both measured. **太字**、*斜体*、`コード` と
[リンク](https://example.com) が並んでいます。

#### 네 번째 수준

The deepest heading, with Latin and 한글 on one line, and a trailing sentence so the paragraph wraps
at a narrow width rather than fitting on a single line: 이 문장은 좁은 폭에서 줄바꿈이 어떻게
일어나는지 확인하기 위한 것입니다.
