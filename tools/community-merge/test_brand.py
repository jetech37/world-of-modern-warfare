"""Tests for brand.py, on the made-up brand of src/brand.rs's tests (the real words are private):
    python -I tools/community-merge/test_brand.py"""

import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import brand  # noqa: E402

# The same rules as `brand::tests::RULES`.
RULES = "\n".join([
    "# test rules",
    "banned\tmurky wow",
    "banned\tmkwow",
    "prose\tmurky wow\tAzeroth",
    "prose\tmurky-wow\tAzeroth",
    "prose\tSumpf-WoW\tAzeroth",
    "prose\t泥鱼魔兽\t艾泽拉斯",
    "addr\tmurky-wow",
    "addr\tmurkywow",
    "word\tmkwow\tAzeroth",
    "same\twombatix\trivermaw",
    "same\tmkwow\twomwx",
]) + "\n"

# (input, scrubbed): the cases of the Rust tests, so both sides rewrite alike.
CASES = [
    ("Big Murky WoW Bear", "Big Azeroth Bear"),
    ("a proud member of the murky wow community", "a proud member of the Azeroth community"),
    ("Welcome to MkWoW!", "Welcome to Azeroth!"),
    ("MurkyWoW", "Azeroth"),
    ("Murky-WoW rules", "Azeroth rules"),
    ("der Murky-WoW-Gemeinschaft", "der Azeroth-Gemeinschaft"),
    ("Großer Sumpf-WoW-Bär", "Großer Azeroth-Bär"),
    ("他是泥鱼魔兽社区", "他是艾泽拉斯社区"),
    ("See https://forum.murky-wow.example/viewtopic.php?t=1 for more.", "See for more."),
    ("Visit murky-wow.example.", "Visit."),
    ("It's 'murky-wow.example', ok", "It's '', ok"),
    ("(1,'He is one of us, see murky-wow.example.',1)", "(1,'He is one of us, see.',1)"),
    ("Sea Turtle Shell", "Sea Turtle Shell"),
    ("Murky Waters", "Murky Waters"),
    ("MkWoWx", "MkWoWx"),
    ("Wombatix Pup", "Rivermaw Pup"),
    ("INV_Rifle_MkWoW_02_Gray", "INV_Rifle_WoMwX_02_Gray"),
    (r"Interface\Icons\INV_wombatix_albino.blp", r"Interface\Icons\INV_rivermaw_albino.blp"),
]


class ScrubTest(unittest.TestCase):
    def setUp(self):
        self.r = brand.Rules(RULES)

    def test_cases(self):
        for s, want in CASES:
            self.assertEqual(self.r.scrub_text(s), want, s)

    def test_text_and_file_rules_agree_on_paths(self):
        for p in [r"Interface\Icons\INV_Wombatix_Gold.blp", r"Item\MkWoW\Rifle_MkWoW.m2"]:
            self.assertEqual(self.r.scrub_text(p), self.r.scrub_same_length(p))

    def test_rule_patterns_are_banned_and_rules_are_checked(self):
        self.assertIsNotNone(self.r.find_banned("x Sumpf-WoW y"))
        self.assertIsNone(self.r.find_banned("Sea Turtle Shell"))
        with self.assertRaises(ValueError):
            brand.Rules("same\tab\tabc\nbanned\tx\n")
        with self.assertRaises(ValueError):
            brand.Rules("# nothing\n")

    def test_a_file_is_scrubbed_in_place(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "x.sql")
            with open(p, "w", encoding="utf-8", newline="\n") as f:
                f.write("INSERT INTO t VALUES ('Murky WoW');\n")
            self.assertIsNone(self.r.scrub_file(p))
            with open(p, encoding="utf-8") as f:
                self.assertEqual(f.read(), "INSERT INTO t VALUES ('Azeroth');\n")


if __name__ == "__main__":
    unittest.main()
