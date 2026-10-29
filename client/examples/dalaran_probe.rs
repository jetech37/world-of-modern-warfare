//! Probe of the floating Dalaran (`wotlk::dalaran`), for placing things in and under it.
//!
//! ```text
//! dalaran_probe lift    how far the city's rock clears the stock ground (CLEARANCE)
//! dalaran_probe spots   open standing spots on the city's floors (x y z), 8-yard grid,
//!                       and the stock ground at the crater centre under the city
//! ```
//!
//! Needs the 3.3.5 install and `WOW_DATA` (the 1.12 `Data` folder).

use anyhow::Result;
use benilla_mw2::wotlk::{self, dalaran::*};

fn main() -> Result<()> {
    let src = wotlk::source::Source::open_default().expect("the 3.3.5 install");
    let data = std::path::PathBuf::from(std::env::var("WOW_DATA")?);
    let chain = wotlk::stock_chain(&data)?;
    let root = src.read(ROOT)?;
    let n = u32::from_le_bytes(chunk(&root, b"MOHD").unwrap()[4..8].try_into()?) as usize;
    let stem = ROOT.strip_suffix(".wmo").unwrap();
    let groups: Vec<Vec<u8>> = (0..n).map(|g| src.read(&format!("{stem}_{g:03}.wmo")).unwrap()).collect();
    let bbox = mohd_bbox(&root)?;
    let t = translation(bbox);
    let tris = world_triangles(&groups, t);
    let ground = stock_ground(&chain)?;
    match std::env::args().nth(1).as_deref() {
        Some("lift") => {
            let (mut least, mut at) = (f32::MAX, [0.0f32; 3]);
            for p in tris.iter().flatten() {
                if let Some(g) = ground.height(p[0], p[1])
                    && p[2] - g < least
                {
                    least = p[2] - g;
                    at = *p;
                }
            }
            println!("origin {t:?}: the rock clears the ground by {least:.1} at {at:?}");
        }
        Some("spots") => {
            let a = arrival(bbox);
            println!("arrival {a:?} stands: {:?}", standing_spot(&tris, a[0], a[1], a[2]));
            for (x, y) in [(CENTRE[0], CENTRE[1]), (CENTRE[0] + 6.0, CENTRE[1] - 6.0)] {
                println!("ground at {x:.1},{y:.1}: {:?}", ground.height(x, y));
            }
            let fp = Footprint::new(&tris);
            let mut x = CENTRE[0] - 260.0;
            while x < CENTRE[0] + 260.0 {
                let mut y = CENTRE[1] - 260.0;
                while y < CENTRE[1] + 260.0 {
                    if fp.base(x, y).is_some()
                        && let Some(z) = standing_spot(&tris, x, y, a[2])
                    {
                        println!("{x:.1} {y:.1} {z:.2}");
                    }
                    y += 8.0;
                }
                x += 8.0;
            }
        }
        Some("edge") => {
            let fp = Footprint::new(&tris);
            for az in (0..360).step_by(10) {
                let (c, s) = ((az as f32).to_radians().cos(), (az as f32).to_radians().sin());
                let mut r = 0.0;
                let mut edge = 0.0;
                while r < 450.0 {
                    if fp.base(CENTRE[0] + c * r, CENTRE[1] + s * r).is_some() {
                        edge = r;
                    }
                    r += 1.0;
                }
                let mut line = format!("az {az:3} edge {edge:.0}:");
                for d in [0.0f32, 3.0, 6.0, 10.0, 15.0, 20.0, 30.0] {
                    let (x, y) = (CENTRE[0] + c * (edge - d), CENTRE[1] + s * (edge - d));
                    let mut up: Vec<i32> = tris.iter().filter(|t| up_facing(t)).filter_map(|t| floors(std::slice::from_ref(t), x, y).first().copied()).map(|z| z as i32).collect();
                    up.sort();
                    line += &format!(" -{d}:{up:?}");
                }
                let (x, y) = (CENTRE[0] + c * (edge + 20.0), CENTRE[1] + s * (edge + 20.0));
                line += &format!(" ground+20 {:?}", ground.height(x, y).map(|v| v as i32));
                println!("{line}");
            }
        }
        Some("ground") => {
            let fp = Footprint::new(&tris);
            let v: Vec<f32> = std::env::args().skip(2).filter_map(|v| v.parse().ok()).collect();
            for p in v.chunks(2) {
                let o = ground.height(p[0], p[1]).unwrap();
                println!("{:.1} {:.1}: stock {o:.2} new {:.2} in city {:?}", p[0], p[1], fp.terrain(p[0], p[1], o), fp.base(p[0], p[1]));
            }
        }
        Some("outline") => {
            // The outline polygon, `margin` yards out (8 + the ring by default).
            let fp = Footprint::new(&tris);
            let margin: f32 = std::env::args().nth(2).and_then(|v| v.parse().ok()).unwrap_or(8.0 + BLEND);
            let mut poly = Vec::new();
            for a in (0..360).step_by(5) {
                let (c, s) = ((a as f32).to_radians().cos(), (a as f32).to_radians().sin());
                let mut last = 0.0;
                let mut r = 0.0;
                while r < 600.0 {
                    if fp.base(CENTRE[0] + c * r, CENTRE[1] + s * r).is_some() {
                        last = r;
                    }
                    r += 1.0;
                }
                poly.push(format!("{:.0} {:.0}", CENTRE[0] + c * (last + margin), CENTRE[1] + s * (last + margin)));
            }
            poly.push(poly[0].clone());
            println!("POLYGON(({}))", poly.join(","));
        }
        Some("walk") => {
            // Flood fill over walkable floor samples (2-yd grid; a step up of 1.3 yd, a drop of 4)
            // from the arrival; prints how close to the outline it gets per 30 degrees.
            let fp = Footprint::new(&tris);
            let [bx0, by0, bx1, by1] = fp.bounds();
            let cell = 8.0f32;
            let (gw, gh) = (((bx1 - bx0) / cell) as usize + 1, ((by1 - by0) / cell) as usize + 1);
            let mut bucket: Vec<Vec<usize>> = vec![Vec::new(); gw * gh];
            for (i, t) in tris.iter().enumerate() {
                let (x0, x1) = (t.iter().map(|p| p[0]).fold(f32::MAX, f32::min), t.iter().map(|p| p[0]).fold(f32::MIN, f32::max));
                let (y0, y1) = (t.iter().map(|p| p[1]).fold(f32::MAX, f32::min), t.iter().map(|p| p[1]).fold(f32::MIN, f32::max));
                for cx in ((x0 - bx0) / cell).max(0.0) as usize..=(((x1 - bx0) / cell) as usize).min(gw - 1) {
                    for cy in ((y0 - by0) / cell).max(0.0) as usize..=(((y1 - by0) / cell) as usize).min(gh - 1) {
                        bucket[cy * gw + cx].push(i);
                    }
                }
            }
            let near = |x: f32, y: f32| -> Vec<[[f32; 3]; 3]> {
                let (cx, cy) = (((x - bx0) / cell) as usize, ((y - by0) / cell) as usize);
                if cx >= gw || cy >= gh { return Vec::new(); }
                bucket[cy * gw + cx].iter().map(|&i| tris[i]).collect()
            };
            let mut a = arrival(bbox);
            let args: Vec<f32> = std::env::args().skip(2).filter_map(|v| v.parse().ok()).collect();
            if args.len() == 3 {
                a = [args[0], args[1], args[2], 0.0];
            }
            let target = arrival(bbox);
            let step = 2.0f32;
            let key = |x: f32, y: f32, z: f32| ((x / step).round() as i32, (y / step).round() as i32, (z * 2.0).round() as i32);
            let mut seen = std::collections::HashSet::new();
            let mut q = std::collections::VecDeque::new();
            q.push_back([a[0], a[1], a[2]]);
            seen.insert(key(a[0], a[1], a[2]));
            let mut best = vec![0.0f32; 12];
            let mut reached = 0;
            while let Some(p) = q.pop_front() {
                reached += 1;
                let r = ((p[0] - CENTRE[0]).powi(2) + (p[1] - CENTRE[1]).powi(2)).sqrt();
                let az = ((p[1] - CENTRE[1]).atan2(p[0] - CENTRE[0]).to_degrees().rem_euclid(360.0) / 30.0) as usize % 12;
                if r > best[az] { best[az] = r; }
                for (dx, dy) in [(step, 0.0), (-step, 0.0), (0.0, step), (0.0, -step)] {
                    let (x, y) = (p[0] + dx, p[1] + dy);
                    let tl = near(x, y);
                    let fl = floors(&tl, x, y);
                    for &f in &fl {
                        if f < FLOOR_MIN || f - p[2] > 1.3 || p[2] - f > 4.0 { continue; }
                        // head room
                        if fl.iter().any(|g| *g > f + 0.3 && *g < f + 2.2) { continue; }
                        // no wall between: a surface between the two heights at the midpoint
                        let (mx, my) = (p[0] + dx / 2.0, p[1] + dy / 2.0);
                        let lo = p[2].min(f) + 0.4;
                        if floors(&near(mx, my), mx, my).iter().any(|g| *g > lo + 0.0 && *g < p[2].max(f) + 2.0 && (*g - p[2]).abs() > 1.3 && (*g - f).abs() > 1.3) { continue; }
                        if seen.insert(key(x, y, f)) { q.push_back([x, y, f]); }
                    }
                }
                if reached > 400000 { break; }
            }
            // Street samples on the city's outline: open to the land outside.
            let mut gates = Vec::new();
            for k in &seen {
                let (x, y, z) = (k.0 as f32 * step, k.1 as f32 * step, k.2 as f32 / 2.0);
                let (ux, uy) = ((x - CENTRE[0]), (y - CENTRE[1]));
                let r = (ux * ux + uy * uy).sqrt();
                let (ux, uy) = (ux / r, uy / r);
                // Nothing of the city further out along this ray: the true outline.
                if (4..120).all(|d| fp.base(x + ux * d as f32, y + uy * d as f32).is_none()) {
                    gates.push((x, y, z));
                }
            }
            gates.sort_by(|a, b| a.partial_cmp(b).unwrap());
            for g in gates.iter().step_by(4) {
                let az = (g.1 - CENTRE[1]).atan2(g.0 - CENTRE[0]).to_degrees().rem_euclid(360.0);
                println!("edge sample {:.0} {:.0} {:.1} az {az:.0}", g.0, g.1, g.2);
            }
            println!("reached {reached} samples; the arrival floor reached: {}", seen.iter().any(|k| (k.0 as f32 * step - target[0]).abs() < 2.1 && (k.1 as f32 * step - target[1]).abs() < 2.1 && (k.2 as f32 / 2.0 - target[2]).abs() < 1.0));
            for (k, b) in best.iter().enumerate() { println!("az {}-{}: farthest {b:.0}", k * 30, k * 30 + 30); }
            let mut low = f32::MAX;
            for k in &seen { low = low.min(k.2 as f32 / 2.0); }
            println!("lowest floor reached {low}");
        }
        _ => eprintln!("dalaran_probe lift | spots"),
    }
    Ok(())
}
