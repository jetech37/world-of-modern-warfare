//! WDBC tables (the 1.12 `.dbc` layout: records of 4-byte fields and a string block), and the
//! row merge: the community's rows our table lacks are appended to ours, their strings moved
//! into our string block.

use std::collections::{BTreeMap, HashSet};

use anyhow::{bail, Result};

/// One table, every field a raw `u32` (floats as bits, strings as string-block offsets).
#[derive(Clone, Debug)]
pub struct Dbc {
    pub field_count: usize,
    pub records: Vec<Vec<u32>>,
    pub strings: Vec<u8>,
}

impl Dbc {
    pub fn parse(b: &[u8]) -> Result<Self> {
        if b.len() < 20 || &b[..4] != b"WDBC" {
            bail!("not a WDBC table");
        }
        let rd = |o: usize| u32::from_le_bytes([b[o], b[o + 1], b[o + 2], b[o + 3]]) as usize;
        let (n, fields, size, ssize) = (rd(4), rd(8), rd(12), rd(16));
        if size != fields * 4 {
            bail!("record size {size} is not {fields} 4-byte fields");
        }
        let end = 20 + n * size;
        if b.len() < end + ssize {
            bail!("truncated table");
        }
        let records = (0..n)
            .map(|i| {
                (0..fields)
                    .map(|f| rd(20 + i * size + f * 4) as u32)
                    .collect()
            })
            .collect();
        Ok(Self {
            field_count: fields,
            records,
            strings: b[end..end + ssize].to_vec(),
        })
    }

    pub fn to_bytes(&self) -> Vec<u8> {
        let mut out = Vec::with_capacity(20 + self.records.len() * self.field_count * 4);
        out.extend_from_slice(b"WDBC");
        for v in [
            self.records.len(),
            self.field_count,
            self.field_count * 4,
            self.strings.len(),
        ] {
            out.extend_from_slice(&(v as u32).to_le_bytes());
        }
        for r in &self.records {
            for f in r {
                out.extend_from_slice(&f.to_le_bytes());
            }
        }
        out.extend_from_slice(&self.strings);
        out
    }

    /// The string at string-block offset `ofs`.
    pub fn string(&self, ofs: u32) -> &str {
        let s = &self.strings[(ofs as usize).min(self.strings.len())..];
        let end = s.iter().position(|&c| c == 0).unwrap_or(s.len());
        std::str::from_utf8(&s[..end]).unwrap_or("")
    }

    pub fn row(&self, id: u32) -> Option<&Vec<u32>> {
        self.records.iter().find(|r| r[0] == id)
    }

    pub fn ids(&self) -> HashSet<u32> {
        self.records.iter().map(|r| r[0]).collect()
    }

    /// Add `s` to the string block; its offset (0 for the empty string).
    pub fn add_string(&mut self, s: &[u8]) -> u32 {
        if self.strings.is_empty() {
            self.strings.push(0);
        }
        if s.is_empty() {
            return 0;
        }
        let ofs = self.strings.len() as u32;
        self.strings.extend_from_slice(s);
        self.strings.push(0);
        ofs
    }

    fn raw_string(&self, ofs: u32) -> &[u8] {
        let s = &self.strings[(ofs as usize).min(self.strings.len())..];
        let end = s.iter().position(|&c| c == 0).unwrap_or(s.len());
        &s[..end]
    }

    /// Whether `v` can be a string-block offset here: 0, or the start of a string (offset 0 or
    /// just after a NUL) inside the block.
    fn is_string_start(&self, v: u32) -> bool {
        let v = v as usize;
        v == 0 || (v < self.strings.len() && self.strings[v - 1] == 0)
    }
}

/// The columns that hold string offsets, found from the data: in every row of both tables the
/// value is 0 or a string start, and the column takes at least two distinct non-zero values (so a
/// 0/1 flag column, whose 1 is always a string start, stays a number). A column that is 0 in
/// every row is a number: copied as is, it means the empty string either way.
pub fn string_columns(a: &Dbc, b: &Dbc) -> Vec<usize> {
    let fields = a.field_count.min(b.field_count);
    (1..fields)
        .filter(|&f| {
            let mut distinct = HashSet::new();
            for t in [a, b] {
                for r in &t.records {
                    if !t.is_string_start(r[f]) {
                        return false;
                    }
                    if r[f] != 0 {
                        distinct.insert(r[f]);
                    }
                }
            }
            distinct.len() >= 2
        })
        .collect()
}

/// What a merge did to one table.
#[derive(Debug, Default, Clone)]
pub struct MergeReport {
    /// Rows appended (their ids).
    pub added: Vec<u32>,
    /// Ids both tables hold with different contents: ours kept, unless `theirs_win` named them.
    pub conflicts: Vec<u32>,
    /// Conflicting ids the community's row replaced (`theirs_win`).
    pub replaced: Vec<u32>,
    pub string_columns: Vec<usize>,
}

/// Append the rows of `theirs` whose id `ours` lacks (and which `keep` accepts), with their
/// strings re-homed. Where both hold an id with different contents ours wins, except ids for
/// which `theirs_win` is true.
pub fn merge_rows(
    ours: &mut Dbc,
    theirs: &Dbc,
    keep: impl Fn(u32) -> bool,
    theirs_win: impl Fn(u32) -> bool,
) -> Result<MergeReport> {
    if ours.field_count != theirs.field_count {
        bail!(
            "field counts differ ({} ours, {} theirs)",
            ours.field_count,
            theirs.field_count
        );
    }
    let cols = string_columns(ours, theirs);
    let mut report = MergeReport {
        string_columns: cols.clone(),
        ..Default::default()
    };
    let index: BTreeMap<u32, usize> = ours
        .records
        .iter()
        .enumerate()
        .map(|(i, r)| (r[0], i))
        .collect();
    for r in &theirs.records {
        let id = r[0];
        if !keep(id) {
            continue;
        }
        let rehome = |ours: &mut Dbc| -> Vec<u32> {
            let mut row = r.clone();
            for &c in &cols {
                let s = theirs.raw_string(r[c]).to_vec();
                row[c] = ours.add_string(&s);
            }
            row
        };
        match index.get(&id) {
            None => {
                let row = rehome(ours);
                ours.records.push(row);
                report.added.push(id);
            }
            Some(&i) => {
                let same = (0..ours.field_count).all(|c| {
                    if cols.contains(&c) {
                        ours.raw_string(ours.records[i][c]) == theirs.raw_string(r[c])
                    } else {
                        ours.records[i][c] == r[c]
                    }
                });
                if same {
                    continue;
                }
                report.conflicts.push(id);
                if theirs_win(id) {
                    let row = rehome(ours);
                    ours.records[i] = row;
                    report.replaced.push(id);
                }
            }
        }
    }
    ours.records.sort_by_key(|r| r[0]);
    Ok(report)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn table(rows: &[(u32, &str, u32)]) -> Dbc {
        let mut t = Dbc {
            field_count: 3,
            records: Vec::new(),
            strings: vec![0],
        };
        for &(id, s, n) in rows {
            let o = t.add_string(s.as_bytes());
            t.records.push(vec![id, o, n]);
        }
        t
    }

    #[test]
    fn new_rows_are_appended_with_their_strings_and_ours_win_a_clash() {
        let mut ours = table(&[(1, "Azeroth", 1), (2, "Kalimdor", 1)]);
        let theirs = table(&[
            (2, "Kalimdor Edited", 1),
            (800, "KarazhanCrypt", 1),
            (5, "Gilneas", 0),
        ]);
        let r = merge_rows(&mut ours, &theirs, |_| true, |_| false).unwrap();
        assert_eq!(r.string_columns, vec![1]);
        assert_eq!(r.added, vec![800, 5]);
        assert_eq!(r.conflicts, vec![2]);
        assert_eq!(
            ours.string(ours.row(2).unwrap()[1]),
            "Kalimdor",
            "ours kept"
        );
        assert_eq!(ours.string(ours.row(800).unwrap()[1]), "KarazhanCrypt");
        assert_eq!(ours.string(ours.row(5).unwrap()[1]), "Gilneas");
        assert_eq!(
            ours.row(5).unwrap()[2],
            0,
            "the 0/1 column is copied as a number"
        );
        let ids: Vec<u32> = ours.records.iter().map(|r| r[0]).collect();
        assert_eq!(ids, vec![1, 2, 5, 800], "sorted by id");
        // Round trip.
        let back = Dbc::parse(&ours.to_bytes()).unwrap();
        assert_eq!(back.records, ours.records);
    }

    #[test]
    fn theirs_win_replaces_only_the_named_ids() {
        let mut ours = table(&[(800, "WOMWDalaran", 1), (801, "Other", 1)]);
        let theirs = table(&[(800, "KarazhanCrypt", 1), (801, "Moomoo", 1)]);
        let r = merge_rows(&mut ours, &theirs, |_| true, |id| id == 800).unwrap();
        assert_eq!(r.replaced, vec![800]);
        assert_eq!(ours.string(ours.row(800).unwrap()[1]), "KarazhanCrypt");
        assert_eq!(ours.string(ours.row(801).unwrap()[1]), "Other");
    }

    #[test]
    fn a_layout_change_is_refused() {
        let mut ours = table(&[(1, "a", 1)]);
        let mut theirs = table(&[(2, "b", 1)]);
        theirs.field_count = 2;
        theirs.records = vec![vec![2, 1]];
        assert!(merge_rows(&mut ours, &theirs, |_| true, |_| false).is_err());
    }
}
