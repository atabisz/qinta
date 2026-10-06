# Printing CJK text 印刷テスト

This fixture exists so a print path can be told apart from a plausible one: every page that
carries CJK text needs a CJK-capable font, and a printer or PDF that substituted a Latin face
would print boxes. **Bold**, *italic*, `inline code` and a [link](https://example.com) sit
beside it so the surrounding content is checked too.

## 中文段落

这是一段用于打印测试的中文文本。它包含**粗体**、*斜体*和`代码`，以及一个[链接](https://example.com)。

## 日本語の段落

これは印刷のテストに使う日本語の文章です。ひらがな、カタカナ、漢字が含まれています。

## 한국어 문단

이 문단은 인쇄 테스트를 위한 한국어 텍스트입니다.

| Language | Sample | Note |
|---|---|---|
| Chinese | 你好，世界 | Simplified |
| Japanese | こんにちは世界 | Kana and kanji |
| Korean | 안녕하세요 | Hangul |

- First item 第一项
- Second item 二番目
  - Nested 중첩

> A quote with CJK: 学而时习之，不亦说乎。

```text
plain code block, no CJK, so a monospace face is printed too
```
