"""Tests for db_import (no database needed):
    python -I tools/community-merge/test_db_import.py"""

import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import db_import as d  # noqa: E402


class IdMapTest(unittest.TestCase):
    def test_free_ids_stay_clashing_and_reserved_ones_move_into_the_range(self):
        m = d.IdMap()
        m.ours["creature"] = {1150, 61000}
        self.assertEqual(m.get("creature", 61072), 61072, "free and outside our bands: kept")
        self.assertEqual(m.get("creature", 61000), 110000, "ours: renumbered")
        self.assertEqual(m.get("creature", 90500), 110001, "our custom band: renumbered")
        self.assertEqual(m.get("creature", 61000), 110000, "stable within a run")
        self.assertEqual(m.get("creature", 0), 0)
        # Items and quests have their own blocks.
        m.ours["item"] = {93001}
        self.assertEqual(m.get("item", 93001), 140000)
        self.assertEqual(m.get("quest", 95000), 160000)

    def test_a_renumbered_id_never_lands_on_one_of_ours_or_a_kept_one(self):
        m = d.IdMap()
        m.ours["gossip_menu"] = {62000, 62001}
        self.assertEqual(m.get("gossip_menu", 5), 5)
        m.ours["gossip_menu"].add(7)
        self.assertEqual(m.get("gossip_menu", 7), 62002)

    def test_an_id_past_our_column_moves_into_the_range(self):
        m = d.IdMap()
        self.assertEqual(m.get("gossip_menu", 41276), 41276)
        self.assertEqual(m.get("gossip_menu", 6230301), 62000, "gossip_menu.entry is a smallint")

    def test_guids_always_move_into_the_block(self):
        m = d.IdMap()
        self.assertEqual(m.get("creature_guid", 2900000), 12000000)
        self.assertEqual(m.get("creature_guid", 5), 12000001)

    def test_the_map_round_trips_and_a_reimport_keeps_every_number(self):
        with tempfile.TemporaryDirectory() as t:
            p = os.path.join(t, "map.tsv")
            m = d.IdMap(p)
            m.ours["creature"] = {61000}
            a = m.get("creature", 61000)
            g = m.get("creature_guid", 77)
            m.save()
            again = d.IdMap(p)
            # Even if our side has since taken the old id or the range moved on, the map wins.
            again.ours["creature"] = {61000, 110000}
            self.assertEqual(again.get("creature", 61000), a)
            self.assertEqual(again.get("creature_guid", 77), g)
            self.assertEqual(again.get("creature", 61001), 61001)
            self.assertEqual(again.get("creature", 90001), 110001, "after every number handed out")


class PlacementTest(unittest.TestCase):
    def test_a_spawn_lands_in_the_chunk_under_it(self):
        # Kalimdor tile 44_47 spans x 17066.67 - 47*533.33 .. +533.33, y 17066.67 - 44*533.33 ...
        x = d.ORIGIN - 47 * 533.3333333 - 1.0  # just inside the tile's north edge
        y = d.ORIGIN - 44 * 533.3333333 - 40.0  # second chunk column
        col, row = d.chunk_of(x, y)
        self.assertEqual((col // 16, row // 16), (44, 47))
        self.assertEqual((col % 16, row % 16), (1, 0))


class ConversionTest(unittest.TestCase):
    STATS = {(1, 30): {"health": 955, "mana": 0, "armor": 1160, "melee_damage": 49.3875},
             (1, 31): {"health": 1006, "mana": 0, "armor": 1194, "melee_damage": 50.6313}}

    def row(self, **kw):
        t = {"entry": 61000, "unit_class": 1, "level_min": 30, "level_max": 31, "health_max": 2012,
             "mana_max": 0, "armor": 1194, "dmg_min": 50.0, "dmg_max": 50.0, "dmg_multiplier": 1,
             "dmg_school": 0, "beast_family": 6, "scale": 0, "unit_flags": 0, "type_flags": 0,
             "regeneration": 3, "inhabit_type": 1, "display_id1": 1039, "display_id2": 0,
             "display_id3": 0, "display_id4": 0}
        t.update(kw)
        return t

    def test_absolute_stats_become_multipliers_of_the_level_base(self):
        r = d.creature_template(self.row(), self.STATS)
        self.assertAlmostEqual(r["health_multiplier"], 2.0, places=3)
        self.assertAlmostEqual(r["armor_multiplier"], 1.0, places=3)
        self.assertAlmostEqual(r["damage_multiplier"], 50.0 / ((49.3875 + 50.6313) / 2), places=3)
        self.assertEqual(r["mana_multiplier"], 1.0, "no mana: neutral")
        self.assertEqual(r["pet_family"], 6)
        self.assertEqual(r["display_probability1"], 1)
        self.assertEqual(r["display_total_probability"], 1)

    def test_class_0_becomes_a_class_our_core_has_stats_for(self):
        self.assertEqual(d.creature_template(self.row(unit_class=0), self.STATS)["unit_class"], 1)
        self.assertEqual(d.creature_template(self.row(unit_class=0, mana_max=900), self.STATS)["unit_class"], 8)
        self.assertEqual(d.creature_template(self.row(unit_class=2), self.STATS)["unit_class"], 2)
        self.assertEqual(d.creature_template(self.row(unit_class=4), self.STATS)["unit_class"], 1, "rogue")
        self.assertEqual(d.creature_template(self.row(unit_class=4, mana_max=50), self.STATS)["unit_class"], 8)

    def test_flags_move_to_static_flags(self):
        r = d.creature_template(self.row(unit_flags=0x8000 | 0x100, type_flags=1, inhabit_type=3, regeneration=0), self.STATS)
        f = r["static_flags1"]
        for bit in (d.SF_CAN_SWIM, d.SF_IMMUNE_TO_PC, d.SF_TAMEABLE, d.SF_AMPHIBIOUS, d.SF_NO_AUTOMATIC_REGEN):
            self.assertTrue(f & bit, hex(bit))
        self.assertFalse(f & d.SF_IMMUNE_TO_NPC)

    def test_values_past_our_columns_are_masked_or_clamped(self):
        self.assertEqual(d.int_range("tinyint", "tinyint(3) unsigned"), (0, 255))
        self.assertEqual(d.int_range("smallint", "smallint(6)"), (-32768, 32767))
        self.assertIsNone(d.int_range("float", "float"))
        self.assertEqual(d.fit("SpecialFlags", 0x101, (0, 255)), 0x01)
        self.assertEqual(d.fit("ZoneOrSort", 40000, (-32768, 32767)), 32767)

    def test_values_are_quoted_for_their_column_type(self):
        self.assertEqual(d.sql_value("It's\n", "varchar"), "'It\\'s\\n'")
        self.assertEqual(d.sql_value("12", "int"), "12")
        self.assertEqual(d.sql_value(None, "int"), "NULL")
        self.assertEqual(d.unescape("a\\tb\\\\c\\n"), "a\tb\\c\n")


if __name__ == "__main__":
    unittest.main()
