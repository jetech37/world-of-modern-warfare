use super::*;

/// A humanoid in benilla's model space (forward −Z, up +Y, right +X), at rest: root, pelvis,
/// SpineLow, chest, neck, head, helm point; each arm clavicle, upper arm, forearm, hand; the
/// waist, and each leg thigh, shin, foot and a knee point.
fn humanoid() -> (Vec<i16>, Vec<Transform>) {
    let bones: [(i16, [f32; 3]); 24] = [
        (-1, [0.0, 0.0, 0.0]),    // 0 root
        (0, [0.0, 1.0, 0.0]),     // 1 pelvis
        (1, [0.0, 1.1, 0.0]),     // 2 SpineLow
        (2, [0.0, 1.4, 0.0]),     // 3 chest
        (3, [0.0, 1.6, 0.0]),     // 4 neck
        (4, [0.0, 1.7, 0.0]),     // 5 head
        (5, [0.0, 1.9, 0.0]),     // 6 helm
        (3, [0.1, 1.55, 0.0]),    // 7 clavicle R
        (7, [0.25, 1.55, 0.0]),   // 8 upper arm R
        (8, [0.25, 1.25, 0.0]),   // 9 forearm R
        (9, [0.25, 0.95, 0.0]),   // 10 hand R
        (3, [-0.1, 1.55, 0.0]),   // 11 clavicle L
        (11, [-0.25, 1.55, 0.0]), // 12 upper arm L
        (12, [-0.25, 1.25, 0.0]), // 13 forearm L
        (13, [-0.25, 0.95, 0.0]), // 14 hand L
        (1, [0.0, 0.95, 0.0]),    // 15 waist
        (15, [0.12, 0.95, 0.0]),  // 16 thigh R
        (16, [0.12, 0.5, 0.0]),   // 17 shin R
        (17, [0.12, 0.05, 0.0]),  // 18 foot R
        (17, [0.12, 0.5, 0.0]),   // 19 knee point R
        (15, [-0.12, 0.95, 0.0]), // 20 thigh L
        (20, [-0.12, 0.5, 0.0]),  // 21 shin L
        (21, [-0.12, 0.05, 0.0]), // 22 foot L
        (21, [-0.12, 0.5, 0.0]),  // 23 knee point L
    ];
    let parents: Vec<i16> = bones.iter().map(|b| b.0).collect();
    let locals = bones
        .iter()
        .map(|&(p, at)| {
            let parent = usize::try_from(p).map_or(Vec3::ZERO, |p| Vec3::from(bones[p].1));
            Transform::from_translation(Vec3::from(at) - parent)
        })
        .collect();
    (parents, locals)
}

fn bound(parents: &[i16], locals: &[Transform]) -> RigBones {
    bind_bones(
        parents,
        [(10, Vec3::ZERO), (14, Vec3::ZERO)],
        Some([19, 23]),
        Some(6),
        |b| model_point(locals, parents, b, Vec3::ZERO),
    )
    .expect("a humanoid binds")
}

/// The eased inputs settled on a target.
fn settled(t: &PoseTarget) -> Smooth {
    let mut s = Smooth::default();
    for _ in 0..200 {
        s.step(t, 0.05);
    }
    s
}

fn posed(t: &PoseTarget, now: f32) -> (Vec<i16>, Vec<Transform>, RigBones) {
    let (parents, mut locals) = humanoid();
    let bones = bound(&parents, &locals);
    let o = offsets(t, &settled(t), now);
    apply(&mut locals, &parents, &bones, &o);
    (parents, locals, bones)
}

fn at(parents: &[i16], locals: &[Transform], b: u16) -> Vec3 {
    model_point(locals, parents, b, Vec3::ZERO)
}

#[test]
fn the_bones_are_found_from_the_hands_knees_and_helm() {
    let (parents, locals) = humanoid();
    let b = bound(&parents, &locals);
    assert_eq!(b.chest, 3);
    assert_eq!(b.pelvis, 1);
    assert_eq!(b.neck, Some(4));
    assert_eq!(b.upper_arm, [8, 12]);
    assert_eq!(b.legs, Some([[16, 17, 18], [20, 21, 22]]));
    // The strafe twist's bones (SpineLow 2, Head 5) are never written.
    assert!(b.touched().all(|x| x != 2 && x != 5));
    // No knees: no legs, the pelvis the chest's grandparent.
    let b = bind_bones(
        &parents,
        [(10, Vec3::ZERO), (14, Vec3::ZERO)],
        None,
        None,
        |_| Vec3::ZERO,
    )
    .unwrap();
    assert_eq!((b.pelvis, b.legs, b.neck), (1, None, None));
    // No knee points but a real pose: the legs are found from the lowest bone on each side.
    let b = bind_bones(
        &parents,
        [(10, Vec3::ZERO), (14, Vec3::ZERO)],
        None,
        Some(6),
        |b| model_point(&locals, &parents, b, Vec3::ZERO),
    )
    .unwrap();
    assert_eq!(b.legs, Some([[16, 17, 18], [20, 21, 22]]));
    assert_eq!(b.pelvis, 1);
}

#[test]
fn the_upper_body_follows_the_aim_up_and_down() {
    let neck_z = |pitch: f32| {
        let t = PoseTarget {
            pitch,
            gun_up: true,
            ..default()
        };
        let (p, l, _) = posed(&t, 0.0);
        at(&p, &l, 4).z
    };
    let level = neck_z(0.0);
    assert!(level.abs() < 1e-4);
    // Looking up leans the chest back (+Z), looking down forward (−Z).
    assert!(neck_z(0.6) > level + 0.05, "{}", neck_z(0.6));
    assert!(neck_z(-0.6) < level - 0.05);
}

#[test]
fn the_gun_takes_more_of_the_aim_in_the_chest_than_bare_hands() {
    let s_gun = settled(&PoseTarget {
        pitch: 0.5,
        gun_up: true,
        ..default()
    });
    let s_bare = settled(&PoseTarget {
        pitch: 0.5,
        ..default()
    });
    let gun = offsets(&default(), &s_gun, 0.0);
    let bare = offsets(&default(), &s_bare, 0.0);
    assert!(gun.chest_pitch > bare.chest_pitch);
    assert!((gun.chest_pitch - 0.4).abs() < 0.01, "{}", gun.chest_pitch);
}

#[test]
fn a_spell_arm_points_along_the_aim() {
    for (hand, cast) in [(0usize, [true, false]), (1, [false, true])] {
        let t = PoseTarget {
            pitch: 0.3,
            cast,
            ..default()
        };
        let (p, l, b) = posed(&t, 0.0);
        let shoulder = at(&p, &l, b.upper_arm[hand]);
        let hand_at = at(&p, &l, b.hand[hand].0);
        let d = (hand_at - shoulder).normalize();
        // 90% of the way from hanging to the aim.
        assert!(d.dot(aim_dir(0.3)) > 0.95, "hand {hand}: {d}");
        // The other arm still hangs.
        let other = 1 - hand;
        let od = (at(&p, &l, b.hand[other].0) - at(&p, &l, b.upper_arm[other])).normalize();
        assert!(od.y < -0.9);
    }
}

#[test]
fn an_arm_layer_at_full_weight_points_the_hand_exactly() {
    let (parents, mut locals) = humanoid();
    let bones = bound(&parents, &locals);
    let want = Vec3::new(0.3, 0.2, -1.0).normalize();
    let mut o = Offsets::default();
    o.arms[0] = ArmLayer {
        dir: want,
        weight: 1.0,
        raise: 0.0,
    };
    // With the chest turned too: the arm aims in model space whatever its parents do.
    o.chest_pitch = 0.4;
    o.chest_yaw = 0.3;
    apply(&mut locals, &parents, &bones, &o);
    let d = (at(&parents, &locals, 10) - at(&parents, &locals, 8)).normalize();
    assert!(d.dot(want) > 0.9999, "{d}");
}

#[test]
fn a_crouch_bends_the_knees_and_lowers_the_hips_with_the_feet_on_the_ground() {
    let stand = posed(&default(), 0.0);
    let t = PoseTarget {
        crouch: true,
        ..default()
    };
    let (p, l, _) = posed(&t, 0.0);
    let pelvis_drop = at(&stand.0, &stand.1, 1).y - at(&p, &l, 1).y;
    assert!(pelvis_drop > 0.2, "{pelvis_drop}");
    for (foot, knee) in [(18u16, 17u16), (22, 21)] {
        let f = at(&p, &l, foot);
        // The foot stays on the ground, under the body.
        assert!(f.y.abs() < 0.08, "foot {foot} at {f}");
        assert!(f.z.abs() < 0.12, "foot {foot} at {f}");
        // The knee comes forward.
        assert!(at(&p, &l, knee).z < -0.2);
    }
}

#[test]
fn prone_lies_the_body_face_down_on_the_ground() {
    let t = PoseTarget {
        prone: true,
        ..default()
    };
    let (p, l, _) = posed(&t, 0.0);
    let pelvis = at(&p, &l, 1);
    let chest = at(&p, &l, 3);
    let foot = at(&p, &l, 18);
    assert!(pelvis.y < 0.35, "pelvis {pelvis}");
    // The body lies along the ground: the chest ahead of the hips, the feet behind.
    assert!(chest.z < pelvis.z - 0.2, "chest {chest}");
    assert!(foot.z > pelvis.z + 0.5, "foot {foot}");
    assert!(foot.y < 0.45, "foot {foot}");
}

#[test]
fn a_shot_kicks_and_settles() {
    assert_eq!(recoil_curve(-0.1), 0.0);
    assert!(recoil_curve(0.04) > 0.99);
    assert!(recoil_curve(0.1) < recoil_curve(0.04));
    assert!(recoil_curve(0.5) < 0.05);
    assert_eq!(recoil_curve(1.0), 0.0);
    let t = PoseTarget {
        fired_at: Some(1.0),
        ..default()
    };
    let s = settled(&t);
    assert!(offsets(&t, &s, 1.04).chest_pitch > 0.05);
    assert!(offsets(&t, &s, 2.0).chest_pitch.abs() < 1e-6);
}

#[test]
fn a_reload_brings_the_left_hand_down_and_back() {
    let t = PoseTarget {
        reload: Some((10.0, 2.0)),
        ..default()
    };
    let s = settled(&t);
    assert_eq!(offsets(&t, &s, 10.0).arms[1].weight, 0.0);
    let mid = offsets(&t, &s, 11.0).arms[1];
    assert!(mid.weight > 0.8 && mid.dir.y < -0.5);
    assert!(offsets(&t, &s, 12.01).arms[1].weight.abs() < 1e-3);
}

#[test]
fn a_throw_winds_up_releases_and_returns_and_a_cooked_one_holds() {
    assert_eq!(throw_phases(0.0, false), (0.0, 0.0));
    assert_eq!(throw_phases(0.25, false).0, 1.0);
    let (w, r) = throw_phases(0.35, false);
    assert!(w < 1.0 && r > 0.0);
    assert_eq!(throw_phases(2.0, false), (0.0, 0.0));
    assert_eq!(throw_phases(5.0, true), (1.0, 0.0));
    // Wound up the arm goes back over the shoulder; released, forward.
    let t = PoseTarget {
        throw_at: Some(0.0),
        ..default()
    };
    let s = settled(&t);
    let wind = offsets(&t, &s, 0.25).arms[0];
    assert!(wind.dir.z > 0.3 && wind.dir.y > 0.3 && wind.weight > 0.99);
    let release = offsets(&t, &s, 0.45).arms[0];
    assert!(release.dir.z < -0.5);
}

#[test]
fn a_sprint_carries_the_gun_low() {
    let t = PoseTarget {
        gun_up: true,
        sprint: true,
        ..default()
    };
    let o = offsets(&t, &settled(&t), 0.0);
    assert!(o.arms[0].raise < -0.4 && o.arms[1].raise < -0.4);
    assert!(o.chest_pitch < 0.0);
}

#[test]
fn the_shield_comes_up_across_the_front() {
    let t = PoseTarget {
        block: true,
        ..default()
    };
    let o = offsets(&t, &settled(&t), 0.0);
    assert!(o.arms[1].weight > 0.99);
    // Toward the middle (+X for the left arm) and ahead.
    assert!(o.arms[1].dir.x > 0.2 && o.arms[1].dir.z < -0.5);
}

#[test]
fn each_tool_swings_its_own_way_and_round_trips_its_byte() {
    for tool in ToolKind::ALL {
        assert_eq!(ToolKind::from_wire(tool.wire()), tool);
    }
    assert_eq!(ToolKind::from_wire(200), ToolKind::None);
    assert_eq!(ToolKind::None.swing_anim(), None);
    assert_eq!(ToolKind::Pick.swing_anim(), Some(17));
    assert_eq!(ToolKind::Herb.swing_anim(), Some(50));
    // A pick goes up over the head before it comes down.
    let t = PoseTarget {
        tool: Some((ToolKind::Pick, 0.0)),
        ..default()
    };
    let s = settled(&t);
    let secs = ToolKind::Pick.swing_secs();
    assert!(offsets(&t, &s, 0.4 * secs).arms[0].dir.y > 0.8);
    assert!(offsets(&t, &s, 0.6 * secs).arms[0].dir.y < -0.3);
    // Herbs bend the body down.
    let t = PoseTarget {
        tool: Some((ToolKind::Herb, 0.0)),
        ..default()
    };
    assert!(offsets(&t, &s, 0.5).chest_pitch < -0.4);
}

#[test]
fn the_layer_never_accumulates_on_a_bone_its_clip_did_not_key() {
    let (parents, mut locals) = humanoid();
    let bones = bound(&parents, &locals);
    let mut mem = LayerMemory {
        held: bones
            .touched()
            .map(|bone| Held {
                bone,
                base: Transform::IDENTITY,
                out: Transform::from_scale(Vec3::ZERO),
            })
            .collect(),
        bones: Some(bones.clone()),
        ..default()
    };
    let mut o = Offsets {
        chest_pitch: 0.3,
        legs: 0.5,
        prone: 0.0,
        ..default()
    };
    o.arms[0] = ArmLayer {
        dir: Vec3::NEG_Z,
        weight: 1.0,
        raise: 0.2,
    };
    let mut once = Vec::new();
    for frame in 0..5 {
        // No clip re-keys anything: the locals carry last frame's output.
        mem.restore(&mut locals);
        apply(&mut locals, &parents, &bones, &o);
        mem.remember(&locals);
        if frame == 0 {
            once = locals.clone();
        }
    }
    for (a, b) in once.iter().zip(&locals) {
        assert!(a.rotation.angle_between(b.rotation) < 1e-4);
        assert!(a.translation.distance(b.translation) < 1e-5);
    }
    // A bone the clip does key takes the new key as its base.
    let keyed = Quat::from_rotation_z(0.2);
    locals[3].rotation = keyed;
    mem.restore(&mut locals);
    assert_eq!(locals[3].rotation, keyed);
}

#[test]
fn easing_reaches_its_target_at_any_frame_rate() {
    let t = PoseTarget {
        pitch: 0.8,
        ads: true,
        ..default()
    };
    let mut fast = Smooth::default();
    let mut slow = Smooth::default();
    for _ in 0..120 {
        fast.step(&t, 1.0 / 120.0);
    }
    for _ in 0..30 {
        slow.step(&t, 1.0 / 30.0);
    }
    assert!((fast.pitch - slow.pitch).abs() < 1e-3);
    assert!(fast.ads > 0.99);
}

/// Against a real character, read from the install (read-only): skipped without one.
mod real {
    use super::*;
    use std::path::PathBuf;

    fn install() -> Option<PathBuf> {
        if let Some(d) = std::env::var_os("WOW_DATA").map(PathBuf::from) {
            return d.is_dir().then_some(d);
        }
        PathBuf::from(env!("CARGO_MANIFEST_DIR"))
            .ancestors()
            .map(|a| a.join("dist/AzerothWarfare/game/WoW/Data"))
            .find(|d| d.is_dir())
    }

    fn w2b(p: [f32; 3]) -> Vec3 {
        benilla_assets::coords::wow_to_bevy(p)
    }

    struct Body {
        parents: Vec<i16>,
        rest: Vec<Transform>,
        pivots: Vec<Vec3>,
        points: Vec<(u16, u16, Vec3)>,
        anims: Vec<benilla_formats::ModelAnimation>,
    }

    fn load(path: &str) -> Option<Body> {
        let data = install()?;
        let mut chain = benilla_formats::open_chain(&data).ok()?;
        let bytes = chain.read_file(path).ok()?;
        let skel = benilla_formats::parse_m2_skeleton(&bytes).ok()?;
        let attach = benilla_formats::parse_m2_attachments(&bytes).ok()?;
        let parents: Vec<i16> = skel.bones.iter().map(|b| b.parent).collect();
        let pivots: Vec<Vec3> = skel.bones.iter().map(|b| w2b(b.pivot)).collect();
        let rest = (0..pivots.len())
            .map(|i| {
                let parent = usize::try_from(parents[i]).map_or(Vec3::ZERO, |p| pivots[p]);
                Transform::from_translation(pivots[i] - parent)
            })
            .collect();
        Some(Body {
            points: attach
                .iter()
                .map(|a| (a.id, a.bone, w2b(a.position) - pivots[usize::from(a.bone)]))
                .collect(),
            parents,
            rest,
            pivots,
            anims: benilla_formats::parse_m2_animations(&bytes),
        })
    }

    impl Body {
        fn point(&self, id: u16) -> Option<(u16, Vec3)> {
            self.points
                .iter()
                .find(|p| p.0 == id)
                .map(|&(_, b, o)| (b, o))
        }

        /// The locals `t` seconds into `anim_id`, as benilla's evaluator samples them.
        fn pose(&self, anim_id: u16, t: f32) -> Vec<Transform> {
            let r = Quat::from_mat3(&bevy::math::Mat3::from_cols(
                w2b([1.0, 0.0, 0.0]),
                w2b([0.0, 1.0, 0.0]),
                w2b([0.0, 0.0, 1.0]),
            ));
            let mut locals = self.rest.clone();
            let Some(anim) = self.anims.iter().find(|a| a.anim_id == anim_id) else {
                return locals;
            };
            for bk in &anim.bones {
                let Some(tf) = locals.get_mut(usize::from(bk.bone)) else {
                    continue;
                };
                let rest = tf.translation;
                let tr: Vec<(f32, Vec3)> = bk
                    .translation
                    .iter()
                    .map(|(k, v)| (*k, rest + w2b(*v)))
                    .collect();
                let ro: Vec<(f32, Quat)> = bk
                    .rotation
                    .iter()
                    .map(|(k, q)| {
                        (
                            *k,
                            r * Quat::from_xyzw(q[0], q[1], q[2], q[3]) * r.inverse(),
                        )
                    })
                    .collect();
                if let Some(v) = benilla_assets::PoseTrack::new(&tr).sample(t) {
                    tf.translation = v;
                }
                if let Some(v) = benilla_assets::PoseTrack::new(&ro).sample(t) {
                    tf.rotation = v;
                }
            }
            locals
        }

        fn bones(&self, locals: &[Transform]) -> RigBones {
            let hand = |id| self.point(id).expect("hand point");
            bind_bones(
                &self.parents,
                [hand(ATTACH_HAND_RIGHT), hand(ATTACH_HAND_LEFT)],
                self.point(ATTACH_KNEE_RIGHT)
                    .zip(self.point(ATTACH_KNEE_LEFT))
                    .map(|(r, l)| [r.0, l.0]),
                self.point(ATTACH_HELM).map(|p| p.0),
                |b| model_point(locals, &self.parents, b, Vec3::ZERO),
            )
            .expect("a character binds")
        }
    }

    const MODELS: [&str; 10] = [
        "Character\\Human\\Male\\HumanMale.m2",
        "Character\\Human\\Female\\HumanFemale.m2",
        "Character\\Orc\\Male\\OrcMale.m2",
        "Character\\Dwarf\\Male\\DwarfMale.m2",
        "Character\\NightElf\\Male\\NightElfMale.m2",
        "Character\\Scourge\\Male\\ScourgeMale.m2",
        "Character\\Tauren\\Male\\TaurenMale.m2",
        "Character\\Tauren\\Female\\TaurenFemale.m2",
        "Character\\Gnome\\Male\\GnomeMale.m2",
        "Character\\Troll\\Male\\TrollMale.m2",
    ];

    #[test]
    fn every_race_binds_its_chest_arms_neck_and_legs() {
        let mut seen = 0;
        for m in MODELS {
            let Some(body) = load(m) else {
                eprintln!("skipped {m}: no install");
                continue;
            };
            seen += 1;
            let b = body.bones(&body.rest);
            // The chest sits above the hips, the upper arms out at its sides, the neck above it.
            let y = |x: u16| body.pivots[usize::from(x)].y;
            assert!(y(b.chest) > y(b.pelvis), "{m}");
            assert!(b.neck.is_some_and(|n| y(n) > y(b.chest)), "{m}");
            let x = |x: u16| body.pivots[usize::from(x)].x;
            assert!(
                x(b.upper_arm[0]) > 0.1 && x(b.upper_arm[1]) < -0.1,
                "{m}: {b:?}"
            );
            let legs = b.legs.unwrap_or_else(|| panic!("{m}: no legs"));
            for [thigh, shin, foot] in legs {
                assert!(y(thigh) > y(shin) && y(shin) > y(foot), "{m}: {b:?}");
            }
        }
        eprintln!("{seen} models bound");
        if install().is_some() {
            assert_eq!(seen, MODELS.len(), "every model reads from the install");
        }
    }

    /// Over the rifle's ready stance, the aim lifts and lowers the gun hand, and a cast arm
    /// points where the aim goes.
    #[test]
    fn over_the_rifle_stance_the_aim_moves_the_gun_hand() {
        for m in MODELS {
            let Some(body) = load(m) else {
                continue;
            };
            let base = body.pose(crate::thirdperson::anim::READY_RIFLE, 0.3);
            let bones = body.bones(&base);
            // The gun lies along the hand: the body's forward, as the hand holds it at rest,
            // turns with the aim.
            let (hb, _) = bones.hand[0];
            let held = model_rot(&base, &body.parents, hb).inverse() * Vec3::NEG_Z;
            let gun_y = |pitch: f32| {
                let mut locals = base.clone();
                let t = PoseTarget {
                    pitch,
                    gun_up: true,
                    ..default()
                };
                apply(
                    &mut locals,
                    &body.parents,
                    &bones,
                    &offsets(&t, &settled(&t), 0.0),
                );
                (model_rot(&locals, &body.parents, hb) * held).y
            };
            let (down, level, up) = (gun_y(-0.6), gun_y(0.0), gun_y(0.6));
            // 80% of 0.6 rad each way: sin 0.48 = 0.46.
            assert!(level.abs() < 1e-4, "{m}: {level}");
            assert!(up > 0.4 && down < -0.4, "{m}: {down} {level} {up}");

            let mut locals = body.pose(crate::thirdperson::anim::READY_SPELL_DIRECTED, 0.3);
            let t = PoseTarget {
                pitch: 0.2,
                cast: [true, false],
                ..default()
            };
            apply(
                &mut locals,
                &body.parents,
                &bones,
                &offsets(&t, &settled(&t), 0.0),
            );
            let (hb, ho) = bones.hand[0];
            let d = (model_point(&locals, &body.parents, hb, ho)
                - model_point(&locals, &body.parents, bones.upper_arm[0], Vec3::ZERO))
            .normalize();
            assert!(d.dot(aim_dir(0.2)) > 0.9, "{m}: {d}");
        }
    }

    /// Every race and gender, over its own stand and ready poses: crouched, the lowest foot stays
    /// where it stood (the gnome floated: the pelvis came down by a straight-leg guess while its
    /// short, bent legs lifted the ankles further).
    #[test]
    fn every_race_crouches_with_its_feet_on_the_ground() {
        let mut all = Vec::new();
        for race in ["Human", "Orc", "Dwarf", "NightElf", "Scourge", "Tauren", "Gnome", "Troll"] {
            for sex in ["Male", "Female"] {
                all.push(format!("Character\\{race}\\{sex}\\{race}{sex}.m2"));
            }
        }
        for m in &all {
            let Some(body) = load(m) else {
                continue;
            };
            for anim in [0u16, crate::thirdperson::anim::READY_RIFLE] {
                let base = body.pose(anim, 0.3);
                let bones = body.bones(&base);
                let Some(legs) = bones.legs else {
                    panic!("{m}: no legs");
                };
                let low = |locals: &[Transform]| {
                    legs.iter()
                        .map(|l| model_point(locals, &body.parents, l[2], Vec3::ZERO).y)
                        .fold(f32::MAX, f32::min)
                };
                let stood = low(&base);
                let t = PoseTarget {
                    crouch: true,
                    gun_up: anim != 0,
                    ..default()
                };
                let mut locals = base.clone();
                apply(&mut locals, &body.parents, &bones, &offsets(&t, &settled(&t), 0.0));
                let crouched = low(&locals);
                assert!(
                    (crouched - stood).abs() < 0.02,
                    "{m} anim {anim}: the foot moved {:.3} yd",
                    crouched - stood
                );
                let pelvis =
                    |l: &[Transform]| model_point(l, &body.parents, bones.pelvis, Vec3::ZERO).y;
                assert!(pelvis(&locals) < pelvis(&base) - 0.05, "{m}: the hips come down");
            }
        }
    }
}

#[test]
fn the_support_hand_goes_ahead_of_the_grip_along_the_aim() {
    let g = super::foregrip(Vec3::new(0.2, 1.0, 0.0), Vec3::new(0.0, 0.0, -2.0), 0.4);
    assert!((g - Vec3::new(0.2, 1.0, -0.4 * super::FOREGRIP_SPAN)).length() < 1e-5);
}

#[test]
fn a_raised_gun_lifts_toward_the_shoulder_and_takes_the_support_hand() {
    let t = PoseTarget { gun_up: true, ..Default::default() };
    let s = Smooth { gun: 1.0, ..Default::default() };
    let o = offsets(&t, &s, 0.0);
    assert!(o.arms[0].raise > 0.0 && o.support > 0.99);
    let s = Smooth { gun: 1.0, sprint: 1.0, ..Default::default() };
    assert!(offsets(&t, &s, 0.0).support < 1e-3, "a sprint carries it one-handed");
}

#[test]
fn a_pistol_is_cupped_and_a_long_gun_reaches_the_foregrip() {
    let s = Smooth { gun: 1.0, ..Default::default() };
    let rifle = offsets(&PoseTarget { gun_up: true, ..Default::default() }, &s, 0.0);
    let pistol = offsets(&PoseTarget { gun_up: true, pistol: true, ..Default::default() }, &s, 0.0);
    assert!(pistol.support_span < 0.3 && rifle.support_span > 0.8);
    assert!(rifle.elbow[0] > 0.2, "the stock comes toward the shoulder");
    let known = offsets(&PoseTarget { gun_up: true, foregrip: Some(0.4), ..Default::default() }, &s, 0.0);
    assert_eq!(known.support_reach, Some(0.4), "the gun's own foregrip");
    let cupped = offsets(&PoseTarget { gun_up: true, pistol: true, foregrip: Some(0.4), ..Default::default() }, &s, 0.0);
    assert_eq!(cupped.support_reach, None, "a pistol is cupped, not reached along");
    assert_eq!(rifle.elbow[1], 0.0, "the support arm is aimed, not bent");
}
