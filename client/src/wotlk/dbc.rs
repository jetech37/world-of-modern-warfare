//! A WDBC table (the 1.12 and 3.3.5 `.dbc` layout): records of 4-byte fields and a string block.
//! Read a table, add or replace rows, write it back; the patch layer's DBCs are the 1.12 tables
//! with our rows appended.

use anyhow::{Result, bail};

/// One table, every field a raw `u32` (floats as bits, strings as string-block offsets).
#[derive(Clone)]
pub struct Dbc {
    pub field_count: usize,
    pub records: Vec<Vec<u32>>,
    strings: Vec<u8>,
}

/// One field of a new record.
#[derive(Clone, Debug, PartialEq)]
pub enum Field {
    U32(u32),
    F32(f32),
    Str(String),
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
            .map(|i| (0..fields).map(|f| rd(20 + i * size + f * 4) as u32).collect())
            .collect();
        Ok(Self {
            field_count: fields,
            records,
            strings: b[end..end + ssize].to_vec(),
        })
    }

    /// The string at string-block offset `ofs`.
    pub fn string(&self, ofs: u32) -> &str {
        let s = &self.strings[(ofs as usize).min(self.strings.len())..];
        let end = s.iter().position(|&c| c == 0).unwrap_or(s.len());
        std::str::from_utf8(&s[..end]).unwrap_or("")
    }

    /// Field `f` of the record with id (field 0) `id`.
    pub fn row(&self, id: u32) -> Option<&Vec<u32>> {
        self.records.iter().find(|r| r[0] == id)
    }

    pub fn max_id(&self) -> u32 {
        self.records.iter().map(|r| r[0]).max().unwrap_or(0)
    }

    /// Add `s` to the string block; its offset.
    pub fn add_string(&mut self, s: &str) -> u32 {
        if s.is_empty() {
            // Offset 0 is the empty string every table starts with.
            if self.strings.is_empty() {
                self.strings.push(0);
            }
            return 0;
        }
        if self.strings.is_empty() {
            self.strings.push(0);
        }
        let ofs = self.strings.len() as u32;
        self.strings.extend_from_slice(s.as_bytes());
        self.strings.push(0);
        ofs
    }

    /// Add `fields` as a record, replacing a record with the same id; `fields[0]` is the id.
    pub fn upsert(&mut self, fields: &[Field]) -> Result<()> {
        if fields.len() != self.field_count {
            bail!("{} fields for a {}-field table", fields.len(), self.field_count);
        }
        let rec: Vec<u32> = fields
            .iter()
            .map(|f| match f {
                Field::U32(v) => *v,
                Field::F32(v) => v.to_bits(),
                Field::Str(s) => self.add_string(s),
            })
            .collect();
        match self.records.iter_mut().find(|r| r[0] == rec[0]) {
            Some(r) => *r = rec,
            None => self.records.push(rec),
        }
        Ok(())
    }

    /// Add a raw record, replacing a record with the same id.
    pub fn upsert_raw(&mut self, rec: Vec<u32>) {
        match self.records.iter_mut().find(|r| r[0] == rec[0]) {
            Some(r) => *r = rec,
            None => self.records.push(rec),
        }
    }

    pub fn to_bytes(&self) -> Vec<u8> {
        let mut strings = self.strings.clone();
        if strings.is_empty() {
            strings.push(0);
        }
        let mut out = Vec::with_capacity(20 + self.records.len() * self.field_count * 4);
        out.extend_from_slice(b"WDBC");
        for v in [
            self.records.len(),
            self.field_count,
            self.field_count * 4,
            strings.len(),
        ] {
            out.extend_from_slice(&(v as u32).to_le_bytes());
        }
        for r in &self.records {
            for v in r {
                out.extend_from_slice(&v.to_le_bytes());
            }
        }
        out.extend_from_slice(&strings);
        out
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn rows_round_trip_and_replace_by_id() {
        let mut t = Dbc {
            field_count: 4,
            records: Vec::new(),
            strings: Vec::new(),
        };
        t.upsert(&[
            Field::U32(5),
            Field::Str("Creature\\A\\A.mdx".into()),
            Field::F32(1.5),
            Field::Str(String::new()),
        ])
        .unwrap();
        t.upsert(&[Field::U32(9), Field::Str("b".into()), Field::F32(2.0), Field::U32(3)])
            .unwrap();
        t.upsert(&[Field::U32(5), Field::Str("c".into()), Field::F32(0.5), Field::U32(1)])
            .unwrap();
        let back = Dbc::parse(&t.to_bytes()).unwrap();
        assert_eq!(back.records.len(), 2);
        let r5 = back.row(5).unwrap();
        assert_eq!(back.string(r5[1]), "c");
        assert_eq!(f32::from_bits(r5[2]), 0.5);
        assert_eq!(back.string(back.row(9).unwrap()[1]), "b");
        assert_eq!(back.max_id(), 9);
        assert!(t.upsert(&[Field::U32(1)]).is_err(), "a short record is refused");
    }
}
