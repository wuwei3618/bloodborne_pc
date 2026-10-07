"""The in-game menu's Chinese texts (gpu/shim/bbport_overlay_text.h): every Russian string of the
menu and of the effect labels has one, with the same printf conversions and full-width
punctuation next to Chinese characters."""
from paths import ROOT
import re
import unittest

LITERAL = r'"(?:[^"\\]|\\.)*"'
LITERALS = LITERAL + r'(?:\s*' + LITERAL + r')*'
CYRILLIC = re.compile('[Ѐ-ӿ]')
CJK = '[㐀-鿿]'
CONVERSION = re.compile(r'%[-+ #0]*\d*(?:\.\d+)?[diouxXfFeEgGcsp%]')
# Shown as they are in either language: the language switch.
UNTRANSLATED = {'Русский', 'Язык / 语言'}


def unescape(body: str) -> str:
    return re.sub(r'\\(["\\n])', lambda m: {'"': '"', '\\': '\\', 'n': '\n'}[m.group(1)], body)


def joined(group: str) -> str:
    """A run of adjacent C++ string literals, joined as the compiler joins them."""
    return ''.join(unescape(part[1:-1]) for part in re.findall(LITERAL, group))


def russian_strings() -> set[str]:
    sources = [ROOT / 'gpu/shim/bbport_overlay.cpp', ROOT / 'gpu/shim/bbport_settings.h']
    found = set()
    for source in sources:
        for group in re.finditer(LITERALS, source.read_text(encoding='utf-8')):
            text = joined(group.group(0))
            if CYRILLIC.search(text) and text not in UNTRANSLATED:
                found.add(text)
    return found


def chinese_texts() -> list[tuple[str, str]]:
    table = (ROOT / 'gpu/shim/bbport_overlay_text.h').read_text(encoding='utf-8')
    pairs = [(joined(ru), joined(zh))
             for ru, zh in re.findall(r'\{\s*(' + LITERALS + r')\s*,\s*(' + LITERALS + r')\s*\}', table)]
    return pairs


class OverlayTextTests(unittest.TestCase):
    def test_every_russian_menu_string_has_a_chinese_text(self) -> None:
        texts = dict(chinese_texts())
        missing = sorted(russian_strings() - texts.keys())
        self.assertEqual(missing, [])

    def test_no_text_for_a_string_the_menu_no_longer_has(self) -> None:
        unused = sorted(dict(chinese_texts()).keys() - russian_strings())
        self.assertEqual(unused, [])

    def test_each_russian_string_has_one_text(self) -> None:
        keys = [ru for ru, _ in chinese_texts()]
        self.assertEqual(len(keys), len(set(keys)))

    def test_chinese_texts_keep_the_printf_conversions(self) -> None:
        for ru, zh in chinese_texts():
            with self.subTest(ru=ru):
                self.assertEqual(CONVERSION.findall(zh), CONVERSION.findall(ru))

    def test_chinese_texts_use_chinese_punctuation(self) -> None:
        for ru, zh in chinese_texts():
            with self.subTest(zh=zh):
                self.assertIsNone(CYRILLIC.search(zh))
                self.assertRegex(zh, CJK)
                self.assertIsNone(re.search(CJK + r'[,;:()!?]|[,;:()!?]' + CJK, zh))


if __name__ == '__main__':
    unittest.main()
