//! `cargo run --release --example prop_bounds -- <WoW Data dir>`: the bounding boxes of the WoW
//! props building offers (crate::build::props), printed as the Rust and C++ tables both sides
//! use for the props' collision. Run again when the list changes; nothing of WoW's is copied,
//! only each model's size.

use std::path::Path;

use benilla_formats::{Chain, parse_m2_render_submeshes};

/// (name, model path) of every prop, in id order from 100.
const PROPS: &[(&str, &str)] = &[
    (
        "Campfire",
        "World\\Azeroth\\Elwynn\\PassiveDoodads\\Campfire\\ElwynnCampfire.m2",
    ),
    (
        "Bed",
        "World\\Azeroth\\Westfall\\PassiveDoodads\\Furniture\\westfallbed01.m2",
    ),
    (
        "Barrel",
        "World\\Azeroth\\Westfall\\PassiveDoodads\\Barrel\\WestFallBarrel01.m2",
    ),
    (
        "Crate",
        "World\\Azeroth\\Westfall\\PassiveDoodads\\Crate\\WestFallCrate.m2",
    ),
    (
        "Big crate",
        "World\\Azeroth\\Karazahn\\PassiveDoodads\\Crates\\KarazahnCrate01.m2",
    ),
    (
        "Grain sack",
        "World\\Azeroth\\Karazahn\\PassiveDoodads\\GrainSacks\\KN_GrainSack01.m2",
    ),
    (
        "Haystack",
        "World\\Azeroth\\Elwynn\\PassiveDoodads\\Haystacks\\haystack01.m2",
    ),
    (
        "Hay wagon",
        "World\\Azeroth\\Westfall\\PassiveDoodads\\HayWagon\\WestfallHayWagon.m2",
    ),
    (
        "Mine cart",
        "World\\Azeroth\\Elwynn\\PassiveDoodads\\MineCart\\ElwynnMineCart.m2",
    ),
    (
        "Lamp post",
        "World\\Azeroth\\Elwynn\\PassiveDoodads\\LampPost\\LampPost.m2",
    ),
    (
        "Anvil",
        "World\\Azeroth\\Elwynn\\PassiveDoodads\\Anvil\\Anvil.m2",
    ),
    (
        "Round table",
        "World\\Azeroth\\Elwynn\\PassiveDoodads\\Tables\\RoundTable\\RoundTable.m2",
    ),
    (
        "Wood fence",
        "World\\Azeroth\\Elwynn\\PassiveDoodads\\ElwynnFences\\ElwynnWoodFence01.m2",
    ),
    (
        "Fence post",
        "World\\Azeroth\\Elwynn\\PassiveDoodads\\ElwynnFences\\ElwynnWoodPost01.m2",
    ),
    (
        "Stone fence",
        "World\\Azeroth\\Elwynn\\PassiveDoodads\\ElwynnFences\\ElwynnStoneFence.m2",
    ),
    (
        "Stone fence post",
        "World\\Azeroth\\Elwynn\\PassiveDoodads\\ElwynnFences\\ElwynnStoneFencePost.m2",
    ),
    (
        "Spike wall",
        "World\\Azeroth\\RedRidge\\PassiveDoodads\\spikewall\\BattleGladeWoodSpikesLongRow.m2",
    ),
    (
        "Banner",
        "World\\Azeroth\\Elwynn\\PassiveDoodads\\BattleGladeBanner1\\BattleGladeBanner1.m2",
    ),
    (
        "Large tent",
        "World\\Generic\\Buildings\\HumanTentLarge\\HumanTentLarge.m2",
    ),
    (
        "Tent",
        "World\\Generic\\Buildings\\HumanTentMedium\\HumanTentMedium.m2",
    ),
    (
        "Orc tent",
        "World\\Azeroth\\BurningSteppes\\PassiveDoodads\\OrcTents\\OrcTent.m2",
    ),
    (
        "Goblin tent",
        "World\\Generic\\Goblin\\PassiveDoodads\\GoblinTents\\GoblinTent01.m2",
    ),
    (
        "Bonfire",
        "World\\Azeroth\\BurningSteppes\\PassiveDoodads\\Bonfire\\OrcBonFire.m2",
    ),
    (
        "Rock wall",
        "World\\Azeroth\\Duskwood\\PassiveDoodads\\Walls\\DuskWoodRockWall.m2",
    ),
    (
        "Gate",
        "World\\Azeroth\\Duskwood\\PassiveDoodads\\Gates\\DuskWoodGate01.m2",
    ),
    (
        "Water hut",
        "World\\Azeroth\\SwampOSorrow\\PassiveDoodads\\WaterHuts\\WaterHut01.m2",
    ),
    (
        "Well",
        "World\\Azeroth\\Stranglethorn\\PassiveDoodads\\HolySpringWell\\HolySpringWell.m2",
    ),
    (
        "Wall lantern",
        "World\\Azeroth\\Stranglethorn\\PassiveDoodads\\DiamondMineWallLantern\\DiamondMineWallLantern.m2",
    ),
    // Not a prop of the world but the buildable portal (the server's `Build::kPortalProp`);
    // drawn at the client's `PORTAL_SCALE`, so scale what this prints by it.
    (
        "Portal",
        "World\\Generic\\ActiveDoodads\\MagePortals\\StormwindMagePortal01.m2",
    ),
];

/// (name, model path) of every furniture piece (crate::build::pieces FURNITURE, the server's
/// `Furniture.cpp`), in id order from 200; printed as `furniture <id> cx cy cz hx hy hz` lines.
const FURNITURE: &[(&str, &str)] = &[
    ("Wooden chair", "World\\Azeroth\\Westfall\\PassiveDoodads\\WestfallChair\\WestfallChair.m2"),
    ("Cushioned chair", "World\\Generic\\Human\\Passive Doodads\\Chairs\\GeneralChairHighEnd01.m2"),
    ("Inn bench", "World\\Generic\\Human\\Passive Doodads\\Benches\\InnBench.m2"),
    ("Stormwind bench", "World\\Generic\\Human\\Passive Doodads\\Benches\\StormWindBench01.m2"),
    ("Dwarven chair", "World\\Generic\\Dwarf\\Passive Doodads\\Chairs\\IronForgeChair_ornate01.m2"),
    ("Log bench", "World\\Generic\\Tauren\\Passive Doodads\\Chairs\\TaurenLogChair01.m2"),
    ("High-backed chair", "World\\Generic\\Dwarf\\Passive Doodads\\Chairs\\DwarvenHighEndChair.m2"),
    ("Inn bed", "World\\Dungeon\\GoldshireInn\\Innbed\\InnBed.m2"),
    ("Canopy bed", "World\\Dungeon\\GoldshireInn\\InnBedCanopy\\InnBedCanopy.m2"),
    ("Bunk bed", "World\\Generic\\Human\\Passive Doodads\\Beds\\Bunkbed01.m2"),
    ("Dwarven bed", "World\\Generic\\Dwarf\\Passive Doodads\\Beds\\DwarvenBed01.m2"),
    ("Inn table", "World\\Generic\\Human\\Passive Doodads\\Tables\\InnTable.m2"),
    ("Small table", "World\\Generic\\Human\\Passive Doodads\\Tables\\InnTableSmall.m2"),
    ("Long table", "World\\Azeroth\\Westfall\\PassiveDoodads\\WestfallTable\\WestfallTable.m2"),
    ("Writing desk", "World\\Generic\\Human\\Passive Doodads\\Desks\\FancyDesk01.m2"),
    ("Chest", "World\\Generic\\ActiveDoodads\\Chest01\\Chest01.m2"),
    ("Footlocker", "World\\Generic\\Human\\Passive Doodads\\Footlockers\\DuskwoodFootLocker01.m2"),
    ("Wardrobe", "World\\Generic\\Human\\Passive Doodads\\Wardrobe\\DuskwoodWardrobe01.m2"),
    ("Bookshelf", "World\\Generic\\Human\\Passive Doodads\\BookShelves\\DuskwoodBookshelf01.m2"),
    ("Abbey bookshelf", "World\\Generic\\Human\\Passive Doodads\\BookShelves\\AbbeyShelf01.m2"),
    ("Book stack", "World\\Generic\\Human\\Passive Doodads\\BookStacks\\GeneralBookStackShort01.m2"),
    ("Lectern", "World\\Generic\\Human\\Passive Doodads\\Podiums\\DuskwoodPodium01.m2"),
    ("Smith's anvil", "World\\Azeroth\\Elwynn\\PassiveDoodads\\Anvil\\Anvil.m2"),
    ("Forge", "World\\Generic\\Dwarf\\Passive Doodads\\Forgebonfire\\Forgebonfire.m2"),
    ("Cooking fire", "World\\Generic\\Orc\\Passive Doodads\\TableCooker\\OrcTableCooker01Fire.m2"),
    ("Loom", "World\\Generic\\Human\\Passive Doodads\\Loom\\LoomHumanBlue.m2"),
    ("Alchemy table", "World\\Generic\\Undead\\Passive Doodads\\UndeadAlchemyTable\\undead_alchemy_table.m2"),
    ("Mailbox", "World\\Generic\\PassiveDoodads\\PostBoxes\\PostBoxHuman.m2"),
    ("Training dummy", "World\\Generic\\Human\\Passive Doodads\\WoodenDummies\\StormWindWoodenDummy01.m2"),
    ("Trophy plinth", "World\\Generic\\Human\\Passive Doodads\\Altars\\Altar01.m2"),
    ("Bard's stage", "World\\Generic\\DarkIronDwarf\\Passive Doodads\\WoodPlanks\\DarkIronWoodPlanks01.m2"),
    ("Stormwind banner", "World\\Generic\\Human\\Passive Doodads\\Banners\\StormwindLionBanner.m2"),
    ("Horde banner", "World\\Generic\\Orc\\Passive Doodads\\HordeBanners\\HordeBanner01.m2"),
    ("Clan banner", "World\\Generic\\Orc\\Passive Doodads\\Banners\\ClanBanner01.m2"),
    ("Stormwind rug", "World\\Generic\\Human\\Passive Doodads\\Rugs\\StormwindRug01.m2"),
    ("Red rug", "World\\Generic\\Human\\Passive Doodads\\Rugs\\KarazahnRugRed.m2"),
    ("Bearskin rug", "World\\Generic\\Human\\Passive Doodads\\Rugs\\GeneralBearSkinRug01.m2"),
    ("Tauren rug", "World\\Generic\\Tauren\\Passive Doodads\\TaurenRugs\\TaurenRug01.m2"),
    ("Candle", "World\\Generic\\PassiveDoodads\\Lights\\Candle01.m2"),
    ("Candelabra", "World\\Generic\\PassiveDoodads\\Lights\\Candelabra01.m2"),
    ("Brazier", "World\\Generic\\Human\\Passive Doodads\\Braziers\\StormwindBrazier01.m2"),
    ("Wall candelabra", "World\\Generic\\PassiveDoodads\\Lights\\CandelabraTallWall01.m2"),
    ("Hanging lantern", "World\\Generic\\Human\\Passive Doodads\\HangingLanterns\\GeneralHangingLantern01.m2"),
    ("Flowers", "World\\Generic\\Human\\Passive Doodads\\Flowers\\FlowersBunch01.m2"),
    ("Planter", "World\\Generic\\Human\\Passive Doodads\\PlanterBoxes\\StormwindPlanter.m2"),
    ("Beer keg", "World\\Generic\\Human\\Passive Doodads\\BeerKegs\\BeerKeg01.m2"),
    ("Mug", "World\\Generic\\Human\\Passive Doodads\\Mugs\\Mug01.m2"),
    ("Weapon rack", "World\\Generic\\Human\\Passive Doodads\\WeaponRacks\\GeneralWeaponrack01.m2"),
    ("Wall shield", "World\\Generic\\Human\\Passive Doodads\\Weapons&Armor\\WallShield03.m2"),
    ("Mounted boar head", "World\\Generic\\Human\\Passive Doodads\\AnimalHeads\\DuskwoodBoarHead01.m2"),
    ("Stuffed bear", "World\\Generic\\Human\\Passive Doodads\\AnimalHeads\\StuffedBear.m2"),
    ("Painting", "World\\Generic\\Human\\Passive Doodads\\Artwork\\StormwindPainting01.m2"),
    ("Globe", "World\\Generic\\Human\\Passive Doodads\\Globes\\Globe01.m2"),
    ("Armor stand", "World\\Generic\\Human\\Passive Doodads\\Armor\\ArmorStand.m2"),
    ("Potbelly stove", "World\\Generic\\Human\\Passive Doodads\\Stoves\\potbellystove.m2"),
];

/// The bounds of a model's non-billboard geometry: (centre, half sizes).
fn bounds(chain: &Chain, path: &str) -> Option<([f32; 3], [f32; 3])> {
    let bytes = chain.read(path).ok()?;
    let dir = path.rsplit_once('\\').map_or("", |(d, _)| d);
    let subs = parse_m2_render_submeshes(&bytes, dir, &[]).ok()?;
    let (mut lo, mut hi) = ([f32::MAX; 3], [f32::MIN; 3]);
    for s in subs.iter().filter(|s| s.billboard.is_none()) {
        for p in &s.positions {
            for k in 0..3 {
                lo[k] = lo[k].min(p[k]);
                hi[k] = hi[k].max(p[k]);
            }
        }
    }
    (lo[0] <= hi[0]).then(|| {
        let c = [0, 1, 2].map(|k| (lo[k] + hi[k]) / 2.0);
        let h = [0, 1, 2].map(|k| ((hi[k] - lo[k]) / 2.0).max(0.05));
        (c, h)
    })
}

fn main() {
    let data = std::env::args().nth(1).expect("WoW Data dir");
    let chain = Chain::open(Path::new(&data)).expect("open the patch chain");
    let mut rust = String::new();
    let mut cpp = String::new();
    for (i, (name, path)) in PROPS.iter().enumerate() {
        let id = 100 + i;
        let bytes = match chain.read(path) {
            Ok(b) => b,
            Err(e) => {
                eprintln!("{path}: {e}");
                continue;
            }
        };
        let dir = path.rsplit_once('\\').map_or("", |(d, _)| d);
        let subs = parse_m2_render_submeshes(&bytes, dir, &[]).expect("parse");
        let (mut lo, mut hi) = ([f32::MAX; 3], [f32::MIN; 3]);
        for s in subs.iter().filter(|s| s.billboard.is_none()) {
            for p in &s.positions {
                for k in 0..3 {
                    lo[k] = lo[k].min(p[k]);
                    hi[k] = hi[k].max(p[k]);
                }
            }
        }
        if lo[0] > hi[0] {
            eprintln!("{path}: no geometry");
            continue;
        }
        let c: Vec<f32> = (0..3).map(|k| (lo[k] + hi[k]) / 2.0).collect();
        let h: Vec<f32> = (0..3).map(|k| ((hi[k] - lo[k]) / 2.0).max(0.05)).collect();
        rust.push_str(&format!(
            "    Prop {{ id: {id}, name: \"{name}\", model: \"{}\", center: [{:.2}, {:.2}, {:.2}], half: [{:.2}, {:.2}, {:.2}] }},\n",
            path.replace('\\', "\\\\"),
            c[0], c[1], c[2], h[0], h[1], h[2]
        ));
        cpp.push_str(&format!(
            "        {{{id}, \"{name}\", {{{:.2}f, {:.2}f, {:.2}f}}, {{{:.2}f, {:.2}f, {:.2}f}}}},\n",
            c[0], c[1], c[2], h[0], h[1], h[2]
        ));
    }
    println!("// Rust\n{rust}\n// C++\n{cpp}");
    for (i, (name, path)) in FURNITURE.iter().enumerate() {
        match bounds(&chain, path) {
            Some((c, h)) => println!(
                "furniture {} {:.2} {:.2} {:.2} {:.2} {:.2} {:.2} {name}",
                200 + i,
                c[0],
                c[1],
                c[2],
                h[0],
                h[1],
                h[2]
            ),
            None => eprintln!("{path}: no geometry"),
        }
    }
}
