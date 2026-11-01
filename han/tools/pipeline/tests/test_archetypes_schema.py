"""Данные архетипов прохожих (data/people/archetypes/*.json) соответствуют схеме contracts/schemas/npc_archetype.schema.json.
Без пакета jsonschema тест пропускается (в CI он ставится вместе с проверками данных)."""
import json
import unittest
from pathlib import Path

try:
    import jsonschema
except ImportError:  # pragma: no cover
    jsonschema = None

ROOT = Path(__file__).resolve().parents[4]
SCHEMA = ROOT / "contracts" / "schemas" / "npc_archetype.schema.json"
DATA = ROOT / "data" / "people" / "archetypes"


@unittest.skipIf(jsonschema is None, "нет пакета jsonschema")
class ArchetypeSchemaTests(unittest.TestCase):
    def test_schema_is_valid(self):
        jsonschema.Draft202012Validator.check_schema(json.loads(SCHEMA.read_text(encoding="utf-8")))

    def test_every_archetype_matches_schema(self):
        validator = jsonschema.Draft202012Validator(json.loads(SCHEMA.read_text(encoding="utf-8")))
        files = sorted(DATA.glob("*.json"))
        self.assertGreaterEqual(len(files), 9)
        for f in files:
            errors = [e.message for e in validator.iter_errors(json.loads(f.read_text(encoding="utf-8")))]
            self.assertEqual(errors, [], f.name)

    def test_ids_match_file_names_and_numeric_ids_are_unique(self):
        seen = set()
        for f in sorted(DATA.glob("*.json")):
            doc = json.loads(f.read_text(encoding="utf-8"))
            self.assertEqual(doc["id"], f.stem)
            self.assertNotIn(doc["numeric_id"], seen)
            seen.add(doc["numeric_id"])
            self.assertGreaterEqual(doc["age_range"][0], 18, f"{f.name}: в игре только взрослые")


if __name__ == "__main__":
    unittest.main()
