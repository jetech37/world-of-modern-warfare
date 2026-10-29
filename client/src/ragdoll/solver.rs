//! The death ragdoll's physics, free of the ECS: a bounded position-based solver (Müller et al.,
//! "Position Based Dynamics", 2006) over one particle per distinct bone position, kept on the
//! ground by a few rays into the world, and the pose written back as bone locals.
//!
//! The ground is sampled with a handful of rays every few steps instead of a shape cast per
//! particle per step.

use bevy::math::{Affine3A, Quat, Vec2, Vec3};
use bevy::transform::components::Transform;

/// The fixed step: 120 Hz, whatever the frame rate.
pub const STEP: f32 = 1.0 / 120.0;
/// WoW's gravity, yards a second squared.
pub const GRAVITY: f32 = 19.29;
/// Velocity kept each step (air drag, and what keeps the chain from ringing).
const DAMPING: f32 = 0.994;
/// Constraint passes a step.
const ITERATIONS: usize = 10;
/// Tangential velocity kept by a particle touching the ground: no bounce, a corpse does not slide.
const FRICTION_KEEP: f32 = 0.55;
/// The most bones a ragdoll takes; a bigger rig keeps its animation.
pub const MAX_BONES: usize = 256;
/// Steps between world probes.
const PROBE_EVERY: u32 = 4;
/// A body may sleep only after this long...
const SETTLE_AFTER: f32 = 0.75;
/// ...once its fastest particle has stayed under this speed (yards a second)...
const QUIET_SPEED: f32 = 0.16;
/// ...for this long.
const QUIET_FOR: f32 = 0.35;
/// A body sleeps after this long whatever it is doing.
pub const TIMEOUT: f32 = 8.0;
/// The most frame time one advance catches up (a hitch does not explode the body).
const MAX_CATCH_UP: f32 = STEP * 8.0;
/// A ground sample steeper than this (normal's up component) is a wall, not a floor.
const FLOOR_MIN_UP: f32 = 0.2;

/// A ray's hit: how far along, and the surface's normal.
#[derive(Clone, Copy, Debug)]
pub struct Hit {
    pub distance: f32,
    pub normal: Vec3,
}

/// What the body collides with.
pub trait World {
    /// The first surface along `dir` (unit length) from `origin` within `max` yards.
    fn ray(&self, origin: Vec3, dir: Vec3, max: f32) -> Option<Hit>;
}

#[derive(Clone, Copy, Debug)]
struct Particle {
    position: Vec3,
    previous: Vec3,
    /// Where it was before the last step, for drawing between steps.
    render_previous: Vec3,
    radius: f32,
}

#[derive(Clone, Copy, Debug)]
struct Link {
    a: usize,
    b: usize,
    min: f32,
    max: f32,
}

#[derive(Clone, Copy, Debug)]
struct Plane {
    point: Vec3,
    normal: Vec3,
}

impl Plane {
    /// How far `p` (a sphere of `radius`) is out of the plane; negative inside.
    fn depth(&self, p: Vec3, radius: f32) -> f32 {
        (p - self.point).dot(self.normal) - radius
    }
}

/// The pose a body starts from.
pub struct Start<'a> {
    /// The rig's bone locals at the moment of death.
    pub locals: &'a [Transform],
    /// The rig's parent indices (`-1` for a root).
    pub parents: &'a [i16],
    /// The model frame in the world.
    pub root: Affine3A,
    /// How fast the unit was moving (yards a second).
    pub velocity: Vec3,
    /// Where the body is seen from (the camera): it topples away from it.
    pub viewer: Vec3,
    /// Picks the side it twists to.
    pub salt: u64,
}

/// One ragdoll.
pub struct Body {
    /// The death-frame locals: the fallback where a bone cannot be written.
    start_locals: Vec<Transform>,
    parents: Vec<i16>,
    bone_points: Vec<usize>,
    particles: Vec<Particle>,
    links: Vec<Link>,
    /// The child a bone aims at (its farthest, on another particle), and its rest direction.
    aim: Vec<Option<(usize, Vec3)>>,
    rest_rotation: Vec<Quat>,
    rest_scale: Vec<Vec3>,
    /// The floor under each particle.
    grounds: Vec<Plane>,
    /// A wall the body is moving into.
    wall: Option<Plane>,
    /// The centre at the last probe.
    probe_centre: Vec3,
    steps: u32,
    /// Steps left in which the floor does not hold the body back: a blast's throw lifts it first.
    launch: u32,
    elapsed: f32,
    accumulator: f32,
    quiet: f32,
    sleeping: bool,
}

fn parent(parents: &[i16], i: usize) -> Option<usize> {
    usize::try_from(parents[i]).ok().filter(|&p| p < i)
}

/// The rig's bones in the world, `locals` composed under `root`.
pub fn compose(locals: &[Transform], parents: &[i16], root: Affine3A) -> Vec<Affine3A> {
    let mut world: Vec<Affine3A> = Vec::with_capacity(locals.len());
    for (i, local) in locals.iter().enumerate() {
        let up = parent(parents, i).map_or(root, |p| world[p]);
        world.push(up * local.compute_affine());
    }
    world
}

impl Body {
    /// A ragdoll from a rig's pose, or `None` for a rig it cannot take (no bones, too many, a
    /// broken pose).
    pub fn new(start: Start) -> Option<Self> {
        let n = start.locals.len();
        if n == 0 || n > MAX_BONES || start.parents.len() != n {
            return None;
        }
        let rest = compose(start.locals, start.parents, start.root);
        if rest.iter().any(|m| !m.is_finite()) {
            return None;
        }
        let (root_scale, _, root_at) = start.root.to_scale_rotation_translation();
        let radius = (0.075 * root_scale.abs().max_element()).clamp(0.035, 0.16);
        let away = (root_at - start.viewer).with_y(0.0).normalize_or_zero();
        let side = Vec3::new(away.z, 0.0, -away.x) * if start.salt & 1 == 0 { 0.45 } else { -0.45 };
        let base = start.velocity.clamp_length_max(5.0);
        let mut particles: Vec<Particle> = Vec::new();
        let mut bone_points = Vec::with_capacity(n);
        for m in &rest {
            let position = Vec3::from(m.translation);
            let index = particles
                .iter()
                .position(|p| p.position.distance_squared(position) < 1e-6)
                .unwrap_or_else(|| {
                    let height = (position.y - root_at.y).max(0.0);
                    let velocity = base + (away * 1.8 + side) * height.min(1.5);
                    particles.push(Particle {
                        position,
                        previous: position - velocity * STEP,
                        render_previous: position,
                        radius,
                    });
                    particles.len() - 1
                });
            bone_points.push(index);
        }
        let mut links = Vec::new();
        let mut pairs = std::collections::HashSet::new();
        let mut add = |a: usize, b: usize, min: f32, max: f32| {
            let (a, b) = (bone_points[a], bone_points[b]);
            if a == b || !pairs.insert((a.min(b), a.max(b))) {
                return;
            }
            let d = particles[a].position.distance(particles[b].position);
            links.push(Link {
                a,
                b,
                min: d * min,
                max: d * max,
            });
        };
        for i in 0..n {
            let Some(p) = parent(start.parents, i) else {
                continue;
            };
            add(i, p, 1.0, 1.0);
            if let Some(g) = parent(start.parents, p) {
                add(i, g, 0.6, 1.05);
            }
            // Shoulders and hips stay a cross-section instead of folding into one line.
            for sibling in 0..i {
                if parent(start.parents, sibling) == Some(p) {
                    add(i, sibling, 0.92, 1.02);
                }
            }
        }
        let mut aim = vec![None; n];
        let mut rest_rotation = Vec::with_capacity(n);
        let mut rest_scale = Vec::with_capacity(n);
        for i in 0..n {
            let (scale, rotation, at) = rest[i].to_scale_rotation_translation();
            rest_rotation.push(rotation);
            rest_scale.push(scale);
            aim[i] = (i + 1..n)
                .filter(|&c| parent(start.parents, c) == Some(i))
                .filter(|&c| bone_points[c] != bone_points[i])
                .max_by(|&a, &b| {
                    let da = Vec3::from(rest[a].translation).distance_squared(at);
                    let db = Vec3::from(rest[b].translation).distance_squared(at);
                    da.total_cmp(&db)
                })
                .and_then(|c| Some((c, (Vec3::from(rest[c].translation) - at).try_normalize()?)));
        }
        let ground = Plane {
            point: root_at,
            normal: Vec3::Y,
        };
        let grounds = vec![ground; particles.len()];
        let probe_centre = centre(&particles);
        Some(Self {
            start_locals: start.locals.to_vec(),
            parents: start.parents.to_vec(),
            bone_points,
            particles,
            links,
            aim,
            rest_rotation,
            rest_scale,
            grounds,
            wall: None,
            probe_centre,
            steps: 0,
            launch: 0,
            elapsed: 0.0,
            accumulator: 0.0,
            quiet: 0.0,
            sleeping: false,
        })
    }

    pub fn is_asleep(&self) -> bool {
        self.sleeping
    }

    /// Seconds simulated.
    pub fn elapsed(&self) -> f32 {
        self.elapsed
    }

    /// Stop simulating; the body holds where it is.
    pub fn sleep(&mut self) {
        self.sleeping = true;
        self.accumulator = 0.0;
        for p in &mut self.particles {
            p.render_previous = p.position;
        }
    }

    /// Add `velocity` (yards a second) to every particle: a blast's throw.
    pub fn kick(&mut self, velocity: Vec3) {
        if !velocity.is_finite() {
            return;
        }
        for p in &mut self.particles {
            p.previous -= velocity * STEP;
        }
        self.launch = 6;
    }

    /// Run the body on by `dt` seconds of frame time.
    pub fn advance(&mut self, dt: f32, world: &impl World) {
        if self.sleeping {
            return;
        }
        self.accumulator = (self.accumulator + dt.max(0.0)).min(MAX_CATCH_UP);
        while self.accumulator >= STEP {
            self.accumulator -= STEP;
            if self.steps % PROBE_EVERY == 0 {
                self.probe(world);
            }
            self.steps += 1;
            self.elapsed += STEP;
            self.integrate();
            self.constrain();
            let top = self
                .particles
                .iter()
                .map(|p| p.position.distance_squared(p.previous))
                .fold(0.0, f32::max)
                .sqrt()
                / STEP;
            self.quiet = if top < QUIET_SPEED && self.elapsed > SETTLE_AFTER {
                self.quiet + STEP
            } else {
                0.0
            };
            let broken = self.particles.iter().any(|p| !p.position.is_finite());
            if self.quiet > QUIET_FOR || self.elapsed >= TIMEOUT || broken {
                if broken {
                    for p in &mut self.particles {
                        p.position = p.render_previous;
                        p.previous = p.position;
                    }
                }
                self.sleep();
                return;
            }
        }
    }

    fn integrate(&mut self) {
        let fall = Vec3::NEG_Y * (GRAVITY * STEP * STEP);
        for p in &mut self.particles {
            p.render_previous = p.position;
            let velocity = (p.position - p.previous) * DAMPING;
            p.previous = p.position;
            p.position += velocity + fall;
        }
    }

    fn constrain(&mut self) {
        let points = &mut self.particles;
        for _ in 0..ITERATIONS {
            for link in &self.links {
                let delta = points[link.b].position - points[link.a].position;
                let length = delta.length();
                if length < 1e-6 {
                    continue;
                }
                let correction =
                    delta * ((length - length.clamp(link.min, link.max)) / length * 0.5);
                points[link.a].position += correction;
                points[link.b].position -= correction;
            }
            for (p, ground) in points.iter_mut().zip(&self.grounds) {
                for plane in std::iter::once(ground).chain(self.wall.as_ref()) {
                    let depth = plane.depth(p.position, p.radius);
                    if depth < 0.0 {
                        p.position -= plane.normal * depth;
                    }
                }
            }
        }
        // Touching the floor: no bounce, and most of the slide taken away.
        if self.launch > 0 {
            self.launch -= 1;
            return;
        }
        for (p, ground) in points.iter_mut().zip(&self.grounds) {
            if ground.depth(p.position, p.radius) <= 0.001 {
                let velocity = p.position - p.previous;
                let tangent = velocity - ground.normal * velocity.dot(ground.normal);
                p.previous = p.position - tangent * FRICTION_KEEP;
            }
        }
    }

    /// Sample the floor on a 3×3 grid over the body's footprint, each particle taking the
    /// nearest sample's plane, and look for a wall along the way the body is moving.
    fn probe(&mut self, world: &impl World) {
        let (mut lo, mut hi) = (Vec3::splat(f32::MAX), Vec3::splat(f32::MIN));
        for p in &self.particles {
            lo = lo.min(p.position);
            hi = hi.max(p.position);
        }
        let pad = 0.3;
        let top = hi.y + 0.3;
        let reach = (hi.y - lo.y) + 4.0;
        let mut samples: Vec<(Vec2, Plane)> = Vec::with_capacity(9);
        for i in 0..3 {
            for j in 0..3 {
                let x = lo.x - pad + (hi.x - lo.x + 2.0 * pad) * i as f32 * 0.5;
                let z = lo.z - pad + (hi.z - lo.z + 2.0 * pad) * j as f32 * 0.5;
                let origin = Vec3::new(x, top, z);
                if let Some(hit) = world.ray(origin, Vec3::NEG_Y, reach)
                    && hit.normal.is_finite()
                    && hit.normal.y > FLOOR_MIN_UP
                {
                    let normal = hit.normal.normalize_or(Vec3::Y);
                    samples.push((
                        Vec2::new(x, z),
                        Plane {
                            point: origin + Vec3::NEG_Y * hit.distance,
                            normal,
                        },
                    ));
                }
            }
        }
        if !samples.is_empty() {
            for (p, ground) in self.particles.iter().zip(&mut self.grounds) {
                let at = Vec2::new(p.position.x, p.position.z);
                if let Some((_, plane)) = samples.iter().min_by(|a, b| {
                    a.0.distance_squared(at)
                        .total_cmp(&b.0.distance_squared(at))
                }) {
                    *ground = *plane;
                }
            }
        }
        let centre_now = centre(&self.particles);
        let moved = centre_now - self.probe_centre;
        self.probe_centre = centre_now;
        self.wall = None;
        if let Some(dir) = moved.with_y(0.0).try_normalize() {
            let half = ((hi - lo).with_y(0.0).length() * 0.5).max(0.2);
            let reach = moved.with_y(0.0).length() * 2.0 + half + 0.2;
            if let Some(hit) = world.ray(centre_now, dir, reach)
                && hit.normal.is_finite()
                && hit.normal.y.abs() < 0.7
            {
                self.wall = Some(Plane {
                    point: centre_now + dir * hit.distance,
                    normal: hit.normal.with_y(0.0).normalize_or(-dir),
                });
            }
        }
    }

    /// The bone locals for the body's current shape, under the model frame `root`.
    pub fn pose(&self, root: Affine3A, out: &mut Vec<Transform>) {
        let n = self.bone_points.len();
        out.clear();
        let alpha = if self.sleeping {
            1.0
        } else {
            (self.accumulator / STEP).clamp(0.0, 1.0)
        };
        let points: Vec<Vec3> = self
            .particles
            .iter()
            .map(|p| p.render_previous.lerp(p.position, alpha))
            .collect();
        let mut world: Vec<Affine3A> = Vec::with_capacity(n);
        let mut deltas: Vec<Quat> = Vec::with_capacity(n);
        for i in 0..n {
            let up = parent(&self.parents, i);
            let inherited = up.map_or(Quat::IDENTITY, |p| deltas[p]);
            let delta = self.aim[i]
                .and_then(|(c, before)| {
                    let after = (points[self.bone_points[c]] - points[self.bone_points[i]])
                        .try_normalize()?;
                    Some(Quat::from_rotation_arc(before, after))
                })
                .unwrap_or(inherited)
                .normalize();
            let m = Affine3A::from_scale_rotation_translation(
                self.rest_scale[i],
                delta * self.rest_rotation[i],
                points[self.bone_points[i]],
            );
            let parent_world = up.map_or(root, |p| world[p]);
            let local = (parent_world.matrix3.determinant().abs() > 1e-9)
                .then(|| {
                    let (scale, rotation, translation) =
                        (parent_world.inverse() * m).to_scale_rotation_translation();
                    Transform {
                        translation,
                        rotation,
                        scale,
                    }
                })
                .filter(|t| t.is_finite());
            match local {
                Some(t) => {
                    out.push(t);
                    world.push(m);
                }
                None => {
                    let t = self.start_locals[i];
                    out.push(t);
                    world.push(parent_world * t.compute_affine());
                }
            }
            deltas.push(delta);
        }
    }

    #[cfg(test)]
    fn positions(&self) -> impl Iterator<Item = Vec3> + '_ {
        self.particles.iter().map(|p| p.position)
    }
}

fn centre(particles: &[Particle]) -> Vec3 {
    particles.iter().map(|p| p.position).sum::<Vec3>() / particles.len().max(1) as f32
}

/// The throw a blast gives a body `offset` from it (yards, from the blast to the body's chest):
/// outward and up, stronger the nearer it is; `None` beyond `radius + 2`.
pub fn blast_velocity(offset: Vec3, radius: f32) -> Option<Vec3> {
    let reach = radius.max(0.0) + 2.0;
    let distance = offset.length();
    if !distance.is_finite() || distance > reach {
        return None;
    }
    let strength = (1.0 - distance / reach).clamp(0.0, 1.0);
    Some(
        offset.with_y(0.0).normalize_or_zero() * (4.0 + 3.0 * strength)
            + Vec3::Y * (2.0 + strength),
    )
}

/// Before another body starts: put the oldest awake ones to sleep until fewer than `max_awake`
/// are awake.
pub fn make_room(bodies: &mut [&mut Body], max_awake: usize) {
    loop {
        let awake = bodies.iter().filter(|b| !b.sleeping).count();
        if awake < max_awake {
            return;
        }
        let Some(oldest) = bodies
            .iter_mut()
            .filter(|b| !b.sleeping)
            .max_by(|a, b| a.elapsed.total_cmp(&b.elapsed))
        else {
            return;
        };
        oldest.sleep();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// A flat floor at `y`.
    struct Floor(f32);

    impl World for Floor {
        fn ray(&self, origin: Vec3, dir: Vec3, max: f32) -> Option<Hit> {
            if dir.y >= -1e-6 || origin.y < self.0 {
                return None;
            }
            let distance = (origin.y - self.0) / -dir.y;
            (distance <= max).then_some(Hit {
                distance,
                normal: Vec3::Y,
            })
        }
    }

    /// Nothing to hit.
    struct Void;

    impl World for Void {
        fn ray(&self, _: Vec3, _: Vec3, _: f32) -> Option<Hit> {
            None
        }
    }

    /// A small humanoid: a spine of three, a head, two arms off the chest and two legs off the
    /// hips, standing at the origin.
    fn skeleton() -> (Vec<Transform>, Vec<i16>) {
        let t = |x: f32, y: f32, z: f32| Transform::from_xyz(x, y, z);
        let locals = vec![
            t(0.0, 1.0, 0.0),                                             // 0 hips
            t(0.0, 0.4, 0.0).with_rotation(Quat::from_rotation_y(0.3)),   // 1 chest
            t(0.0, 0.3, 0.0),                                             // 2 neck
            t(0.0, 0.25, 0.0),                                            // 3 head
            t(0.25, 0.0, 0.0).with_rotation(Quat::from_rotation_z(-1.2)), // 4 left arm
            t(0.0, -0.35, 0.0),                                           // 5 left hand
            t(-0.25, 0.0, 0.0).with_rotation(Quat::from_rotation_z(1.2)), // 6 right arm
            t(0.0, -0.35, 0.0),                                           // 7 right hand
            t(0.12, -0.1, 0.0),                                           // 8 left leg
            t(0.0, -0.45, 0.0),                                           // 9 left knee
            t(0.0, -0.45, 0.0),                                           // 10 left foot
            t(-0.12, -0.1, 0.0),                                          // 11 right leg
            t(0.0, -0.45, 0.0),                                           // 12 right knee
            t(0.0, -0.45, 0.0),                                           // 13 right foot
        ];
        let parents = vec![-1, 0, 1, 2, 1, 4, 1, 6, 0, 8, 9, 0, 11, 12];
        (locals, parents)
    }

    fn body(velocity: Vec3) -> Body {
        let (locals, parents) = skeleton();
        Body::new(Start {
            locals: &locals,
            parents: &parents,
            root: Affine3A::IDENTITY,
            velocity,
            viewer: Vec3::new(0.0, 1.5, 5.0),
            salt: 7,
        })
        .expect("a body")
    }

    fn run(b: &mut Body, world: &impl World, secs: f32) {
        let frames = (secs * 60.0) as usize;
        for _ in 0..frames {
            b.advance(1.0 / 60.0, world);
        }
    }

    #[test]
    fn falls_and_rests_on_the_floor() {
        let mut b = body(Vec3::ZERO);
        let head_before = b.positions().map(|p| p.y).fold(f32::MIN, f32::max);
        run(&mut b, &Floor(0.0), 10.0);
        let top = b.positions().map(|p| p.y).fold(f32::MIN, f32::max);
        assert!(top < head_before * 0.5, "it fell over: top {top}");
        for p in b.positions() {
            assert!(p.is_finite());
            assert!(p.y >= 0.035 - 0.01, "above the floor: {p}");
        }
        // It toppled away from the viewer (at +z).
        let c = centre(&b.particles);
        assert!(c.z < 0.0, "away from the viewer: {c}");
    }

    #[test]
    fn links_keep_their_lengths() {
        let mut b = body(Vec3::new(2.0, 0.0, 0.0));
        run(&mut b, &Floor(0.0), 3.0);
        for l in &b.links {
            let d = b.particles[l.a]
                .position
                .distance(b.particles[l.b].position);
            assert!(
                d >= l.min - 0.03 && d <= l.max + 0.03,
                "link {}-{}: {d} not in {}..{}",
                l.a,
                l.b,
                l.min,
                l.max
            );
        }
    }

    #[test]
    fn settles_and_sleeps_before_the_timeout() {
        let mut b = body(Vec3::ZERO);
        run(&mut b, &Floor(0.0), 9.0);
        assert!(b.is_asleep());
        assert!(b.elapsed() < TIMEOUT - 0.5, "slept at {}", b.elapsed());
        let held: Vec<Vec3> = b.positions().collect();
        run(&mut b, &Floor(0.0), 1.0);
        assert!(b.positions().zip(held).all(|(a, h)| a == h), "asleep holds");
    }

    #[test]
    fn sleeps_at_the_timeout_whatever_it_is_doing() {
        let mut b = body(Vec3::ZERO);
        b.kick(Vec3::Y * 250.0);
        run(&mut b, &Void, 9.0);
        assert!(b.is_asleep());
        assert!(
            (b.elapsed() - TIMEOUT).abs() < 2.0 * STEP,
            "{}",
            b.elapsed()
        );
    }

    #[test]
    fn a_blast_throws_outward_and_up_less_with_distance() {
        let near = blast_velocity(Vec3::new(1.0, 1.0, 0.0), 5.0).unwrap();
        let far = blast_velocity(Vec3::new(5.0, 1.0, 0.0), 5.0).unwrap();
        assert!(near.x > 0.0 && near.y > 0.0 && near.z.abs() < 1e-6);
        assert!(far.x > 0.0 && far.y > 0.0);
        assert!(near.length() > far.length());
        assert!(blast_velocity(Vec3::new(7.5, 0.0, 0.0), 5.0).is_none());
    }

    #[test]
    fn at_most_twelve_awake() {
        let mut bodies: Vec<Body> = Vec::new();
        for _ in 0..20 {
            {
                let mut refs: Vec<&mut Body> = bodies.iter_mut().collect();
                make_room(&mut refs, 12);
            }
            bodies.push(body(Vec3::ZERO));
            for b in &mut bodies {
                b.advance(1.0 / 60.0, &Floor(0.0));
            }
            assert!(bodies.iter().filter(|b| !b.is_asleep()).count() <= 12);
        }
        // The oldest went to sleep, the newest are awake.
        assert!(bodies[0].is_asleep());
        assert!(!bodies[19].is_asleep());
    }

    #[test]
    fn the_rest_pose_round_trips() {
        let (locals, parents) = skeleton();
        let root = Affine3A::from_scale_rotation_translation(
            Vec3::splat(1.3),
            Quat::from_rotation_y(0.8),
            Vec3::new(10.0, 2.0, -4.0),
        );
        let b = Body::new(Start {
            locals: &locals,
            parents: &parents,
            root,
            velocity: Vec3::ZERO,
            viewer: Vec3::ZERO,
            salt: 0,
        })
        .unwrap();
        let mut out = Vec::new();
        b.pose(root, &mut out);
        assert_eq!(out.len(), locals.len());
        for (i, (a, e)) in out.iter().zip(&locals).enumerate() {
            assert!(
                a.translation.distance(e.translation) < 1e-3,
                "bone {i} translation {} vs {}",
                a.translation,
                e.translation
            );
            assert!(
                a.rotation.angle_between(e.rotation) < 1e-3,
                "bone {i} rotation"
            );
            assert!(a.scale.distance(e.scale) < 1e-3, "bone {i} scale");
        }
    }

    #[test]
    fn refuses_what_it_cannot_take() {
        let start = |locals: &[Transform], parents: &[i16]| {
            Body::new(Start {
                locals,
                parents,
                root: Affine3A::IDENTITY,
                velocity: Vec3::ZERO,
                viewer: Vec3::ZERO,
                salt: 0,
            })
            .is_none()
        };
        assert!(start(&[], &[]));
        let many = vec![Transform::IDENTITY; MAX_BONES + 1];
        assert!(start(&many, &vec![-1; MAX_BONES + 1]));
        assert!(start(&[Transform::IDENTITY], &[]));
    }
}
