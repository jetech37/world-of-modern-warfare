-- World of Modern Warfare: retired. This file once spawned the named Quartermasters (creatures
-- 92002-92199 at each inn and class trainer area, guids 9200000 + entry - 92000) and sold the Field
-- Quartermaster's Ledger (item 93001) from them. Kits are now edited at every trainer and
-- innkeeper ("Edit your kits"), and the ledger became the kit window's field editor (once an hour,
-- fusion/Kits.h); 14_kits_at_trainers.sql deletes the old rows. The ledger's item_template row is
-- kept there so copies already in bags still work (they open the field editor). Kept, empty, so
-- the numbering and an apply-everything run stay as they were.
DO 0;
