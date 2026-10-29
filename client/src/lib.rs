//! Modern Warfare 2 in Classic WoW: a first-person shooter layer on top of the benilla 1.12.1
//! client.
//!
//! Everything here is client side and talks to the server only the way a player can: it targets
//! what is under the crosshair and casts the class's own spells. The server stays the authority
//! on hits, damage and kills; this layer reads the results back off the object mirror and turns
//! them into hitmarkers, a kill feed and killstreaks.
//!
//! - [`input`]: takes the mouse buttons and the MW2 keys away from WoW while the gun is up.
//! - [`keybinds`]: every layer key as a command of the stock Key Bindings window.
//! - [`action_systems`]: Modern, Classic and Utility, each with its own action bar.
//! - [`mode`]: the layer's state, first person and the permanent mouselook.
//! - [`aim`]: the unit under the crosshair, with walls in the way respected.
//! - [`weapon`]: fire, aim down sights, reload, lethal and knife.
//! - [`combat`]: hits, kills and damage taken, read from unit health.
//! - [`streaks`]: killstreak rewards and the kill feed (no points).
//! - [`companion`]: the battle companion out: its pet bar keys and health ring.
//! - [`companion_ui`]: the Companions window: the companions carried and the Bestiary.
//! - [`companion_bar`]: the Companion action system's bar (C): the companion's pet bar on the
//!   main bar, and the ride's own name and icon on the buff.
//! - [`handling`]: what the character's stats do with the gun (the server's stat table).
//! - [`downed`]: down instead of dead: the crawl, the bleed-out, Last Stand and revival kits.
//! - [`prestige`]: the Prestige tab, field gear, and the match messages.
//! - [`hud`]: the crosshair, hitmarkers, radar, kill feed and the rest of the overlay.
//! - [`damage_dir`]: red crescents around the crosshair toward whoever hurt us.
//! - [`wow_ui`]: the layer's windows in the stock dialog look, movable (where they were put is
//!   kept between sessions; a reset button puts them back), kept on screen.
//! - [`mw2`]: MW2's own first-person gun and arms, read from the player's MW2 install.
//! - [`ragdoll`]: death ragdolls, and the throw a blast gives a body.
//! - [`crash_report`]: crash reports for players' games, which the launcher sends.
//! - [`guide`]: the new-player guide, at a character's first login and on `/help`.
//! - [`bounty`]: the bounty boards' window: the day's bounties for the zone, taken and turned in.
//! - [`minstrel`]: Minstrelsy, music as a profession: the instruments on Utility's bar, the note
//!   highway over it played on the slot keys, the songs' buffs, tips and the Minstrelsy window.
//! - [`lore_streaks`]: the lore killstreaks the caller handles: the Goblin Rocket's flight and
//!   the turret's placement.

use benilla_world::schedule::WorldStage;
use bevy::prelude::*;

pub mod ability_tips;
pub mod achievements;
pub mod action_systems;
pub mod aim;
pub mod arsenal;
pub mod blast;
pub mod body_pose;
pub mod body_state;
pub mod bounty;
pub mod companion_battle;
pub mod build;
pub mod cac;
pub mod cac_ui;
pub mod capture_keep;
pub mod cast_bar;
pub mod charge_sound;
pub mod catalog;
pub mod classes;
pub mod combat;
pub mod companion;
pub mod companion_bar;
pub mod companion_icons;
pub mod companion_ui;
pub mod config;
pub mod crash_report;
pub mod damage_dir;
pub mod downed;
pub mod equipment;
pub mod feedback;
pub mod form_limbs;
pub mod forms;
pub mod fp_body;
pub mod fusion;
pub mod grenade;
pub mod guide;
pub mod hand_anim;
pub mod hand_shape;
pub mod hand_slots;
pub mod handling;
pub mod hands;
pub mod hd_pack;
pub mod heartbeat;
pub mod hud;
pub mod input;
pub mod keybinds;
pub mod killcam;
pub mod lore_streaks;
pub mod lua;
pub mod main_bar;
pub mod melee;
pub mod melee_pose;
pub mod micro_menu;
pub mod minstrel;
pub mod mode;
pub mod movement;
pub mod mw2;
pub mod npc_guns;
pub mod panels;
pub mod professions;
pub mod pickups;
pub mod prestige;
pub mod probe;
pub mod ragdoll;
pub mod riding;
pub mod mount_flight;
pub mod rounds;
pub mod scope;
pub mod ships;
pub mod shot_target;
pub mod hand_anims;
pub mod spell_fx;
pub mod spellcast;
pub mod spread;
pub mod sprint;
pub mod stance_view;
pub mod streaks;
pub mod thermal;
pub mod thirdperson;
pub mod tools;
pub mod tracer;
pub mod use_key;
pub mod weapon;
pub mod world_map;
pub mod wotlk;
pub mod wow_ui;

/// The layer's per-frame order in `Update`, after benilla has seated the camera and read input
/// ([`WorldStage::Input`]) and before terrain streaming.
#[derive(SystemSet, Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum Mw2Set {
    Mode,
    Aim,
    Weapon,
    Combat,
    Streaks,
    Hud,
}

/// The whole MW2 layer as one plugin, for [`benilla_app::run_with`].
pub struct Mw2Plugin;

impl Plugin for Mw2Plugin {
    fn build(&self, app: &mut App) {
        // The layer ships benilla's Modern atmosphere (sun shadows, light shafts, lamplight, grass
        // wind) as its default; benilla's own default stays the 1.12 look, and the options window's
        // Graphics > Atmosphere row takes a player back to Classic.
        let layer = app
            .world_mut()
            .remove_resource::<benilla_app::LayerDefaults>()
            .unwrap_or_default()
            .with_atmosphere(benilla_world::atmosphere::AtmosphereSettings::MODERN);
        app.insert_resource(layer);
        app.configure_sets(
            Update,
            (
                Mw2Set::Mode,
                Mw2Set::Aim,
                Mw2Set::Weapon,
                Mw2Set::Combat,
                Mw2Set::Streaks,
                Mw2Set::Hud,
            )
                .chain()
                .after(WorldStage::Input)
                .before(WorldStage::Stream),
        )
        .add_plugins((
            catalog::CatalogPlugin,
            wow_ui::WowUiPlugin,
            cac::CacPlugin,
            cac_ui::CacUiPlugin,
            achievements::AchievementsPlugin,
            hands::HandsPlugin,
            thirdperson::ThirdPersonPlugin,
            input::InputPlugin,
            mode::ModePlugin,
            aim::AimPlugin,
            weapon::WeaponPlugin,
            fusion::FusionPlugin,
            stance_view::StanceViewPlugin,
            movement::MovementPlugin,
            riding::RidingPlugin,
        ))
        .add_plugins(mount_flight::MountFlightPlugin)
        .add_plugins((
            combat::CombatPlugin,
            streaks::StreakPlugin,
            hud::HudPlugin,
            mw2::Mw2AssetsPlugin,
            probe::ProbePlugin,
            scope::ScopePlugin,
            thermal::ThermalPlugin,
            tracer::TracerPlugin,
            heartbeat::HeartbeatPlugin,
            equipment::EquipmentPlugin,
            build::BuildPlugin,
            killcam::KillcamPlugin,
            blast::BlastPlugin,
            melee::MeleePlugin,
            npc_guns::NpcGunsPlugin,
        ))
        .add_plugins((
            panels::PanelsPlugin,
            micro_menu::MicroMenuPlugin,
            main_bar::MainBarPlugin,
            world_map::WorldMapPlugin,
            companion::CompanionPlugin,
            companion_bar::CompanionBarPlugin,
            handling::HandlingPlugin,
            pickups::PickupsPlugin,
            rounds::RoundsPlugin,
            downed::DownedPlugin,
            prestige::PrestigePlugin,
            classes::ClassesPlugin,
            spellcast::SpellcastPlugin,
            cast_bar::CastBarPlugin,
        ))
        .add_plugins((
            use_key::UseKeyPlugin,
            tools::ToolsPlugin,
            fp_body::FpBodyPlugin,
            forms::FormsPlugin,
            form_limbs::FormLimbsPlugin,
            damage_dir::DamageDirPlugin,
            ragdoll::RagdollPlugin,
            lore_streaks::LoreStreaksPlugin,
            crash_report::CrashReportPlugin,
            feedback::FeedbackPlugin,
            guide::GuidePlugin,
            keybinds::KeybindsPlugin,
            action_systems::ActionSystemsPlugin,
            spell_fx::SpellFxPlugin,
            charge_sound::ChargeSoundPlugin,
        ))
        .add_plugins((
            body_pose::BodyPosePlugin,
            body_state::BodyStatePlugin,
            bounty::BountyPlugin,
            companion_battle::CompanionBattlePlugin,
            minstrel::MinstrelPlugin,
            professions::ProfessionsPlugin,
            ability_tips::AbilityTipsPlugin,
            shot_target::ShotTargetPlugin,
            ships::ShipsPlugin,
        ));
        info!("benilla-mw2: Modern Warfare 2 layer loaded");
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn every_system_initialises() {
        // Bevy refuses, at start, a system whose queries overlap; initialise them all here.
        // Outside `resource_scope`: initialising may insert a `Schedules` of its own.
        let mut app = App::new();
        app.add_plugins(Mw2Plugin);
        let world = app.world_mut();
        let mut schedules = world.remove_resource::<Schedules>().expect("schedules");
        for (label, schedule) in schedules.iter_mut() {
            schedule
                .initialize(world)
                .unwrap_or_else(|e| panic!("{label:?}: {e}"));
            // Bevy's fixed-loop runner leaves an empty stand-in while the real one is out.
            if let Some(added) = world.remove_resource::<Schedules>() {
                let names: Vec<_> = added.iter().map(|(l, _)| format!("{l:?}")).collect();
                assert!(names.is_empty(), "{label:?} added schedules {names:?}");
            }
        }
        world.insert_resource(schedules);
    }
}
