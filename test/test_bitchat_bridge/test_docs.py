"""Documentation checks: every relative link in the bridge documents resolves."""

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LINK = re.compile(r"\]\(([^)\s]+)\)")


class DocumentationTests(unittest.TestCase):
    def test_relative_links_resolve(self):
        documents = [ROOT / "README.md", ROOT / "README.en.md"]
        documents += sorted((ROOT / "docs/bridge").rglob("*.md"))
        broken = []
        for document in documents:
            for target in LINK.findall(document.read_text(encoding="utf-8")):
                if re.match(r"^[a-z]+:", target) or target.startswith("#"):
                    continue
                if not (document.parent / target.partition("#")[0]).exists():
                    broken.append(f"{document.relative_to(ROOT)} -> {target}")
        self.assertEqual(broken, [])

    def test_index_lists_every_current_document(self):
        index = (ROOT / "docs/bridge/README.md").read_text(encoding="utf-8")
        for document in (ROOT / "docs/bridge").glob("*.md"):
            if document.name != "README.md":
                self.assertIn(f"({document.name})", index, document.name)


if __name__ == "__main__":
    unittest.main()
